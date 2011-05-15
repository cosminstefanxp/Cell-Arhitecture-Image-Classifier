#include <spu_mfcio.h>
#include "../clasificare.h"
#include "../util.h"
#include "../imglib.h"
//de pe cell:  ssh cosmin.stefan-dobrin@172.16.6.3


// Macro ce asteapta finalizarea grupului de comenzi DMA cu acelasi tag
// 1. scrie masca de tag
// 2. citeste statusul - blocant pana la finalizareac comenzilor
#define waitag(t) mfc_write_tag_mask(1<<t); mfc_read_tag_status_all();
#define COUNT_CHUNK_SIZE 16000

char unitName[20];
task_t task __attribute__((aligned(128)));


void count_task()
{
	uint32 counts[256] __attribute__((aligned(128)));
	pixel_t buffer[COUNT_CHUNK_SIZE] __attribute__((aligned(128)));
	int remaining;
	int chunk,i;
	uint32 tag;
	uint32 offset=0;

	//Initialization
	dlog(LOG_INFO,"Received a new COUNT TASK");
	memset(counts,0,256*sizeof(uint32));

	//Alocare tag
	tag=mfc_tag_reserve();
	DIE(tag==MFC_TAG_INVALID,"Cannot alocate tag");

	//Get chunks and process them
	remaining=task.size;
	while(remaining>0)
	{
		//Calculate offset
		chunk=remaining>COUNT_CHUNK_SIZE?COUNT_CHUNK_SIZE:remaining;
		dlog(LOG_DEBUG,"\tGetting chunk of size %d from offset %u",chunk,offset);

		//Read data
		mfc_get(&buffer,task.mainSource + offset, chunk,tag,0,0);
		waitag(tag);

		//Process
		dlog(LOG_DEBUG,"\tReceived data. Processing offset %u...",offset);
		for(i=0;i<chunk;i++)
			counts[buffer[i]]++;

		offset+=chunk;
		remaining-=chunk;
	}
	dlog(LOG_INFO,"Count Task FINISHED! Sending results to PPU.");

	//Putting the result back in the main memory
	mfc_put(counts, task.destination, 256*sizeof(uint32), tag, 0, 0);
	waitag(tag);
	dlog(LOG_DEBUG,"\tCount data sending is complete. Data is in main memory!");

	mfc_tag_release(tag);
}

void equalization_task()
{
	uint32 counts[256] __attribute__((aligned(128)));
	pixel_t buffer[COUNT_CHUNK_SIZE] __attribute__((aligned(128)));
	int remaining;
	int chunk,i;
	uint32 tag;
	uint32 offset=0;
	int temp;

	//Initialization
	dlog(LOG_INFO,"Received a new EQUALIZATION TASK");

	//Alocare tag
	tag=mfc_tag_reserve();
	DIE(tag==MFC_TAG_INVALID,"Cannot alocate tag");

	//Get agregated task info
	mfc_get(&counts,task.source1,256*sizeof(uint32),tag,0,0);
	waitag(tag);

	//Get chunks and process them
	remaining=task.size;
	temp=task.aux1-counts[0];
	while(remaining>0)
	{
		//Calculate offset
		chunk=remaining>COUNT_CHUNK_SIZE?COUNT_CHUNK_SIZE:remaining;
		dlog(LOG_DEBUG,"\tGetting chunk of size %d from offset %u",chunk,offset);

		//Read data
		mfc_get(&buffer,task.mainSource + offset, chunk*sizeof(pixel_t),tag,0,0);
		waitag(tag);

		//Process
		dlog(LOG_DEBUG,"\t\tReceived data. Processing offset %u...",offset);
		for(i=0;i<chunk;i++)
			buffer[i]=((counts[buffer[i]] - counts[0]) / (float) (temp)) * 255;

		//Putting the result back in the main memory
		dlog(LOG_DEBUG,"\t\tProcessing offset %u finished. Sending data in main memory...",offset);
		mfc_put(buffer, task.destination+offset, chunk*sizeof(pixel_t), tag, 0, 0);
		waitag(tag);

		offset+=chunk;
		remaining-=chunk;
	}
	dlog(LOG_INFO,"Equalization Task FINISHED!");

	mfc_tag_release(tag);
}

