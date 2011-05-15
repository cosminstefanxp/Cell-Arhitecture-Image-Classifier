#include <spu_mfcio.h>
#include "../tema4.h"
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
