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


char unitName[20];
task_t task __attribute__((aligned(128)));

void compute_mean_task()
{
	pixel_t buffer[MAX_CHUNK_SIZE] __attribute__((aligned(128)));	//alocare statica ca e mai rapida si permite checking la compilare
	data_t mean[MAX_CHUNK_SIZE] __attribute__((aligned(128)));
	uint32 *slice_sources;
	uint32 tag;
	int cur_image=0;
	int i;
	int nr_images;
	int size;
	int fetchable_size;
	int start_pos;

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
	tag = mfc_tag_reserve();
	DIE(tag==MFC_TAG_INVALID,"Cannot alocate tag");

	//Get addresses of slices
	mfc_get(slice_sources, task.mainSource, CEIL_16(nr_images * sizeof(uint32)), tag,	0, 0);
	waitag(tag);
	dlog(LOG_CRAP,"\t\tSlice Addresses (%d bytes) in local store.",CEIL_16(nr_images * sizeof(uint32)));

	//Get the data and process it
	while(cur_image<nr_images)
	{
		mfc_get(buffer, slice_sources[cur_image]+start_pos*sizeof(pixel_t), fetchable_size*sizeof(pixel_t), tag, 0, 0);
		waitag(tag);

		/*calculate the sum of the pixel values*/
		for (i = 0; i < size; i++) {
			mean[i]+=buffer[i];
		}
		cur_image++;
	}
	dlog(LOG_DEBUG,"\t\tFinished computing sum of pixels.");

	//Compute the mean
	for (i = 0; i < size; i++)
	{
		mean[i]/=nr_images;
	}
	dlog(LOG_DEBUG,"\tComputation complete for mean.");

	//Put the data back into main memory
	mfc_put(mean, task.destination, CEIL_16(size * sizeof(data_t)), tag, 0, 0);
	waitag(tag);
	dlog(LOG_DEBUG,"\tMEAN TASK data sending is complete. Data is in main memory!");

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
