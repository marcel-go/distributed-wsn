#include <string.h>
#include <mpi.h>
#include "sharedstruct.h"
#include "utilities.h"
#include "sensor.h"
#include "basestation.h"
#include "satellite.h"

/* Initialise global variable */
MPI_Datatype ReportType;

int main(int argc, char *argv[]) {

	int i, myRank, size, nIntervals, provided, nRows, nCols;
	// int nRows, nCols;
	MPI_Comm newComm;
	
	/* Start up initial MPI environment */
	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &myRank);

	/* Create custom MPI datatype for reports */
	int blocksCount = 7;
	int blocksLen[7] = {1, 1, 1, 1, 1, 4, 4};
	MPI_Datatype types[7] = {MPI_DOUBLE, MPI_DOUBLE, MPI_LONG, MPI_INT, MPI_INT, MPI_INT, MPI_INT};
	MPI_Aint offsets[7];
	
	offsets[0] = offsetof(Report, time);
	offsets[1] = offsetof(Report, sec);
	offsets[2] = offsetof(Report, nsec);
	offsets[3] = offsetof(Report, temp);
	offsets[4] = offsetof(Report, msg);
	offsets[5] = offsetof(Report, adjacentRanks);
	offsets[6] = offsetof(Report, adjacentTemps);
	
	MPI_Type_create_struct(blocksCount, blocksLen, offsets, types, &ReportType);
	MPI_Type_commit(&ReportType);
	
	/* Process command line arguments*/
	if (argc == 4) {
		nRows = atoi(argv[1]);
		nCols = atoi(argv[2]);
		nIntervals = atoi(argv[3]);
		
		if(nRows * nCols + 1 != size) {
			if (myRank == 0) printf("ERROR: (nRows*nCols)= %d * %d + 1 = %d != %d\n", nRows, nCols, nRows * nCols + 1, size);
			MPI_Finalize(); 
			return 0;
		}
	} else {
		if (myRank == 0) printf("ERROR: Wrong arguments\n");
		MPI_Finalize(); 
		return 0;
	}
	
	/* Split processes into master and slaves, base station as the master and sensor nodes as slave */
	MPI_Comm_split(MPI_COMM_WORLD, myRank == size-1, 0, &newComm);
	if (myRank == size-1) {
		baseStation(MPI_COMM_WORLD, newComm, nIntervals, nRows, nCols);
	} else {
		sensorNode(MPI_COMM_WORLD, newComm, nRows, nCols);
	}

    MPI_Type_free(&ReportType);

	MPI_Finalize();
	return 0;
}
