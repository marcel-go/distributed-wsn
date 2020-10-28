#pragma once

#include <time.h>
#include <mpi.h>

typedef struct report {
	time_t time;
	int temp;
	int msg;
	int adjacentRanks[4];
	int adjacentTemps[4];
} Report;

typedef struct sharedNode {
	int temp;
	int terminateFlag;
	int rank; // temporary
	MPI_Comm comm;
} SharedNode;

typedef struct sharedBaseStation {
	int *satelliteTemp;
	time_t *satelliteTime;
	int terminateFlag;
	int size;
} SharedBaseStation;