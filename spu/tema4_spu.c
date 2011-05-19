#include <spu_mfcio.h>
#include "../tema4.h"
#include "../util.h"
#include "../imglib.h"
//de pe cell:  ssh cosmin.stefan-dobrin@172.16.6.3


// Macro ce asteapta finalizarea grupului de comenzi DMA cu acelasi tag
// 1. scrie masca de tag
// 2. citeste statusul - blocant pana la finalizareac comenzilor
#define waitag(t) mfc_write_tag_mask(1<<t); mfc_read_tag_status_all();
#define MAX_CHUNK_SIZE 16384
#define MAX_CHUNK_SIZE_DATA_T 4096
#define LINES_ON_MUL_CHUNK


char unitName[20];
task_t task __attribute__((aligned(128)));

void compute_mean_task()
{
	pixel_t buffer[2][MAX_CHUNK_SIZE] __attribute__((aligned(128)));	//alocare statica ca e mai rapida si permite checking la compilare
	data_t mean[MAX_CHUNK_SIZE] __attribute__((aligned(128)));
	uint32 *slice_sources;
	uint32 tag[2];
	int cur_image=0;
	int i;
	int nr_images;
	int size;
	int fetchable_size;
	int start_pos;
	int cur_buf=0;
	int nxt_buf=1;

	//Initialization
	size=task.size;
	fetchable_size=task.aux1;
	start_pos=task.aux2;
	nr_images=task.source1;

	dlog(LOG_INFO,"Received a new MEAN TASK with data of size %d (fetchable %d) and %d images, starting at %d.",size,fetchable_size,nr_images,start_pos);
	assert(size<16384);
	assert(fetchable_size % 16==0);

	slice_sources = (uint32*) memalign(128, CEIL_16(nr_images * sizeof(uint32)));
	DIE(slice_sources==NULL,"Cannot alocate memory");

	memset(mean,0,MAX_CHUNK_SIZE*sizeof(data_t));

	//Alocare tag
	tag[0] = mfc_tag_reserve();
	DIE(tag[0]==MFC_TAG_INVALID,"Cannot alocate tag");

	tag[1] = mfc_tag_reserve();
	DIE(tag[1]==MFC_TAG_INVALID,"Cannot alocate tag");

	//Get addresses of slices
	mfc_getb(slice_sources, task.mainSource, CEIL_16(nr_images * sizeof(uint32)), tag[0],	0, 0);
	waitag(tag[0]);
	dlog(LOG_CRAP,"\t\tSlice Addresses (%d bytes) in local store.",CEIL_16(nr_images * sizeof(uint32)));

	//Request first block of data
	mfc_getb(buffer[0], slice_sources[cur_image]+start_pos*sizeof(pixel_t), fetchable_size*sizeof(pixel_t), tag[0], 0, 0);

	//Get the data and process it
	while(cur_image<nr_images)
	{
		//Request next block of data, if any
		if(cur_image+1<nr_images)
		{
			mfc_getb(buffer[nxt_buf], slice_sources[cur_image+1]+start_pos*sizeof(pixel_t), fetchable_size*sizeof(pixel_t), tag[nxt_buf], 0, 0);
		}
		//Wait for current block of data
		waitag(tag[cur_buf]);

		//compute the sum of the pixel values
		for (i = 0; i < size; i++) {
			mean[i]+=buffer[cur_buf][i];
		}

		//actualize the management variables
		cur_image++;
		cur_buf=nxt_buf;
		nxt_buf=(cur_buf+1)%2;
	}
	dlog(LOG_DEBUG,"\t\tFinished computing sum of pixels.");

	//Compute the mean
	for (i = 0; i < size; i++)
	{
		mean[i]/=nr_images;
	}
	dlog(LOG_DEBUG,"\tComputation complete for mean.");

	//Put the data back into main memory
	mfc_put(mean, task.destination, CEIL_16(size * sizeof(data_t)), tag[0], 0, 0);
	waitag(tag[0]);
	dlog(LOG_DEBUG,"\tMEAN TASK data sending is complete. Data is in main memory!");

	//Cleanup
	free(slice_sources);
	mfc_tag_release(tag[0]);
	mfc_tag_release(tag[1]);
}

