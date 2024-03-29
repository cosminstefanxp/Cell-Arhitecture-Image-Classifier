/*
 *  ASC - Tema4
 *  tema4.c
 *
 *  
 *  Author: Stefan-Dobrin Cosmin
 *  		331CA, Faculty of Computer Science, Politehnica University of Bucharest
 *	Created on: May 16, 2011
 */
#include <libspe2.h>
#include <pthread.h>
#include <time.h>
#include "lapack.h"


#include "util.h"
#include "imglib.h"
#include "tema4.h"

#define SPU_THREADS 8

#define DISTRIBUTE_TASK_EXIT 0xffffffff

#define MAX_FILENAME_SIZE	50

extern spe_program_handle_t tema4_spu;

char unitName[20];

/* Structure used to send arguments to pthreads initialization. */
typedef struct
{
	spe_context_ptr_t spe;
	spe_event_unit_t pevents;
	int args;
	int envp;
} thread_arg_t;

/* Problem relevant global variables. */
int L,P;
int nrImagesTraining,nrImagesClasify;
image *images1;
image *images2;
image *test_images;
int H;
int W;
int M;
char *dir1,*dir2,*dirC;


/* Communication specific variables. */
task_t tasks[SPU_THREADS] __attribute__((aligned(128)));
int distribution_init_complete;

/* Tasks variables. */
int cur_task_pos;
data_t* cur_task_destination;
int cur_strip_size;
int cur_image_num;
image *cur_images;
int cur_task_size;
int cur_matrixes_num;

/* Task 1 */
uint32 *strip_sources;

/* Task 2 */
data_t **cur_SWs;
data_t *cur_mean;

/* Task 5*/
data_t *cur_matrix;
data_t *cur_vector;



/* SPU Threads/Running relevant global variables. */
thread_arg_t args[SPU_THREADS];
pthread_t thread[SPU_THREADS];
spe_context_ptr_t spes[SPU_THREADS];
spe_event_unit_t pevents[SPU_THREADS];
spe_event_handler_ptr_t event_handler;

/* Method that runs the context for each SPU */
void* run_spu(void *thread_arg)
{

	thread_arg_t *arg = (thread_arg_t *) thread_arg;
	unsigned int entry;
	entry = SPE_DEFAULT_ENTRY;
	spe_stop_info_t stop_info;


	if ( spe_context_run(arg->spe, &entry, 0, (int*)arg->args,(int*)arg->envp,&stop_info)<0)
		perror("spe_context_run");

	return NULL;
}

/* Function that reads all the images in a given folder, and returns an array of images. */
image* read_images(char* dir, int nrImages)
{
	image *images;
	char base_name[MAX_FILENAME_SIZE],name[MAX_FILENAME_SIZE];
	char *p;
	int i,rv;
	FILE *fin;

	//Alloc space for the images
	images=calloc(nrImages,sizeof(image));

	//Create the base image name
	if(dir[strlen(dir)-1]=='/')	//if there's a / at the end, skip it
		dir[strlen(dir)-1]=0;
	strcpy(base_name,dir);

	p=strrchr(dir,'/');
	if(p==NULL)
		p=dir;	//the beginning of the name
	else
		p++;	//skip the /
	strcat(base_name,"/");
	strcat(base_name,p);
	strcat(base_name,"_");
	dlog(LOG_DEBUG,"\tFinal folder name: %s -> base image name %s",p,base_name);
	for(i=0;i<nrImages;i++)
	{
		sprintf(name,"%s%d.pgm",base_name,i+1);

		fin=fopen(name,"rb");
		DIE(fin==NULL,"Eroare citire fisier intrare");

		images[i]=read_ppm(fin);
		strncpy(images[i]->name,name,MAX_FILENAME_SIZE);

		rv=fclose(fin);
		DIE(rv!=0,"Eroare inchidere fisier intrare");

		dlog(LOG_DEBUG,"\t\tRead image \'%s\' of size %3dx%3d (alloc'ed size: %d)",\
				images[i]->name,images[i]->width,images[i]->height,CEIL_16(images[i]->width*images[i]->height));
	}

	return images;
}