void compute_average_stddev_task()
{
	pixel_t **buffer;
	uint32 *slice_sources;
	unsigned char *average;
	unsigned char *stddev;
	uint32 tag;
	int i,j;

	//Initialization
	dlog(LOG_INFO,"Received a new AVERAGE/STDDEV TASK with data of size %u and %u images.",task.size,task.source1);
	assert(task.size<16384);
	assert(task.size * task.source1<220000); //task.source1 is number of images

	buffer=(pixel_t**)memalign(128,task.source1*sizeof(pixel_t*));
	for(i=0;i<(int)task.source1;i++)
	{
		buffer[i]=(pixel_t*)memalign(128,task.size*sizeof(pixel_t));
		DIE(buffer[i]==NULL,"Cannot alocate memory");
	}

	average=(unsigned char*)memalign(128,task.size*sizeof(unsigned char));
	DIE(average==NULL,"Cannot alocate memory");
	memset(average,0,task.size);
	stddev=(unsigned char*)memalign(128,task.size*sizeof(unsigned char));
	DIE(stddev==NULL,"Cannot alocate memory");
	memset(stddev,0,task.size);
	slice_sources=(uint32*)memalign(128,task.source1*sizeof(uint32));
	DIE(stddev==NULL,"Cannot alocate memory");
	dlog(LOG_CRAP,"\t\tDone memory allocation");

	//Alocare tag
	tag=mfc_tag_reserve();
	DIE(tag==MFC_TAG_INVALID,"Cannot alocate tag");

	//Get data
	mfc_get(slice_sources,task.mainSource, task.source1*sizeof(uint32),tag,0,0);
	waitag(tag);
	dlog(LOG_CRAP,"\t\tSlice Addresses in local store.");
	for(i=0;i<(int)task.source1;i++)
		mfc_get(buffer[i],slice_sources[i], task.size,tag,0,0);
	waitag(tag);
	dlog(LOG_DEBUG,"\t\tAll input data is in local store.");

	/*compute the average pixel values*/
	float temp_avg=0;
	for(i=0;i<(int)task.size;i++)
	{
		temp_avg=0;
		for(j=0;j<(int)task.source1;j++)
			temp_avg += (float) buffer[j][i];
		temp_avg /= (int)task.source1;
		average[i]=(unsigned int)temp_avg;
		//dlog(LOG_CRAP,"%d -> %d(%f)",i,average[i],temp_avg);
	}

	/*compute the standard deviation values*/
	float temp_stddev,temp1;
	for(i = 0; i < (int)task.size; i++)
	{
		temp_stddev = 0;
	    for(j=0;j<(int)task.source1;j++)
	    {
	    	temp1 = (float)buffer[j][i]-average[i];
	    	temp1 *= temp1;
	    	temp_stddev +=temp1;
	    }

	    temp_stddev /= (int)task.source1;
	    temp_stddev = sqrt(temp_stddev);
	    stddev[i]=(unsigned char) temp_stddev;
    }
	dlog(LOG_DEBUG,"\tComputation complete for average and standard deviation.");

	//Put the data back into main memory
	mfc_put(average, task.aux1, task.size*sizeof(unsigned char), tag, 0, 0);
	mfc_put(stddev, task.aux2, task.size*sizeof(unsigned char), tag, 0, 0);
	waitag(tag);
	dlog(LOG_DEBUG,"\tAverage/StdDev data sending is complete. Data is in main memory!");

	//Cleanup
	free(average);
	free(stddev);
	free(slice_sources);
	for(i=0;i<(int)task.source1;i++)
		free(buffer[i]);
	free(buffer);

	mfc_tag_release(tag);
}

void compare_strips_task(uint32 task_source)
{
	unsigned char average[COUNT_CHUNK_SIZE] __attribute__((aligned(128)));
	unsigned char stddev[COUNT_CHUNK_SIZE] __attribute__((aligned(128)));
	pixel_t buffer[COUNT_CHUNK_SIZE] __attribute__((aligned(128)));
	uint32 tag;
	int i;
	int m1,sd1,m2;

	//Initialization
	dlog(LOG_INFO,"Received a new COMPARE STRIPS task.");
	assert(task.size<16384);

	//Rezerve tag
	tag=mfc_tag_reserve();
	DIE(tag==MFC_TAG_INVALID,"Cannot alocate tag");

	//Get needed data
	mfc_get(average,task.aux1, task.size*sizeof(unsigned char),tag,0,0);
	mfc_get(stddev,task.aux2, task.size*sizeof(unsigned char),tag,0,0);
	mfc_get(buffer,task.mainSource, task.size*sizeof(pixel_t),tag,0,0);
	waitag(tag);
	dlog(LOG_DEBUG,"\t\tAll input data is in local store.");

    /*count the number of pixels that follow the mean and standard deviation*/
    int count = 0;
    for (i = 0; i < (int)task.size; i++)
    {
            m1 = (int) average[i];
            sd1 = (int) stddev[i];
            m2 = buffer[i];
            if ((m1 - sd1 < m2) && (m2 < m1 + sd1))
                count++;
    }

    /* if the count is over the threshold, the number of states that are part of the tiger increase
     * we send the result back to the main memory via the task, in the destination field.*/
    if (count / (float) (task.size) > TEST_TIGER_STATE_THRESHOLD)
    {
    	dlog(LOG_CRAP,"\tDecided state is SIMILAR with model.");
    	task.destination=STATE_SIMILAR;
    }
    else
    {
    	dlog(LOG_CRAP,"\tDecided state is NOT SIMILAR with model.");
    	task.destination=STATE_NOT_SIMILAR;
    }

    //Putting the task structure back in main memory, to send back the result
	mfc_put(&task, task_source, sizeof(task_t), tag, 0, 0);
	waitag(tag);

	//Finish-up
	dlog(LOG_INFO,"Compare Task FINISHED!");
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
		case TASK_1A_COUNT: count_task(); break;
		case TASK_1B_EQUALIZATION: equalization_task(); break;
		case TASK_2A_AVERAGE_CALC: compute_average_stddev_task(); break;
		case TASK_3_COMPARE: compare_strips_task(dataIN); break;
		default: dlog(LOG_WARNING,"Unknown type of task!!!! Skipping..."); break;
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
