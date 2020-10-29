#pragma once

#include <time.h>
#include <mpi.h>

typedef struct report {
	time_t time;
	time_t sec;
	long nsec;
	int temp;
	int msg;
	int adjacentRanks[4];
	int adjacentTemps[4];
} Report;

typedef struct sharedBaseStation {
	int *satelliteTemp;
	time_t *satelliteTime;
	int terminateFlag;
	int size;
} SharedBaseStation;