void init_spus()
{
	int i;
	int rv;

	event_handler = spe_event_handler_create();

	for(i=0; i<SPU_THREADS; i++)
		{
			dprintf("Initializing spu %d",i);
			//Creating context for SPEs
			spes[i] = spe_context_create(SPE_EVENTS_ENABLE,NULL);
			DIE(spes[i]==NULL,"Error while creating spe contexts");

			//Loading the programs
			rv = spe_program_load(spes[i],&tema4_spu);
			DIE(rv!=0,"Error while loading spe program");

			//Registering the event handler
			pevents[i].events = SPE_EVENT_OUT_INTR_MBOX;
			pevents[i].spe = spes[i];
			rv=spe_event_handler_register(event_handler, &pevents[i]);
			DIE(rv!=0,"Error while registering event handler");

			//Creating the arguments that are to be given to the SPEs
			args[i].spe = spes[i];
			args[i].args = i;
			args[i].envp = SPU_THREADS;
			args[i].pevents = pevents[i];

			//Creating the threads
			rv = pthread_create(&thread[i],NULL,run_spu, &args[i]);
			DIE(rv!=0,"Error while creating thread");
		}

}

/* Allocates an array of data_t */
data_t* create_matrix(int dim_l, int dim_h) {
    data_t* mat_aux = (data_t*) memalign(128,CEIL_16(dim_l * dim_h * sizeof (data_t)));
    DIE(mat_aux==NULL,"Aloc'ing space");
    return mat_aux;
}

/* Joins the SPU_THREADS and cleanes up everything else, SPU-related. */
void cleanup_spus()
{
	int i;
	int rv;
	uint32 out_data=TASK_DATA_EXIT;

	for(i=0; i<SPU_THREADS; ++i)
	{
		rv=spe_in_mbox_write(spes[i], &out_data, 1, SPE_MBOX_ANY_NONBLOCKING);
		DIE(rv<0,"Error while writing data to SPE mbox while exiting");
	}

	for(i=0; i<SPU_THREADS; ++i)
	{
		pthread_join(thread[i], NULL);
		dlog(LOG_DEBUG,"SPU Thread %d finished.",i);
	}
	for(i=0; i<SPU_THREADS; ++i)
	{
		rv=spe_event_handler_deregister(event_handler, &pevents[i]);
		DIE(rv!=0,"Deregister spe event handler");

		rv = spe_context_destroy(spes[i]);
		DIE(rv!=0,"SPE context destroy");
	}
	dlog(LOG_INFO,"All SPU threads finished and cleaned-up.");
	rv=spe_event_handler_destroy(event_handler);
	DIE(rv!=0,"Destroy event handler");
}

