/*
 *  ASC - Tema3
 *  clasificator.c
 *
 *  
 *  Author: Stefan-Dobrin Cosmin
 *  		331CA, Faculty of Computer Science, Politehnica University of Bucharest
 *	Created on: Apr 26, 2011
 */
#include <libspe2.h>
#include <pthread.h>
#include <time.h>

#include "util.h"
#include "imglib.h"
#include "clasificare.h"

#define SPU_THREADS 8

#define DISTRIBUTE_TASK_EXIT 0xffffffff

#define MAX_FILENAME_SIZE	50

extern spe_program_handle_t clasificare_spu;

char unitName[20];

/* Structure used to send arguments to pthreads initialization. */
typedef struct
{
	spe_context_ptr_t spe;
	spe_event_unit_t pevents;
	int args;
	int envp;
} thread_arg_t;

/* Time counting variables. */
clock_t time_start,time_prev,time_cur;

/* Problem relevant global variables. */
int L,P;
int nrImagesTraining,nrImagesClasify;
image *images1;
image *images2;
image *test_images;
int H;
int W;
char *dir1,*dir2,*dirC;


/* Task 1 global variables. */
uint32 **pixel_count;
uint8 pixel_count_tasks;
image cur_image;

/* Task 2&3 global variables. */
state_t *states1;
state_t *states2;
int nr_states1;
int nr_states1_not_delete;
int nr_states2;
int nr_states2_not_delete;
uint32 **strip_sources;

state_t* cur_states;
image* cur_images;
int cur_nr_states=0;
int cur_state;
int cur_nr_images;
int cur_count;
int distribution_init_complete;		//1 if the initial set of tasks is complete, 0 otherwise



/* Communication specific variables. */
task_t tasks[SPU_THREADS] __attribute__((aligned(128)));



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
		assert(images[i]->width==800);
		assert(images[i]->height=800);
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
			rv = spe_program_load(spes[i],&clasificare_spu);
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

/* Produces the tasks for the first part of task 1 (counting the pixels in each strip) */
uint32 pixel_count_task_producer(uint32 cellID)
{
	//Build structure
	int slice_size;
	slice_size = (cur_image->width * cur_image->height)/SPU_THREADS;

	tasks[cellID].type=TASK_1A_COUNT;
	tasks[cellID].destination=(uint32)pixel_count[cellID];
	tasks[cellID].mainSource=(uint32)(&(cur_image->buf[pixel_count_tasks*slice_size]));

	//If it's not the last task
	if(pixel_count_tasks<SPU_THREADS-1)
	{
		tasks[cellID].size=slice_size;
		dlog(LOG_DEBUG,"\tSending slice %d of size: %d to %d, starting from %u",pixel_count_tasks,tasks[cellID].size,cellID,tasks[cellID].mainSource);
		pixel_count_tasks++;
		return (uint32)&tasks[cellID];
	}
	else
		//If it's the last task
		if(pixel_count_tasks==SPU_THREADS-1)
		{
			tasks[cellID].size= cur_image->width * cur_image->height - pixel_count_tasks*slice_size;
			dlog(LOG_DEBUG,"\tSending slice %d of size: %d to %d, starting from %u",pixel_count_tasks,tasks[cellID].size,cellID,tasks[cellID].mainSource);
			pixel_count_tasks++;
			return (uint32)&tasks[cellID];
		}
	pixel_count_tasks++;
	//It's done so the distribution streak should finish
	return DISTRIBUTE_TASK_EXIT;
}


