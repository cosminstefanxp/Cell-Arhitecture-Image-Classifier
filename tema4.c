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
int cur_task_num;
data_t* cur_task_destination;


/* Task Mean */
uint32 *strip_sources;



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

		dlog(LOG_DEBUG,"\t\tRead image \'%s\' of size %4dx%4d",images[i]->name,images[i]->width,images[i]->height);
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
    data_t* mat_aux = (data_t*) memalign(128,dim_l * dim_h * sizeof (data_t));
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
		dlog(LOG_DEBUG,"\tReceived %d events...",nevents);

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
			dlog(LOG_DEBUG,"\t\tSent data %d to %p.",out_data,event_received.spe);
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
/* Produces the tasks for the task 1 (the mean of the pixels) */
uint32 compute_mean_task_producer(uint32 cellID)
{
	//Build structure
	int slice_size;
	slice_size = M/SPU_THREADS;

	if(cur_task_num<SPU_THREADS)
	{
		tasks[cellID].type=TASK_MEAN;
		tasks[cellID].destination=(uint32)(&(cur_task_destination[cellID*slice_size]));
		tasks[cellID].mainSource=(uint32)(strip_sources);
	}
	//If it's not the last task
	if(cur_task_num<SPU_THREADS-1)
	{
		tasks[cellID].size=slice_size;
		tasks[cellID].aux1=slice_size*cellID;
		dlog(LOG_DEBUG,"\tSending slice %d of size: %d to %d, with strips array at %u",cur_task_num,tasks[cellID].size,cellID,tasks[cellID].mainSource);
		cur_task_num++;
		return (uint32)&tasks[cellID];
	}
	else
		//If it's the last task
		if(cur_task_num==SPU_THREADS-1)
		{
			tasks[cellID].size= M - cur_task_num*slice_size;
			tasks[cellID].aux1=slice_size*cellID;
			dlog(LOG_DEBUG,"\tSending slice %d of size: %d to %d, with strips array at %u",cur_task_num,tasks[cellID].size,cellID,tasks[cellID].mainSource);
			cur_task_num++;
			return (uint32)&tasks[cellID];
		}
	cur_task_num++;
	//It's done so the distribution streak should finish
	return DISTRIBUTE_TASK_EXIT;
}


/********************************************************************************
 * TASK INITIALIZERS
 ********************************************************************************/
void compute_mean(image* images, int nr_images, data_t* mean)
{
	//Create the strips array
	strip_sources=(uint32*)memalign(128,nr_images*sizeof(uint32));
	int i;

	for(i=0;i<nr_images;i++)
		strip_sources[i]=(uint32)(images[i]->buf);

	//Initializing task distribution
	cur_task_destination=mean;
	cur_task_num=0;

	distribute_tasks(&compute_mean_task_producer);
	dlog(LOG_WARNING,"Mean computation finished for %d images from %p.",nr_images,images);
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

	//Init memory
    data_t *mean_type1, *mean_type2, *Sw, *Sw1, *Sw2, *Swinv;
    data_t *matrix1, *matrix2, *matrix3;

    //allocate all the matrices
    matrix1 = create_matrix(M,1);
    matrix2 = create_matrix(1,M);
    matrix3 = create_matrix(M,M);
    mean_type1 = create_matrix(M,1);
    mean_type2 = create_matrix(M,1);
    Sw1 = create_matrix(M, M);
    Sw2 = create_matrix(M, M);



	/*********************** TASK 1 **************************/
	dlog(LOG_CRIT,"\n\n/****************** TASK 1 *******************\\\n");
	compute_mean(images1, nrImagesTraining,mean_type1);

	/*********************** TASK 2 **************************/
	dlog(LOG_CRIT,"\n\n/****************** TASK 2 *******************\\\n");

	/*********************** TASK 3 **************************/
	dlog(LOG_CRIT,"\n\n/****************** TASK 3 *******************\\\n");


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
	free(test_images);
	dlog(LOG_DEBUG,"Clean-up complete.");

	return EXIT_SUCCESS;
}