/* Waits for a SPU to be finished and then it notifies him of finishing. */
void distribute_tasks(uint32 (*get_out_data)(uint32 received_data))
{
	int done=0;
	int nevents;
	int rv;
	int i;
	int spe_tasks_done=0;
	spe_event_unit_t event_received;
	uint32 received_data;
	uint32 out_data;

	//Initial distribution of tasks to free SPUs
	dlog(LOG_INFO,"Starting distribution in initial mode.");
	distribution_init_complete=0;
	for(i=0;i<SPU_THREADS;i++)
	{
		//Prepare data to send
		out_data=(uint32)get_out_data(i);
		//Check for last task of the series
		if(out_data==DISTRIBUTE_TASK_EXIT)
		{
			dlog(LOG_INFO,"Distribution finished while in initial distribution mode.");
			break;
		}
		//Send data
		rv=spe_in_mbox_write(spes[i], &out_data, 1, SPE_MBOX_ANY_NONBLOCKING);
		DIE(rv<0,"Error while writing data to SPE mbox");
		dlog(LOG_DEBUG,"\t\tSent initial data %d to %p.",out_data,spes[i]);
	}
	spe_tasks_done=SPU_THREADS-i;
	distribution_init_complete=1;

	//Continuous distribution of tasks until it's ready
	dlog(LOG_INFO,"Starting distribution in continuous mode.");
	while(spe_tasks_done<SPU_THREADS)
	{
		//Receive 1 event
		nevents = spe_event_wait(event_handler,&event_received,1,-1);
		if(nevents<0)
		{
			eprintf("Error at SPE event wait. Received: %d\n",nevents);
			continue;
		}
		dlog(LOG_CRAP,"\tReceived %d events...",nevents);

		//If it's an event on the out-interrupt mailbox
		if (event_received.events & SPE_EVENT_OUT_INTR_MBOX)
		{
			//Wait until the event is here
			while (spe_out_intr_mbox_status(event_received.spe) < 1);
			spe_out_intr_mbox_read(event_received.spe, &received_data, 1, SPE_MBOX_ANY_NONBLOCKING);
			dlog(LOG_DEBUG,"\t\tReceived return code %u from %p", received_data, event_received.spe);

			//Prepare data to send
			out_data=(uint32)get_out_data(received_data);
			//Check for last task of the series
			if(out_data==DISTRIBUTE_TASK_EXIT)
			{
				dlog(LOG_DEBUG,"\t\tDistribution task kill...");
				spe_tasks_done++;
				if(spe_tasks_done==SPU_THREADS)
				{
					dlog(LOG_INFO,"Distribution series finished.");
					done=1;
					break;
				}
				continue;	//i'm not sending a task now. I know spu finished... the spu waits for message
			}

			//Send data
			rv=spe_in_mbox_write(event_received.spe, &out_data, 1, SPE_MBOX_ANY_NONBLOCKING);
			DIE(rv<0,"Error while writing data to SPE mbox");
			dlog(LOG_CRAP,"\t\tSent data %d to %p.",out_data,event_received.spe);
		}
		/* The spe_stop_info_read loop should check for SPE_EVENT_SPE_STOPPED event received in the events mask */
		else if (event_received.events & SPE_EVENT_SPE_STOPPED)
		{
			dlog(LOG_INFO,"\tSPE %p finished and exited.",event_received.spe);
		}
		else
		{
			dlog(LOG_WARNING,"Unknown exit status from SPE=%p.",event_received.spe);
		}
	}
}

/********************************************************************************
 * TASK PRODUCERS
 ********************************************************************************/
/* Produces the tasks for the task 1 (the mean of the pixels).
 *
 * For all the initial tasks we send cur_strip_size elements for processing.
 * For the previous to last task (in order to skip an extra remaining task with few elements) we send all the elements till the end.
 */
uint32 compute_mean_task_producer(uint32 cellID)
{
	//It's done so the distribution streak should finish
	if(cur_task_pos>=M)
		return DISTRIBUTE_TASK_EXIT;
	//Build structure
	tasks[cellID].type=TASK_MEAN;
	tasks[cellID].destination=(uint32)(&(cur_task_destination[cur_task_pos]));
	tasks[cellID].mainSource=(uint32)(strip_sources);
	tasks[cellID].source1=cur_image_num;	//number of images to process
	tasks[cellID].aux2=cur_task_pos;		//the starting position in the images

	//If it's not the task for the last SPU
	if(cur_task_pos+2*cur_strip_size<M)
	{
		tasks[cellID].size=cur_strip_size;	//effective size of data to process
		tasks[cellID].aux1=cur_strip_size;	//size to fetch
		dlog(LOG_DEBUG,"\tSending mean slice of size %d (fetching %d) beginning at %d to cell %d, with strips array at %u",\
				tasks[cellID].size,tasks[cellID].aux1,cur_task_pos,cellID,tasks[cellID].mainSource);
		cur_task_pos+=cur_strip_size;
	}
	else
		//If it's the last task for the last SPU - we send all the remaining (even if it excedes cur_task_size)
		{
		tasks[cellID].size = M - cur_task_pos;
		tasks[cellID].aux1 = CEIL_16(tasks[cellID].size);
		dlog(LOG_DEBUG,"\tSending last mean slice of size %d (fetching %d) beggining at %d to cell %d, with strips array at %u",\
				tasks[cellID].size,tasks[cellID].aux1,cur_task_pos,cellID,tasks[cellID].mainSource);
		cur_task_pos+=tasks[cellID].size;
	}

	return (uint32) &tasks[cellID];
}