void compute_SW_task()
{
	pixel_t buffer[MAX_CHUNK_SIZE] __attribute__((aligned(128)));	//alocare statica ca e mai rapida si permite checking la compilare
	data_t mean_diff[MAX_CHUNK_SIZE_DATA_T] __attribute__((aligned(128)));
	data_t result[MAX_CHUNK_SIZE_DATA_T] __attribute__((aligned(128)));

	uint32 tag;
	uint32 dest;
	int i,j;
	int M;

	//Initialization
	M=task.size;
	assert(M<=4096);	//at most 64x64 images to permit transfer in one DMA get/put
	dest=task.destination;

	dlog(LOG_INFO,"Received a new COMPUTE SW TASK with data of size %d.",M);

	//Alocare tag
	tag = mfc_tag_reserve();
	DIE(tag==MFC_TAG_INVALID,"Cannot alocate tag");

	//Get the data
	mfc_get(buffer, task.mainSource, CEIL_16(M * sizeof(pixel_t)), tag,	0, 0);
	mfc_get(mean_diff, task.source1, CEIL_16(M * sizeof(data_t)), tag,	0, 0);
	waitag(tag);
	dlog(LOG_CRAP,"\t\tData for task is in Local Storage.");

	//Calculate (mean-buffer) stored in mean_diff array
	for(i=0;i<M;i++)
		mean_diff[i]=buffer[i]-mean_diff[i];
	dlog(LOG_CRAP,"\t\tComputed mean-buffer array.");

	//We process one line at a time, so we can fit in a DMA transfer
	//the result array is actually line 'i' from the big SW matrix
	for(i=0;i<M;i++)
	{
		//dlog(LOG_CRAP,"Working on line %d.",i);
		for(j=0;j<M;j++)
			result[j]=mean_diff[i]*mean_diff[j];	//no more translation cause it's a vector
		//dlog(LOG_CRAP,"Work done. Sending %d bytes to %u.",CEIL_16(M*sizeof(data_t)),dest);
		mfc_put(result,dest,CEIL_16(M*sizeof(data_t)),tag,0,0);
		waitag(tag);
		dest+=M*sizeof(data_t);

	}
	dlog(LOG_DEBUG,"\tCOMPUTE SW TASK task is complete and data is in main memory!");

	//Cleanup
	mfc_tag_release(tag);
}

void compute_addition_task()
{
	data_t buffer[MAX_CHUNK_SIZE] __attribute__((aligned(128)));	//alocare statica ca e mai rapida si permite checking la compilare
	data_t result[MAX_CHUNK_SIZE] __attribute__((aligned(128)));
	uint32 *slice_sources;
	uint32 tag;
	int size;
	int nr_matrixes;
	int cur_matrix,i;



	//Initialization
	size=task.size;
	cur_matrix=0;
	nr_matrixes=task.source1;

	//dlog(LOG_WARNING,"Received a new ADD TASK with data of size %d and %d matrixes, starting at %d.",size,nr_matrixes,task.aux1);
	assert(size*sizeof(data_t)<16384);

	slice_sources = (uint32*) memalign(128, CEIL_16(nr_matrixes * sizeof(uint32)));
	DIE(slice_sources==NULL,"Cannot alocate memory");

	memset(result,0,MAX_CHUNK_SIZE*sizeof(data_t));

	//Alocare tag
	tag = mfc_tag_reserve();
	DIE(tag==MFC_TAG_INVALID,"Cannot alocate tag");

	//Get addresses of slices
	mfc_getb(slice_sources, task.mainSource, CEIL_16(nr_matrixes * sizeof(uint32)), tag,	0, 0);
	waitag(tag);
	dlog(LOG_CRAP,"\t\tSlice Addresses (%d bytes) in local store.",CEIL_16(nr_matrixes * sizeof(uint32)));

	//Compute the addition
	while(cur_matrix<nr_matrixes)
	{
		//Get the data
		mfc_getb(buffer, slice_sources[cur_matrix]+task.aux1*sizeof(data_t), size*sizeof(data_t), tag, 0, 0);

		for(i=0;i<size;i++)
			result[i]+=buffer[i];

		cur_matrix++;
	}
	dlog(LOG_DEBUG,"\t\tComputation complete. Sending data to main memory...");

	//Put the data back in main memory
	mfc_put(result, task.destination+task.aux1*sizeof(data_t), size * sizeof(data_t), tag, 0, 0);
	waitag(tag);
	dlog(LOG_DEBUG,"\tADD TASK data sending is complete. Data is in main memory!");

	//Cleanup
	free(slice_sources);
	mfc_tag_release(tag);
}

int main(unsigned long long speid, unsigned long long cellID, unsigned long long noThreads)
{
	sprintf(unitName,"SPU %llu",cellID);
	dlog(LOG_INFO,"Started. I have the id %llu. There are %llu SPUs working along with me.",speid,noThreads);
	
	uint32 dataIN;
	uint32 tagIN;

	//Alocare tag
	tagIN=mfc_tag_reserve();
	DIE(tagIN==MFC_TAG_INVALID,"Cannot alocate tag");

	while(1)
	{
		//Read task address
		while (spu_stat_in_mbox()==0);
		dataIN = spu_read_in_mbox();

		//Check for exit
		if(dataIN==TASK_DATA_EXIT)
			break;

		//Read task
		mfc_get(&task, dataIN, sizeof(task),tagIN,0,0);
		waitag(tagIN);
		dlog(LOG_INFO,"Received new task of type %u, with main source: %u and data of size: %d",task.type,task.mainSource,task.size);

		switch(task.type)
		{
		case TASK_MEAN: compute_mean_task(); break;
		case TASK_SWS: compute_SW_task(); break;
		case TASK_ADD: compute_addition_task(); break;
		default: dlog(LOG_ALERT,"Unknown type of task!!!! Skipping..."); break;
		}

		//Write confirmation of finished job
		dlog(LOG_DEBUG,"Task finished. Sending confirmation");
		while(spu_stat_out_intr_mbox()==0);
		spu_write_out_intr_mbox(cellID);
	}
	dlog(LOG_INFO,"Exit signal received. Cleaning up...");

	//Cleanup
	mfc_tag_release(tagIN);
	return 0;
}
