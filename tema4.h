/*
 *  ASC - Tema3
 *  calsificator.h
 *
 *  
 *  Author: Stefan-Dobrin Cosmin
 *  		331CA, Faculty of Computer Science, Politehnica University of Bucharest
 *	Created on: Apr 26, 2011
 */


#ifndef CALSIFICATOR_H_
#define CALSIFICATOR_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

typedef unsigned long long 	uint64;
typedef unsigned int 		uint32;
typedef unsigned short 		uint16;
typedef	unsigned char		uint8;



#define TASK_1A_COUNT 			1
#define TASK_1B_EQUALIZATION 	2
#define TASK_2A_AVERAGE_CALC	3
#define TASK_3_COMPARE			4

#define TASK_DATA_EXIT			0


typedef struct {
	uint32 type;
	uint32 mainSource;
	uint32 size;
	uint32 destination;


	uint32 source1;
	uint32 source2;
	uint32 aux1;
	uint32 aux2;
} task_t;

/* For task 2 */
#define STATE_NOT_MERGEABLE	0
#define STATE_MERGE_1 		1
#define STATE_MERGE_2		2
#define HMM_MODE_TIGER 1
#define HMM_MODE_ELEPHANT 2
#define TEST_TIGER_STATE_THRESHOLD 0.33
#define TEST_ELEPH_STATE_THRESHOLD 0.37
#define TIGER_STATE_THRESHOLD 0.78
#define ELEPH_STATE_THRESHOLD 0.73
#define TIGER_PERC 0.21
#define ELEPH_PERC 0.21

#define STATE_SIMILAR		1
#define STATE_NOT_SIMILAR 	0

#define MIN(a,b) ((a) < (b) ? (a) : (b))

typedef struct state {
    unsigned int start_height, stop_height;
    unsigned char *average;
    unsigned char *stddev;
    int is_deleted; //used for state merging
} state_t;

#endif /* CALSIFICATOR_H_ */