/* Produces the tasks for the first part of task 1 (the SWs).
 *
 * Sends an image to a SPU and the mean array, and gets the (x-u)(x-u)T result.
 */
uint32 compute_SWs_task_producer(uint32 cellID)
{
	//It's done so the distribution streak should finish
	if(cur_task_pos>=cur_task_size)
		return DISTRIBUTE_TASK_EXIT;
	//Build structure
	tasks[cellID].type=TASK_SWS;
	tasks[cellID].destination=(uint32)(cur_SWs[cur_task_pos]);
	tasks[cellID].mainSource=(uint32)(cur_images[cur_task_pos]->buf);
	tasks[cellID].size=M;
	tasks[cellID].source1=(uint32)(cur_mean);	//where to get the mean array

	dlog(LOG_DEBUG,"Sending COMPUTE_SW task for image %d to %d with mean %u",cur_task_pos,cellID,tasks[cellID].source1);
	cur_task_pos++;

	return (uint32) &tasks[cellID];
}

/* Produces the task for the addition of a given number of matrixes (cur_image_num).
 *
 * Takes one strip from each of the matrixes, computes the sum, and puts the result back in the main memory.
 */
uint32 compute_add_task_producer(uint32 cellID)
{
	//It's done so the distribution streak should finish
	if(cur_task_pos>=cur_task_size)
		return DISTRIBUTE_TASK_EXIT;

	//Build structure
	tasks[cellID].type=TASK_ADD;
	tasks[cellID].destination=(uint32)(cur_task_destination);
	tasks[cellID].mainSource=(uint32)(strip_sources);
	tasks[cellID].source1=cur_matrixes_num;	//number of matrixes to process
	tasks[cellID].aux1=cur_task_pos;		//the starting position in the images
	tasks[cellID].size=cur_strip_size;		//the size of the strip (i.e. a line of the matrix <=> M)

	dlog(LOG_DEBUG,"\tSending add slice of size %d beginning at %d to cell %d, with strips array at %u",\
				tasks[cellID].size,tasks[cellID].aux1,cellID,tasks[cellID].mainSource);
	cur_task_pos+=cur_strip_size;

	return (uint32) &tasks[cellID];
}

/* Produces the task for the multiplication of a matrix with a vector.
 *
 * Takes one strip from the image, computes the product, and puts the _single_ result value back in memory
 */
uint32 compute_mul_mat_vect_task_producer(uint32 cellID)
{
	/*******************************
	 * Code used when using DMA Access
	 *******************************/
//	//If the initial distribution is finished, get the result from the task structure
//	if(distribution_init_complete==1)
//	{
//		int pos=tasks[cellID].aux1;
//		dlog(LOG_DEBUG,"Putting result from SPU %d on %d in W matrix.",cellID,pos);
//		//I use memcpy to transfer the float in the 4 bytes of the uint32. If it's double it's also valid, as it would use the source2 field too.
//		memcpy(&(cur_task_destination[pos]),&(tasks[cellID].source1),sizeof(data_t));
//	}
	/*******************************
	 * End of Code used when using DMA Access
	 *******************************/

	//It's done so the distribution streak should finish
	if(cur_task_pos>=cur_task_size)
		return DISTRIBUTE_TASK_EXIT;

	//Build structure
	tasks[cellID].type=TASK_MUL;
	tasks[cellID].destination=(uint32)(&(cur_task_destination[cur_task_pos]));
	tasks[cellID].mainSource=(uint32)(&cur_matrix[cur_task_pos*M]);	//we give the line cur_task_pos of the matrix
	tasks[cellID].source1=(uint32)cur_vector;		//the vector
	tasks[cellID].aux1=(uint32)cur_task_pos;		//the current position in the matrix; used when returning the value back to PPU
	tasks[cellID].size=(uint32)cur_strip_size;		//the size of the strip (i.e. a line of the matrix <=> M)

	dlog(LOG_DEBUG,"\tSending add MULTIPLICATION slice of size %d beginning at %d to cell %d",\
				tasks[cellID].size,tasks[cellID].aux1,cellID);
	cur_task_pos+=1;

	return (uint32) &tasks[cellID];
}