/* Produces the tasks for the third part of task 1 (histogram equalization in each strip) */
uint32 histogram_equalization_task_producer(uint32 cellID)
{
	//Build structure
	int slice_size;
	slice_size = (cur_image->width * cur_image->height)/SPU_THREADS;

	tasks[cellID].type=TASK_1B_EQUALIZATION;
	tasks[cellID].mainSource=(uint32)(&(cur_image->buf[pixel_count_tasks*slice_size]));
	tasks[cellID].destination=tasks[cellID].mainSource;
	tasks[cellID].source1=(uint32)(pixel_count[SPU_THREADS]);	//the address of the agregating pixel count vector
	tasks[cellID].aux1=cur_image->width*cur_image->height;

	//If it's not the last task
	if(pixel_count_tasks<SPU_THREADS-1)
	{
		tasks[cellID].size=slice_size;
		dlog(LOG_DEBUG,"\tSending slice %d of size: %d to %d, starting from %u",pixel_count_tasks,tasks[cellID].size,cellID,tasks[cellID].mainSource);
		pixel_count_tasks++;
		return (uint32)&tasks[cellID];
	}
	else
		//If it's the last task
		if(pixel_count_tasks==SPU_THREADS-1)
		{
			tasks[cellID].size= cur_image->width * cur_image->height - pixel_count_tasks*slice_size;
			dlog(LOG_DEBUG,"\tSending slice %d of size: %d to %d, starting from %u. Total image size: %u",pixel_count_tasks,tasks[cellID].size,cellID,tasks[cellID].mainSource,tasks[cellID].aux1);
			pixel_count_tasks++;
			return (uint32)&tasks[cellID];
		}
	pixel_count_tasks++;
	//It's done so the distribution streak should finish
	return DISTRIBUTE_TASK_EXIT;
}

/* Produces the tasks for the second task (computing the averages and stddevs) */
uint32 build_hmm_task_producer(uint32 cellID)
{
	state_t *state;

	cur_state++;
	if(cur_state>=cur_nr_states)
	{
		dlog(LOG_DEBUG,"Sending hmm tasks finished. Current state: %d.",cur_state);
		return DISTRIBUTE_TASK_EXIT;
	}

	state=&cur_states[cur_state];
	dlog(LOG_DEBUG,"\tSending strip %d/%d to SPU %u , from height %u to %u.",cur_state,cur_nr_states,cellID,state->start_height,state->stop_height);

	//Complete the strip_sources array
	int i;
	for(i=0;i<cur_nr_images;i++)
	{
		strip_sources[cellID][i]=(uint32)&(cur_images[i]->buf[state->start_height*W]);
		//dlog(LOG_CRAP,"\t\tSending address %u from position %d",strip_sources[cellID][i],state->start_height*W);
	}
	//Building the state
	tasks[cellID].size=(state->stop_height-state->start_height)*W;
	tasks[cellID].mainSource=(uint32)strip_sources[cellID];		//the vector of the addresses of the image strips
	tasks[cellID].aux1=(uint32)state->average;					//the address of the vector where the averages should be placed
	tasks[cellID].aux2=(uint32)state->stddev;					//the address of the vector where the stddevs should be placed
	tasks[cellID].source1=cur_nr_images;						//the number of images that are needed
	tasks[cellID].type=TASK_2A_AVERAGE_CALC;

	return (uint32)&tasks[cellID];
}

/* Produces the tasks for the third task (comparing strips from the test images with the model) */
uint32 compare_strips_task_producer(uint32 cellID)
{
	state_t *state;

	//Check for result from SPUs
	if(distribution_init_complete)
	{
		if(tasks[cellID].destination==STATE_SIMILAR)
		{
			cur_count++;
			dlog(LOG_DEBUG,"\tSPU %d decided that previous strip was SIMILAR with model.",cellID);
		}
		else
			dlog(LOG_DEBUG,"\tSPU %d decided that previous strip was NOT SIMILAR with model.",cellID);
	}
	else
	{
		dlog(LOG_CRAP,"\t\tCompare strip task request for SPU %d, but initial distribution not finished."
				" Ignoring result from SPU.",cellID);
	}

	//Check for finish
	cur_state++;
	if(cur_state>=cur_nr_states)
	{
		dlog(LOG_DEBUG,"Sending compare tasks finished. Current state: %d.",cur_state);
		return DISTRIBUTE_TASK_EXIT;
	}

	//Check for deleted state
	state=&cur_states[cur_state];
	while(state->is_deleted==1)
	{
		cur_state++;
		if(cur_state>=cur_nr_states)
		{
			dlog(LOG_DEBUG,"Sending compare tasks finished. Current state: %d.",cur_state);
			return DISTRIBUTE_TASK_EXIT;
		}
		state=&cur_states[cur_state];
	}
	dlog(LOG_DEBUG,"\tSending strip %d/%d to SPU %u , from height %u to %u.",cur_state,cur_nr_states,cellID,state->start_height,state->stop_height);

	//Building the state
	tasks[cellID].size=(state->stop_height-state->start_height)*W;
	tasks[cellID].aux1=(uint32)state->average;					//the address of the vector where the averages are placed
	tasks[cellID].aux2=(uint32)state->stddev;					//the address of the vector where the stddevs are placed
	tasks[cellID].mainSource=(uint32)&cur_image->buf[state->start_height*W];	//the address of the image strip
	tasks[cellID].type=TASK_3_COMPARE;

	return (uint32)&tasks[cellID];
}

