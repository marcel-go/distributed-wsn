#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <mpi.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#define SHIFT_ROW 0
#define SHIFT_COL 1
#define DISP 1
#define REQUEST 0
#define REPLY 1
#define TERMINATE 2
#define THRESHOLD 80
#define TOLERANCE 5

void *baseStationListener(void *arg);
void *infraredSatellite(void *arg);
void *groundSensorListener(void *arg);
void *groundSensorReading(void *arg);
void rankToCoord(int rank, int *coord);
void timeToString(time_t rawTime, char *buffer);

int masterIO(MPI_Comm worldComm, MPI_Comm comm, int nIntervals);
int slaveIO(MPI_Comm worldComm, MPI_Comm comm, int nRows, int nCols);

/* Initialise global variables */
int temp = 0; // Temperature reading for each node
int adjacent[4]; // <------------
int reply[4]; // <---------------
int terminateFlag = 0; // Termination flag
int nRows, nCols; // <------------
int *satelliteTemp; // latest infrared satellite reading for each sensor node
time_t *satelliteTime; // latest satellite scan time for each sensor node
MPI_Comm comm2D; // <-------------
MPI_Datatype ReportType;

typedef struct report {
	time_t time;
	int temp;
	int msg;
	int adjacentRanks[4];
	int adjacentTemps[4];
} Report;

typedef struct threadInfo {
	int rank;
	int size;
	MPI_Comm comm;
} ThreadInfo;

int main(int argc, char *argv[]) {

	int i, myRank, size, nIntervals, provided;
	// int nRows, nCols;
	MPI_Comm newComm;
	
	/* Start up initial MPI environment */
	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &myRank);

	/* Create custom MPI datatype for reports */
	int blocksCount = 5;
	int blocksLen[5] = {1, 1, 1, 4, 4};
	MPI_Datatype types[5] = {MPI_DOUBLE, MPI_INT, MPI_INT, MPI_INT, MPI_INT};
	MPI_Aint offsets[5];
	
	offsets[0] = offsetof(Report, time);
	offsets[1] = offsetof(Report, temp);
	offsets[2] = offsetof(Report, msg);
	offsets[3] = offsetof(Report, adjacentRanks);
	offsets[4] = offsetof(Report, adjacentTemps);
	
	MPI_Type_create_struct(blocksCount, blocksLen, offsets, types, &ReportType);
	MPI_Type_commit(&ReportType);
	
    /* Allocate satellite datas array and initialise the array*/
    satelliteTemp = (int*)malloc(sizeof(int) * (size - 1));
    satelliteTime = (time_t*)malloc(sizeof(time_t) * (size - 1));
	for (i = 0; i < size-1; i++) {
		satelliteTemp[i] = THRESHOLD;
		satelliteTime[i] = time(NULL);
	}
	
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
		printf("ERROR: Wrong arguments\n");
		MPI_Finalize(); 
		return 0;
	}
	
	/* Split processes into master and slaves, base station as the master and sensor nodes as slave */
	MPI_Comm_split(MPI_COMM_WORLD, myRank == size-1, 0, &newComm);
	if (myRank == size-1) {
		masterIO(MPI_COMM_WORLD, newComm, nIntervals);
	} else {
		slaveIO(MPI_COMM_WORLD, newComm, nRows, nCols);
	}
	
    free(satelliteTemp);
    free(satelliteTime);
    MPI_Type_free(&ReportType);

	MPI_Finalize();
	return 0;
}

int masterIO(MPI_Comm worldComm, MPI_Comm comm, int nIntervals) {
	/* Base station code */

	int worldSize, myRank;

	MPI_Comm_rank(worldComm, &myRank);
	MPI_Comm_size(worldComm, &worldSize);

	/* Construct arguments for thread */
	ThreadInfo threadArgs;
	threadArgs.rank = myRank;
	threadArgs.size = worldSize;

	pthread_t tid[2];
	pthread_create(&tid[0], NULL, baseStationListener, &threadArgs);
	pthread_create(&tid[1], NULL, infraredSatellite, &threadArgs);
	pthread_join(tid[0], NULL);
	pthread_join(tid[1], NULL);
}

int slaveIO(MPI_Comm worldComm, MPI_Comm comm, int nRows, int nCols) {
	/* Ground sensor code */
	
	int ndims = 2, myRank, myCartRank, size, worldSize, reorder, ierr;
	int coord[ndims], dims[ndims], wrapAround[ndims];

	// int adjacent[4];

	//MPI_Comm comm2D;

	MPI_Comm_size(worldComm, &worldSize);
	MPI_Comm_size(comm, &size);
	MPI_Comm_rank(comm, &myRank);

	dims[0] = nRows;
	dims[1] = nCols;
	/* create cartesian topology for processes */
	MPI_Dims_create(size, ndims, dims);
	
	/* create cartesian mapping */
	wrapAround[0] = 0;
	wrapAround[1] = 0;
	reorder = 0;
	ierr =0;
	ierr = MPI_Cart_create(comm, ndims, dims, wrapAround, reorder, &comm2D);
	if(ierr != 0) printf("ERROR[%d] creating CART\n", ierr);
	    
	MPI_Cart_coords(comm2D, myRank, ndims, coord);
	MPI_Cart_rank(comm2D, coord, &myCartRank);

	/* Find adjacent ranks */
	MPI_Cart_shift(comm2D, SHIFT_ROW, DISP, &adjacent[0], &adjacent[1]);
	MPI_Cart_shift(comm2D, SHIFT_COL, DISP, &adjacent[2], &adjacent[3]);

	/* Construct arguments for thread */
	ThreadInfo threadArgs;
	threadArgs.rank = myRank;
	threadArgs.size = worldSize;
	
	pthread_t tid[2];
	pthread_create(&tid[0], NULL, groundSensorListener, &threadArgs);
	pthread_create(&tid[1], NULL, groundSensorReading, &threadArgs);
	pthread_join(tid[0], NULL);
	pthread_join(tid[1], NULL);

	MPI_Comm_free( &comm2D );
}