uint32 compute_projection_task_producer(uint32 cellID)
{
	//If the initial distribution is finished, get the result from the task structure
	if(distribution_init_complete==1)
	{
		int pos=tasks[cellID].aux1;
		//I use memcpy to transfer the float in the 4 bytes of the uint32. If it's double it's also valid, as it would use the source2 field too.
		memcpy(&(cur_task_destination[pos]),&(tasks[cellID].source1),sizeof(data_t));
		dlog(LOG_INFO,"Putting result %f from SPU %d for image %d in projection matrix.",cur_task_destination[pos],cellID,pos);
	}

	//It's done so the distribution streak should finish
	if(cur_task_pos>=cur_image_num)
		return DISTRIBUTE_TASK_EXIT;

	//Build structure
	tasks[cellID].type=TASK_PROJ;
	tasks[cellID].mainSource=(uint32)((cur_images[cur_task_pos]->buf));	//we give data in the image
	tasks[cellID].source1=(uint32)cur_vector;								//the vector
	tasks[cellID].aux1=(uint32)cur_task_pos;		//the current position in the matrix; used when returning the value back to PPU
	tasks[cellID].size=(uint32)cur_task_size;		//the size of the strip (i.e. a line of the matrix <=> M)

	dlog(LOG_DEBUG,"\tSending get PROJECTION slice of size %u from pos %u to cell %d with main source %u",\
				tasks[cellID].size,tasks[cellID].aux1,cellID,tasks[cellID].mainSource);
	cur_task_pos+=1;

	return (uint32) &tasks[cellID];
}

/********************************************************************************
 * TASK INITIALIZERS
 ********************************************************************************/
/* Task 1 initializer. */
void compute_mean(image* images, int nr_images, data_t* mean)
{
	int i;

	//Create the strips array
	strip_sources=(uint32*)memalign(128,CEIL_16(nr_images*sizeof(uint32)));
	for(i=0;i<nr_images;i++)
		strip_sources[i]=(uint32)(images[i]->buf);
	cur_strip_size=FLOOR_16(M/SPU_THREADS);
	dlog(LOG_WARNING,"Starting computation of mean with strips of size %d",cur_strip_size);

	//Initializing task distribution
	cur_task_destination=mean;
	cur_task_pos=0;
	cur_image_num=nr_images;

	distribute_tasks(&compute_mean_task_producer);
	dlog(LOG_WARNING,"Mean computation finished for %d images from %p.",nr_images,images);

	free(strip_sources);
//	sleep(1);
//	for(i=0;i<M;i++)
//	{
//		if(i%W==0)
//			printf("\n\n");
//		printf("%.2f ",mean[i]);
//	}
//	printf("\n\n----%d---\n",M);
}

/* Task 2 initializer. */
void compute_scatter_matrix(image* images, int nr_images, data_t* mean, data_t* SW)
{
	data_t **SWs;
	int i;
	//Alloc' the necessary space
	SWs=(data_t**)malloc(nrImagesTraining*sizeof(data_t*));
	DIE(SWs==NULL,"Cannot allocate memory.");
	for(i=0;i<nrImagesTraining;i++)
	{
		SWs[i]=create_matrix(M,M);
	}

	//Prepare the tasks
	cur_SWs=SWs;			//where to put the results
	cur_task_pos=0;			//the next task to send
	cur_images=images;		//the array of images
	cur_task_size=nr_images;//the number of images (tasks)
	cur_mean=mean;			//the mean computed previously

	dlog(LOG_INFO,"Completed computation of SWs matrixes for images %p.",images);

	//Distribute the tasks to compute the SWs
	distribute_tasks(compute_SWs_task_producer);

	//Prepare the tasks for addition of matrixes
	cur_task_destination=SW;
	cur_matrixes_num=nr_images;
	cur_task_pos=0;
	cur_task_size=M*M;
	cur_strip_size=M;
	assert(M*sizeof(data_t)%16==0);	//as promised, the product W*H will divide by 4
	//Create the strips array
	strip_sources=(uint32*)memalign(128,CEIL_16(nr_images*sizeof(uint32)));
	for(i=0;i<nr_images;i++)
		strip_sources[i]=(uint32)(SWs[i]);

	//Distribute the tasks to compute the additions
	distribute_tasks(&compute_add_task_producer);

	dlog(LOG_INFO,"Completed addition of SWs matrixes for images %p.",images);

//	printf("\n\n");
//	for(i=0;i<M;i++)
//		printf("%3.2f ",SW[i]);
//	printf("\n\n");
//	for(i=0;i<M;i++)
//			printf("%3.2f ",SW[M*(M-150)+i]);

	//Cleanup
	free(strip_sources);
	for(i=0;i<nr_images;i++)
		free(SWs[i]);
	free(SWs);

}