void histogram_equalization()
{
	int i,j;
	int sum;
	dlog(LOG_INFO,"Starting pixel counting tasks.");

	//Initialization and data preparation
	pixel_count=(uint32**)malloc(sizeof(uint32*)*(SPU_THREADS+1));	//on the last slot we put the final result (computed by PPU)
	for(i=0;i<=SPU_THREADS;i++)
		pixel_count[i]=(uint*)memalign(128,256*sizeof(uint32));

	//Process each image
	for(i=0;i<nrImagesTraining;i++)
	{
		dlog(LOG_INFO,"\n\n-------------Processing TIGER IMAGE %d--------------\n",i);
		cur_image=images1[i];
		//Produce the tasks for initial counting
		pixel_count_tasks=0;
		distribute_tasks(&pixel_count_task_producer);
		dlog(LOG_INFO,"<------ Finished PIXEL COUNTING task for tiger image %d. Agregating... ---->",i);

		//Agregate the results
		memset(pixel_count[SPU_THREADS],0,256*sizeof(uint32));

		register int k;
		for(j=0;j<SPU_THREADS;j++)
		{
			sum=0;
			for(k=0;k<256;k++)
			{
				sum+=pixel_count[j][k];
				pixel_count[SPU_THREADS][k]+=sum;
			}
		}
		dlog(LOG_INFO,"<------ Finished AGREGATING pixel counting for tiger image %d. Sending tasks to SPUs ---->",i);

		//Send next tasks to SPU's - histogram equalization
		pixel_count_tasks=0;
		distribute_tasks(&histogram_equalization_task_producer);
		dlog(LOG_INFO,"<------ Finished HISTOGRAM EQUALIZATION task for tiger image %d. ---->",i);

#ifdef WRITE_PHOTOS_
		char fileno[30];
		sprintf(fileno,"out1_%d.pgm",i);
		FILE *fout=fopen(fileno,"wt");
		write_ppm(fout,cur_image);
#endif

	}

	//Process each image
	for(i=0;i<nrImagesTraining;i++)
	{
		dlog(LOG_INFO,"\n\n-------------Processing ELEPHANT IMAGE %d--------------\n",i);
		cur_image=images2[i];
		//Produce the tasks for initial counting
		pixel_count_tasks=0;
		distribute_tasks(&pixel_count_task_producer);
		dlog(LOG_INFO,"<------ Finished PIXEL COUNTING task for elephant image %d. Agregating... ---->",i);

		//Agregate the results
		memset(pixel_count[SPU_THREADS],0,256*sizeof(uint32));

		register int k;
		for(j=0;j<SPU_THREADS;j++)
		{
			sum=0;
			for(k=0;k<256;k++)
			{
				sum+=pixel_count[j][k];
				pixel_count[SPU_THREADS][k]+=sum;
			}
		}
		dlog(LOG_INFO,"<------ Finished AGREGATING pixel counting for elephant image %d. Sending tasks to SPUs ---->",i);

		//Send next tasks to SPU's - histogram equalization
		pixel_count_tasks=0;
		distribute_tasks(&histogram_equalization_task_producer);
		dlog(LOG_INFO,"<------ Finished HISTOGRAM EQUALIZATION task for elephant image %d. ---->",i);

#ifdef WRITE_PHOTOS_
		char fileno[30];
		sprintf(fileno,"out2_%d.pgm",i);
		FILE *fout=fopen(fileno,"wt");
		write_ppm(fout,cur_image);
#endif

	}

	//Process each image
	for(i=0;i<nrImagesClasify;i++)
	{
		dlog(LOG_INFO,"\n\n-------------Processing TEST IMAGE %d--------------\n",i);
		cur_image=test_images[i];
		//Produce the tasks for initial counting
		pixel_count_tasks=0;
		distribute_tasks(&pixel_count_task_producer);
		dlog(LOG_INFO,"<------ Finished PIXEL COUNTING task for test image %d. Agregating... ---->",i);

		//Agregate the results
		memset(pixel_count[SPU_THREADS],0,256*sizeof(uint32));

		register int k;
		for(j=0;j<SPU_THREADS;j++)
		{
			sum=0;
			for(k=0;k<256;k++)
			{
				sum+=pixel_count[j][k];
				pixel_count[SPU_THREADS][k]+=sum;
			}
		}
		dlog(LOG_INFO,"<------ Finished AGREGATING pixel counting for test image %d. Sending tasks to SPUs ---->",i);

		//Send next tasks to SPU's - histogram equalization
		pixel_count_tasks=0;
		distribute_tasks(&histogram_equalization_task_producer);
		dlog(LOG_INFO,"<------ Finished HISTOGRAM EQUALIZATION task for elephant image %d. ---->",i);

#ifdef DEBUG_
		char fileno[30];
		sprintf(fileno,"out2_%d.pgm",i);
		FILE *fout=fopen(fileno,"wt");
		write_ppm(fout,cur_image);
#endif

	}

	//Cleaning up memory
	for(i=0;i<SPU_THREADS+1;i++)
		free(pixel_count[i]);
	free(pixel_count);
	dlog(LOG_INFO,"Finished pixel counting tasks.");
}