void *baseStationListener(void *arg) {
	ThreadInfo *threadArgs = (ThreadInfo*)arg;
	int size = threadArgs->size;
	int rank = threadArgs->rank;
    /* Base station runs in 2 seconds interval iteration */
    
    int interval = 2; // seconds for each iteration
    int intervalNum = 10000000;
    int i = 0;
    int flag = 0; // false
    MPI_Status status;
    
    while (terminateFlag == 0) {
        sleep(interval);
        MPI_Iprobe(MPI_ANY_SOURCE, REQUEST, MPI_COMM_WORLD, &flag, &status);
        /* If a report is received from a node */
        if (flag == 1) {
            Report recv;
            MPI_Recv(&recv, 1, ReportType, status.MPI_SOURCE, REQUEST, MPI_COMM_WORLD, &status);
			printf("Base station: Report received from node %d\n", status.MPI_SOURCE);
            
            /* Compare with infrared satellite readings */
			int falseAlert = 1; // flag that indicates if the report does not match satellite readings
            int nodeSatTemp = satelliteTemp[status.MPI_SOURCE]; // satellite temp reading for reporting node
            if (recv.temp >= nodeSatTemp - TOLERANCE && recv.temp <= nodeSatTemp + TOLERANCE)
                falseAlert = 0;
            
			/* Log the report into a file */
			/* Get adjacent coordinates received */
			int coords[4][2];
			for (int j = 0; j < 4; j++) {
				int coord[2] = {-1, -1};
				if (recv.adjacentRanks[j] >= 0) {
					rankToCoord(recv.adjacentRanks[j], coord);
				}
				coords[j][0] = coord[0];
				coords[j][1] = coord[1];
			}

			/* Get reporting node coordinate */
			int reportingCoord[2];
			rankToCoord(status.MPI_SOURCE, reportingCoord);
			
			/* Format time of alert, log, and satellite */
			char bufferSat[80], bufferLog[80], bufferAlert[80];
			timeToString(time(NULL), bufferLog);
			timeToString(recv.time, bufferAlert);
			timeToString(satelliteTime[status.MPI_SOURCE], bufferSat);
			
			FILE *fptr;
			fptr = fopen("base-station-log.txt", "a");

			fprintf(fptr, "------------------------------------------------\n");
			fprintf(fptr, "Iteration: %d\n", i);
			fprintf(fptr, "Logged time: %s\n", bufferLog);
			fprintf(fptr, "Alert time: %s\n", bufferAlert);
			fprintf(fptr, "Alert type: %s\n\n", (falseAlert == 1) ? "False" : "True");

			fprintf(fptr, "Reporting Node\t\tCoord\tTemp\n");
	    	fprintf(fptr, "%d\t\t\t\t\t(%d,%d)\t%d\n\n", status.MPI_SOURCE, reportingCoord[0], reportingCoord[1], recv.temp);
			fprintf(fptr, "Adjacent Nodes Temp\tCoord\tTemp\n");
			int i;
			for (i = 0; i < 4; i++)
				if (recv.adjacentRanks[i] >= 0)
					fprintf(fptr, "%d\t\t\t\t\t(%d,%d)\t%d\n", recv.adjacentRanks[i], coords[i][0], coords[i][1], recv.adjacentTemps[i]);

			fprintf(fptr, "\nInfrared satellite reporting time: %s\n", bufferSat);
			fprintf(fptr, "Infrared satellite reporting: %d\n", nodeSatTemp);
			fprintf(fptr, "Infrared satellite reporting coord: (%d,%d)\n\n", reportingCoord[0], reportingCoord[1]);

			fprintf(fptr, "Number of adjacent matches to reporting node: %d\n", recv.msg);
			fprintf(fptr, "------------------------------------------------\n");

			fflush(fptr);
			fclose(fptr);
			flag = 0;
        }
        
        if (i == intervalNum) {
            terminateFlag = 1;
            int i;
            MPI_Request terminate_requests[size-1]; 
            MPI_Request terminate_statuses[size-1]; 

            /* Send termination messages to sensor nodes*/
            // for (i = 0; i < size-1; i++) {
                // MPI_Isend(&terminate_flag, 1, MPI_INT, i, TERMINATE, MPI_COMM_WORLD, terminate_requests[i]);
            // }   
            // MPI_Waitall((size-1, terminate_requests, terminate_statuses);
            printf("Base Station: terminate signals sent\n");
            fflush(stdout);

			/* Write report summary */
        }
        i++;
    }
    
    return NULL;
}