/* Task 3 initializer. */
void compute_SW(data_t* SW1, data_t* SW2, data_t* SW)
{
	//Prepare the tasks for addition of matrixes
	cur_task_destination=SW;
	cur_matrixes_num=2;
	cur_task_pos=0;
	cur_task_size=M*M;
	cur_strip_size=M;
	assert(M*sizeof(data_t)%16==0);	//as promised, the product W*H will be divide by 4
	//Create the strips array
	strip_sources=(uint32*)memalign(128,CEIL_16(2*sizeof(uint32)));
	strip_sources[0]=(uint32)(SW1);
	strip_sources[1]=(uint32)(SW2);

	dlog(LOG_INFO,"Starting computation of SW matrix as Sw1*Sw2.");
	//Distribute the tasks to compute the additions
	distribute_tasks(&compute_add_task_producer);

//	FILE* fout=fopen("out_w","w");
//	DIE(fout==NULL,"Creare fisier");
//
//	int i,j;
//	for(i=0;i<M;i++)
//	{
//		for(j=0;j<M;j++)
//			fprintf(fout,"%3.3f ",SW[M*i+j]);
//		fprintf(fout,"\n");
//	}
	dlog(LOG_INFO,"Completed computation of SW matrix.");



	//Cleanup
	free(strip_sources);
}

/* Task 4 executer. */
void inverse_matrix(data_t* SW)
{
    int info = 0;
    double *a;
    int *ipiv;
    int i;
    double rand;
    int n=M;

    dlog(LOG_WARNING,"Starting matrix inversion algorithm.");
    posix_memalign((void **)((void*)(&ipiv)), 128, sizeof(int)*M);
    DIE(ipiv==NULL,"Allocation error");
    posix_memalign((void **)((void*)(&a)), 128, sizeof(double)*M*M);
    DIE(a==NULL,"Allocation error");

    /* Preparing the data...*/
    srand48(time(NULL));
    for( i = 0; i < M*M; i++ )
    {
        	a[i] = SW [i];
            if (a[i] == 0)
                a[i] += ((drand48() - 0.5f) / 1e2);
    }

    /*prevent matrix from being singular*/
    rand=0;
    while(rand<(1/1e5))
    	rand = ((drand48() - 0.5f));
    dlog(LOG_INFO,"Rand chosen: %lf",rand);

    for (i=0; i<M; i++)
    	a[i + i * M] += rand * a[i + i * M];
    dlog(LOG_INFO,"Preprocessing done for n=%d.",n);

    /*---------Call Cell LAPACK library---------*/
    dgetrf_(&n, &n, a, &n, ipiv, &info);
    DIE(info!=0,"dgetrf error");
    dlog(LOG_INFO,"Round 1 done.");

    /*---------Query workspace-------*/
    double workspace;
    int tmp=-1;
    int lwork;
    double *work;
    dgetri_(&n, a, &n, ipiv, &workspace, &tmp, &info);
    lwork = (int)workspace;
    work = malloc(sizeof(double)*lwork);
    DIE(work==NULL,"Allocation error");
    dlog(LOG_INFO,"Round 2 done.");

    /*---------Call Cell LAPACK library---------*/
    dgetri_(&n, a, &n, ipiv, work, &lwork, &info);
    DIE(info!=0,"dgetri error");
    dlog(LOG_INFO,"Round 3 done.");

    for(i=0;i<M*M;i++)
    	SW[i]=(float)a[i];

    free(ipiv);
    free(a);
    free(work);

//    printf("\n\n");
//    for(i=0;i<M;i++)
//    	printf("%3.4f ", SW[i]);
//	printf("\n\n");


    dlog(LOG_INFO,"Finished matrix inversion algorithm.");
}