/* Determines if 2 states can be merged. */
int states_are_mergeable(state_t* state1, state_t* state2, float threshold)
{
    int common_pixels1 = 0, common_pixels2 = 0, i, common_pixels;
    int m1, m2, sd1, sd2;

    /*determine if 2 states can be merged*/
    for (i = 0; i < L*W; i++)
    {
		m1 = (int) state1->average[i];
		m2 = (int) state2->average[i];
		sd1 = (int) state1->stddev[i];
		sd2 = (int) state2->stddev[i];
		if ((m1 - sd1 < m2) && (m2 < m1 + sd1))
			common_pixels1++;
		if ((m2 - sd2 < m1) && (m1 < m2 + sd2))
			common_pixels2++;
    }
    common_pixels = (common_pixels1 + common_pixels2) / 2;
    //dlog(LOG_CRAP,"%d - %lf",common_pixels,common_pixels / (float) (W * L));

    /*Check if it can be merged. */
    if (common_pixels / (float) (W * L) < threshold)
        return STATE_NOT_MERGEABLE;

    if (common_pixels1 > common_pixels2)
        return STATE_MERGE_1;

    return STATE_MERGE_2;
}

int build_hmm(int hmm_mode)
{
	//Initialization
    dlog(LOG_INFO,"Starting build hmm tasks.");
	state_t** states_ptr = ((hmm_mode == HMM_MODE_TIGER) ? &states1 : &states2);
    float state_threshold = ((hmm_mode == HMM_MODE_TIGER) ? TIGER_STATE_THRESHOLD : ELEPH_STATE_THRESHOLD);
    int i,j,k;

    cur_images = ((hmm_mode == HMM_MODE_TIGER) ? images1 : images2);
    cur_nr_states=0;
    cur_state=-1;
    cur_nr_images=nrImagesTraining;

    //Allocating necessary memory
    int temp_states_no = (H-P)/(L-P) + 1;
    dlog(LOG_DEBUG,"Initial aproximation of states' number: %d",temp_states_no);
    *states_ptr=malloc((temp_states_no+10)*sizeof(state_t));	//for states
    cur_states=*states_ptr;
    DIE(cur_states==NULL,"Cannot alocate memory");

	strip_sources=(uint32**)malloc(sizeof(uint32*)*(SPU_THREADS));	//for addresses of strips
	DIE(strip_sources==NULL,"Cannot alocate memory");
	for(i=0;i<SPU_THREADS;i++)
	{
		strip_sources[i]=(uint*)memalign(128,cur_nr_images*sizeof(uint32));
		DIE(strip_sources[i]==NULL,"Cannot alocate memory");
	}

    //Allocate memory and establish the real maximum number of states and the start height and stop height for each state.
	assert(L*W <= 16384);
    for (i = 0; i < H; i += L)
    {
    	cur_states[cur_nr_states].stddev=memalign(128,L*W*sizeof(unsigned char));
    	cur_states[cur_nr_states].average=memalign(128,L*W*sizeof(unsigned char));
        cur_states[cur_nr_states].start_height = i;
        cur_states[cur_nr_states].stop_height = MIN(i + L, H);
        cur_nr_states++;
        i -= P; /*overlap*/
    }
    assert(cur_nr_states<=temp_states_no+10);
    dlog(LOG_INFO,"Initial allocation complete for %d states of height %d, with a maximum number of elements %d. Distributing tasks.",cur_nr_states,L,L*W);

    //Distribute tasks to SPUs
    distribute_tasks(build_hmm_task_producer);
    dlog(LOG_INFO,"Tasks distribution finished. Merging states...");

    //Merge the states
    k = 0;
    j = 1;
    i = 0;
    int remaining_states=cur_nr_states;
    int aux;
    while ((i < cur_nr_states) && (j < cur_nr_states))
    {
    	//Get state of merge-ability
    	aux = states_are_mergeable(&cur_states[i], &cur_states[j], state_threshold);

    	//dlog(LOG_CRAP,"%d %d: %d", i, j, aux);
    	if (aux == STATE_NOT_MERGEABLE)
    	{ /*not mergeable*/
    		i=j;
    		j++;
    		continue;
    	}
    	if (aux == STATE_MERGE_1)
    	{ /*mergeable, keep i*/
    		cur_states[j].is_deleted = 1;
    		j++;
    		remaining_states--;
    		continue;
    	}
    	if (aux == STATE_MERGE_2)
    	{ /*mergeable, keep j*/
    		cur_states[i].is_deleted = 1;
    		i = j;
    		j = i + 1;
    		remaining_states--;
    		continue;
    	}
    }

#ifdef DEBUG_
    dlog(LOG_DEBUG,"HMM states: ");
    for (i = 0; i < cur_nr_states; i++)
    {
    	if (cur_states[i].is_deleted != 1)
    	{
    		dlog(LOG_DEBUG,"[%d %d] ", cur_states[i].start_height, cur_states[i].stop_height);
    	}
    }
#endif
    dlog(LOG_INFO,"MERGE Complete! Chose %d states out of a total of %d states.", remaining_states, cur_nr_states);

    //Cleanup and finalize
    for(i=0;i<SPU_THREADS;i++)
    	free(strip_sources[i]);
    free(strip_sources);

    if(hmm_mode==HMM_MODE_TIGER)
    {
    	nr_states1=cur_nr_states;
    	nr_states1_not_delete=remaining_states;
    }
    else
    {
    	nr_states2=cur_nr_states;
    	nr_states2_not_delete=remaining_states;
    }

    return cur_nr_states;
}