void *infraredSatellite(void *arg) {
    ThreadInfo *threadArgs = (ThreadInfo*)arg;
	int size = threadArgs->size;

    int rand_node;
    time_t now;

    /* Generate random row, col and temperature */
    while (terminateFlag == 0) {
		sleep(1);

		unsigned int seed = time(NULL);
		temp = rand_r(&seed) % 20 + 80;
        now = time(NULL);
		rand_node = rand_r(&seed) % (size-1);

        satelliteTemp[rand_node] = temp;
        satelliteTime[rand_node] = now;
    }
    
	return NULL;
}


void *groundSensorListener(void *arg) {
	ThreadInfo *threadArgs = (ThreadInfo*)arg;
	int rank = threadArgs->rank;

	printf("Sensor %d: Initialise listener\n", rank);
	fflush(stdout);

    int tempRecv;

    MPI_Status stats;
    MPI_Iprobe(MPI_ANY_SOURCE, TERMINATE, MPI_COMM_WORLD, &terminateFlag, &stats);

	while(terminateFlag == 0) {
		MPI_Request sendRequest, recvRequest;
		MPI_Status sendStatus, recvStatus;

		MPI_Irecv(&tempRecv, 1, MPI_INT, MPI_ANY_SOURCE, REQUEST, comm2D, &recvRequest);
		MPI_Wait(&recvRequest, &recvStatus);

		// printf("Sensor %d: Request received from sensor %d\n", *rank, recvStatus.MPI_SOURCE);
		// fflush(stdout);

		MPI_Isend(&temp, 1, MPI_INT, recvStatus.MPI_SOURCE, REPLY, comm2D, &sendRequest);
		MPI_Wait(&sendRequest, &sendStatus);

		printf("Sensor %d: Reply sent to sensor %d\n", rank, recvStatus.MPI_SOURCE);
		fflush(stdout);
		
	}
    
    return NULL;
}


void *groundSensorReading(void *arg) {
	ThreadInfo *threadArgs = (ThreadInfo*)arg;
	int rank = threadArgs->rank;
	int size = threadArgs->size;

	int i;
	
	sleep(rank);

	printf("Sensor %d: Initialise reading\n", rank);
	fflush(stdout);

	while (terminateFlag == 0) {
		MPI_Request sendRequest[4], recvRequest[4];
		MPI_Status sendStatus[4], recvStatus[4];

		// Initialise replies received to -1
		for (i = 0; i < 4; i++)
			reply[i] = -1;

		sleep(5);
		unsigned int seed = time(NULL);
		temp = rand_r(&seed) % 20 + 80;

		printf("Sensor %d: Generated %d\n", rank, temp);
		fflush(stdout);

		if (temp > THRESHOLD) {

			// Send request to adjacent nodes for their temps
			for (i = 0; i < 4; i++)
				MPI_Isend(&temp, 1, MPI_INT, adjacent[i], REQUEST, comm2D, &sendRequest[i]);
			// Wait for reply for adjacent nodes
			for (i = 0; i < 4; i++)
				MPI_Irecv(&reply[i], 1, MPI_INT, adjacent[i], REPLY, comm2D, &recvRequest[i]);
			
			// printf("Sensor %d: Waiting for replies\n", *rank);
			// fflush(stdout);
			
			MPI_Waitall(4, sendRequest, sendStatus);
			MPI_Waitall(4, recvRequest, recvStatus);
			
			printf("Sensor %d: Replies received, top: %d, bottom:%d, left:%d, right:%d\n", rank, reply[0], reply[1], reply[2], reply[3]);
			fflush(stdout);
            
            /* Compare temps received with its own temp*/
            int withinRangeCount = 0; // Counter for temps within range
            int validRepliesCount = 0; // Counter for valid replies
            for (i = 0; i < 4; i++) {
                // Check if reply is valid
                if (reply[i] != -1) {
                    if (reply[i] >= temp - TOLERANCE && reply[i] <= temp + TOLERANCE) { // Temp is within tolerance range
                        withinRangeCount += 1;
                    }
                    validRepliesCount += 1;
                }
            }
			
            /* An event is confirmed with adjacent nodes */
            if (withinRangeCount == validRepliesCount) {
                /* Send a report to the base station */
                Report send;
                
                send.time = time(NULL);
                send.temp = temp;
                send.msg = withinRangeCount;
                for (i = 0; i < 4; i++) {
                    send.adjacentRanks[i] = adjacent[i];
                    send.adjacentTemps[i] = reply[i];
                }
                /* Send message to base station on size size-1 */
                MPI_Send(&send, 1, ReportType, size-1, REQUEST, MPI_COMM_WORLD);
                
            }
		}
	}

    return NULL;
}


void rankToCoord(int rank, int *coords) {
	/* Convert rank to coordinates */
	coords[0] = rank / nCols;
	coords[1] = rank % nCols;
}