/* Task 5 initializer. */
void compute_W(data_t *mean_diff, data_t* SW, data_t* WM)
{
	//Prepare the tasks for computation of W
	cur_task_destination=WM;
	cur_task_pos=0;			//the current line processing
	cur_matrix=SW;			//the matrix
	cur_strip_size=M;		//M elements per line
	cur_task_size=M;		//M lines
	cur_vector=mean_diff;

	//Start the distribution
	distribute_tasks(&compute_mul_mat_vect_task_producer);

//	int i;
//	printf("\n\n");
//	for(i=0;i<M;i++)
//		printf("%3.3f ",WM[i]);
//	printf("\n\n");
}

/* Task 6 initializer.
 * Returns the array of projections.
 */
data_t* get_projection(image *images, int images_nr, data_t *WM)
{
	//Init the memory
	data_t *projections;
	projections=calloc(images_nr,sizeof(data_t));

	//Prepare the tasks for computation of W
	cur_task_destination=projections;
	cur_task_pos=0;			//the current line processing
	cur_images=images;		//the images
	cur_image_num=images_nr;//the number of images
	cur_task_size=M;		//M elements in the images/vector
	cur_vector=WM;

	//Start the distribution
	distribute_tasks(&compute_projection_task_producer);

	return projections;
}

int main(int argc, char **argv)
{
	//Init parameters
	int i;

	/********************* INIT ****************************/
	sprintf(unitName,"PPU  ");
	//Check run parameters. Example ./{exec} tigru elefant 10 -c test 2
	if(argc!=7)
	{
		eprintf("Numar incorect de parametrii. Folosire: %s {clasa_1} {clasa_2} {nr_img_training}"
				" -c {folder_clasificare} {nr_img_clasif}\n",argv[0]);
		exit(EXIT_FAILURE);
	}
	dir1=argv[1];
	dir2=argv[2];
	nrImagesTraining=atoi(argv[3]);

	assert(argv[4][1]=='c');
	dirC=argv[5];
	nrImagesClasify=atoi(argv[6]);
	dlog(LOG_INFO,"INIT complete. \n\tTraining folders: \'%s\' \'%s\', with %d images\n\tL=%d\tP=%d\t"
			"Clasification folder: \'%s\' with %d images.",dir1,dir2,nrImagesTraining,L,P,dirC,nrImagesClasify);

	//Reading images
	dlog(LOG_DEBUG,"Reading images from %s folder.",dir1);
	images1=read_images(dir1,nrImagesTraining);
	dlog(LOG_DEBUG,"Reading images from %s folder.",dir2);
	images2=read_images(dir2,nrImagesTraining);
	dlog(LOG_INFO,"Training images read. Reading test images from %s folder...",dirC);
	test_images=read_images(dirC,nrImagesClasify);

    H=images1[0]->height;	//we consider all the images to have the same dimensions (equal to first image)
    W=images1[0]->width;
    M=H*W;
    dlog(LOG_WARNING,"All images have been read. W: %d; H: %d",W,H);

	//Init SPU threads
	init_spus();
	dlog(LOG_INFO,"Initialized SPU threads.");


	//Task processing preparation...
    data_t *mean_type1, *mean_type2,*mean_diff;
    data_t *SW1,*SW2,*SW;



	/*********************** TASK 1 **************************/
	dlog(LOG_CRIT,"\n\n/****************** TASK 1 *******************\\\n");
    //Alloc' the necessary space
    mean_type1 = create_matrix(M,1);
    mean_type2 = create_matrix(M,1);
    //Compute the means
	compute_mean(images1, nrImagesTraining,mean_type1);
	compute_mean(images2, nrImagesTraining,mean_type2);

	/*********************** TASK 2 **************************/
	dlog(LOG_CRIT,"\n\n/****************** TASK 2 *******************\\\n");
    //Alloc' the necessary space
    SW1 = create_matrix(M,M);
    SW2 = create_matrix(M,M);
    //Compute the means
	compute_scatter_matrix(images1,nrImagesTraining,mean_type1,SW1);
	compute_scatter_matrix(images2,nrImagesTraining,mean_type2,SW2);

	/*********************** TASK 3 **************************/
	dlog(LOG_CRIT,"\n\n/****************** TASK 3 *******************\\\n");
	SW = create_matrix(M,M);
	compute_SW(SW1,SW2,SW);
	free(SW1);
	free(SW2);

	/*********************** TASK 4 **************************/
	dlog(LOG_CRIT,"\n\n/****************** TASK 4 *******************\\\n");
	inverse_matrix(SW);

	/*********************** TASK 5 **************************/
	dlog(LOG_CRIT,"\n\n/****************** TASK 5 *******************\\\n");
	//Computing the u1-u2 on PPU as the overhead would be bigger if sent on SPU (not too many elements)
	mean_diff=create_matrix(M,1);
	for(i=0;i<M;i++)
		mean_diff[i]=mean_type1[i]-mean_type2[i];
	free(mean_type1);
	free(mean_type2);
	data_t *WM=create_matrix(M,1);	//W Matrix
	//Compute the W array
	compute_W(mean_diff, SW, WM);

	/*********************** TASK 6 **************************/
	dlog(LOG_CRIT,"\n\n/****************** TASK 6 *******************\\\n");
	data_t *projections1,*projections2;
	projections1=get_projection(images1,nrImagesTraining,WM);
	projections2=get_projection(images2,nrImagesTraining,WM);

	/*********************** TASK 7 **************************/
	dlog(LOG_CRIT,"\n\n/****************** TASK 7 *******************\\\n");
	data_t mean_proj1=0,mean_proj2=0;
	data_t sd_proj1=0,sd_proj2=0;
	for(i=0;i<nrImagesTraining;i++)
	{
		mean_proj1+=projections1[i];
		mean_proj2+=projections2[i];
	}
	mean_proj1/=nrImagesTraining;
	mean_proj2/=nrImagesTraining;
	for(i=0;i<nrImagesTraining;i++)
	{
		 sd_proj1+=(projections1[i]-mean_proj1)*(projections1[i]-mean_proj1);
		 sd_proj2+=(projections2[i]-mean_proj2)*(projections2[i]-mean_proj2);
	}
	sd_proj1/=nrImagesTraining;
	sd_proj2/=nrImagesTraining;
	dlog(LOG_WARNING,"Image type 1 - mean: %f - sd: %f",mean_proj1,sd_proj1);
	dlog(LOG_WARNING,"Image type 2 - mean: %f - sd: %f",mean_proj2,sd_proj2);

	/*********************** CLASIFICATION **************************/
	dlog(LOG_CRIT,"\n\n/****************** CLASIFICATION *******************\\\n");
	data_t *projections_clasif;
	projections_clasif=get_projection(test_images,nrImagesClasify,WM);
	for(i=0;i<nrImagesClasify;i++)
	{
        if (abs(projections_clasif[i] - mean_proj1) < abs(projections_clasif[i] - mean_proj2))
        {
            printf("Image %s - Classified as %s\n",test_images[i]->name,dir1);
            continue;
        }

        if (abs(projections_clasif[i] - mean_proj1) > abs(projections_clasif[i] - mean_proj2))
        {
            printf("Image %s - Classified as %s\n",test_images[i]->name,dir2);
            continue;
        }

        printf("Image %s - Not classified\n", test_images[i]->name);
	}

	/********************** CLEANUP **************************/
	dlog(LOG_CRIT,"\n\n/***************** CLEANUP ******************\\\n");
	dlog(LOG_INFO,"Starting clean-up.");
	cleanup_spus();

	//Cleanup images
	for(i=0;i<nrImagesTraining;i++)
	{
		free_img(images1[i]);
		free_img(images2[i]);
	}
	for(i=0;i<nrImagesClasify;i++)
	{
		free_img(test_images[i]);
	}
	free(images1);
	free(images2);
	free(projections1);
	free(projections2);
	free(projections_clasif);
	free(test_images);
	dlog(LOG_DEBUG,"Clean-up complete.");

	return EXIT_SUCCESS;
}