void analyze_img()
{
	int i;
	uint tiger_count,elephant_count;
	/* Variables used:
	 * cur_image - current test image in process.
	 * cur_state - index of current state.
	 * cur_states - current array of states (tiger or elephant)
	 * cur_nr_states - current maximum number of states.
	 */


	//Some checking & initialization
	assert(nr_states1==nr_states2);

	//Compare each image with the models and choose which it resembles
	//nrImagesClasify=1;
	for(i=0;i<nrImagesClasify;i++)
	{
		dlog(LOG_INFO,"Starting analyze tasks for test image %d. Comparing with TIGER model...",i);
		time_prev=clock();
		//Initializing  variables for tiger comparation.
		dlog(LOG_INFO,"----------------- Comparing with TIGER MODEL ---------------");
		cur_image=test_images[i];
		cur_state=-1;
		cur_states=states1;
		cur_nr_states=nr_states1;
		cur_count=0;

		assert((int)cur_image->height==H && (int)cur_image->width==W);

		//Sending tasks to SPUs - TIGER MODEL
		distribute_tasks(compare_strips_task_producer);
		dlog(LOG_NOTICE,"Comparison of test image %d with TIGER model is complete: (T:%d-%.4lf).",i,cur_count,cur_count/(float)nr_states1_not_delete);
		tiger_count=cur_count;

		//Sending tasks to SPUs - ELEPHANT MODEL
		dlog(LOG_INFO,"----------------- Comparing with ELEHPANT MODEL ---------------");
		cur_state=-1;
		cur_states=states2;
		cur_nr_states=nr_states2;
		cur_count=0;

		distribute_tasks(compare_strips_task_producer);
		dlog(LOG_NOTICE,"Comparison of test image %d with ELEPHANT model is complete: (T:%d-%.4lf).",i,cur_count,cur_count/(float)nr_states2_not_delete);
		elephant_count=cur_count;

		//Deciding the type of image
	    int is_tiger = ((tiger_count / (float) nr_states1_not_delete) > TIGER_PERC);
	    int is_eleph = ((elephant_count / (float) nr_states2_not_delete) > ELEPH_PERC);

		if (is_tiger && !is_eleph) {
			dlog(LOG_CRIT,"Image \'%s\' classified as \'%s\' [1]", cur_image->name,dir1);
			printf("Clasificat imaginea \'%s\' ca fiind \'%s\' ", cur_image->name,dir1);
		} else
		if (is_eleph && !is_tiger) {
			dlog(LOG_CRIT,"Image \'%s\' classified as \'%s\' [1]", cur_image->name,dir2);
			printf("Clasificat imaginea \'%s\' ca fiind \'%s\' ", cur_image->name,dir2);
		} else
		if (!is_tiger && !is_eleph) {
			dlog(LOG_CRIT,"Image \'%s\' classified as None", cur_image->name);
			printf("Clasificat imaginea \'%s\' ca fiind \'%s\' ", cur_image->name,"None");
		} else
		if (is_tiger && is_eleph)
		{
			if ((tiger_count / (float) nr_states1_not_delete) >
					(elephant_count / (float) nr_states2_not_delete))
			{
				dlog(LOG_CRIT,"Image \'%s\' classified as \'%s\' [2]", cur_image->name,dir1);
				printf("Clasificat imaginea \'%s\' ca fiind \'%s\' ", cur_image->name,dir1);
			}
			else
			{
				dlog(LOG_CRIT,"Image \'%s\' classified as \'%s\' [2]", cur_image->name,dir2);
				printf("Clasificat imaginea \'%s\' ca fiind \'%s\' ", cur_image->name,dir2);
			}
		}
		time_cur=clock();
		printf("in %2.8lf secunde.\n",(double) (time_cur - time_prev) / CLOCKS_PER_SEC);
	}

}

