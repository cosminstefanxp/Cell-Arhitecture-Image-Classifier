/*
 *  ASC - Tema4
 *  tema4.h
 *
 *  
 *  Author: Stefan-Dobrin Cosmin
 *  		331CA, Faculty of Computer Science, Politehnica University of Bucharest
 *	Created on: May 16, 2011
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

typedef float				data_t;



#define TASK_MEAN 			1
#define TASK_SWS			2
#define TASK_ADD			3
#define TASK_MUL			4


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


#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define CEIL_16(value)   ((value +  0x0F) &  0xFFFFFFF0)
#define FLOOR_16(value)   ((value) &  0xFFFFFFF0)

#endif /* CALSIFICATOR_H_ */