int main(int argc, char **argv)
{
	//Init parameters
	int i;

	/********************* INIT ****************************/
	time_start=clock();
	sprintf(unitName,"PPU  ");
	//Check run parameters. Example ./{exec} tigru elefant 10 -l20 -p10 -c test 2
	if(argc!=9)
	{
		eprintf("Numar incorect de parametrii. Folosire: %s {clasa_1} {clasa_2} {nr_img_training}"
				" -l{l_size} -p{p_size} -c {folder_clasificare} {nr_img_clasif}\n",argv[0]);
		exit(EXIT_FAILURE);
	}
	dir1=argv[1];
	dir2=argv[2];
	nrImagesTraining=atoi(argv[3]);

	assert(argv[4][1]=='l');
	L=atoi(argv[4]+2);

	assert(argv[5][1]=='p');
	P=atoi(argv[5]+2);

	assert(argv[6][1]=='c');
	dirC=argv[7];
	nrImagesClasify=atoi(argv[8]);
	dlog(LOG_INFO,"INIT complete. \n\tTraining folders: \'%s\' \'%s\', with %d images\n\tL=%d\tP=%d\t"
			"Clasification folder: \'%s\' with %d images.",dir1,dir2,nrImagesTraining,L,P,dirC,nrImagesClasify);

	//Reading images
	dlog(LOG_DEBUG,"Reading images from %s folder.",dir1);
	images1=read_images(dir1,nrImagesTraining);
	dlog(LOG_DEBUG,"Reading images from %s folder.",dir2);
	images2=read_images(dir2,nrImagesTraining);
	dlog(LOG_INFO,"Training images read. Reading test images from %s folder...",dirC);
	test_images=read_images(dirC,nrImagesClasify);
	dlog(LOG_WARNING,"All images have been read.");

    H=images1[0]->height;	//we consider all the images to have the same dimensions (equal to first image)
    W=images1[0]->width;

	//Init SPU threads
	init_spus();
	dlog(LOG_INFO,"Initialized SPU threads.");

	/*********************** TASK 1 **************************/
	dlog(LOG_CRIT,"\n\n/****************** TASK 1 *******************\\\n");
	time_prev=clock();
	histogram_equalization();
	time_cur=clock();
	printf("Terminat preprocesare in %2.8lf secunde.\n",(double) (time_cur - time_prev) / CLOCKS_PER_SEC);


	/*********************** TASK 2 **************************/
	dlog(LOG_CRIT,"\n\n/****************** TASK 2 *******************\\\n");
	dlog(LOG_WARNING,"<------------- HMM for TIGER ------------->");
	time_prev=clock();
	build_hmm(HMM_MODE_TIGER);
	time_cur=clock();
	printf("Terminat constructie model 1 in %2.8lf secunde.\n",(double) (time_cur - time_prev) / CLOCKS_PER_SEC);

	dlog(LOG_WARNING,"<------------- HMM for ELEPHANT ------------->");
	time_prev=clock();
	build_hmm(HMM_MODE_ELEPHANT);
	time_cur=clock();
	printf("Terminat constructie model 2 in %2.8lf secunde.\n",(double) (time_cur - time_prev) / CLOCKS_PER_SEC);

	/*********************** TASK 3 **************************/
	dlog(LOG_CRIT,"\n\n/****************** TASK 3 *******************\\\n");
	analyze_img();

	/********************** CLEANUP **************************/
	dlog(LOG_CRIT,"\n\n/***************** CLEANUP ******************\\\n");
	dlog(LOG_INFO,"Starting clean-up.");
	cleanup_spus();

	//Clean-up HMM
	for(i=0;i<nr_states1;i++)
	{
		free(states1[i].stddev);
		free(states1[i].average);
	}
	free(states1);

	//Cleanup images
	for(i=0;i<nrImagesTraining;i++)
	{
		free_img(images1[i]);
		free_img(images2[i]);
	}
	free(images1);
	free(images2);
	dlog(LOG_DEBUG,"Clean-up complete.");

	time_cur=clock();
	printf("Terminat executie in %2.8lf secunde.\n",(double) (time_cur - time_start) / CLOCKS_PER_SEC);

	return EXIT_SUCCESS;
}

