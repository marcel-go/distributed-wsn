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

void *infraredSatellite(void *arg);
void *sensorListener(void *arg);

void rankToCoord(int rank, int *coord);
void timeToString(time_t rawTime, char *buffer);
int coordToPos(int *coord1, int *coord2);

int baseStation(MPI_Comm worldComm, MPI_Comm comm, int nIntervals);
int sensorNode(MPI_Comm worldComm, MPI_Comm comm, int nRows, int nCols);

/* Initialise global variables */
int nRows, nCols; // <------------
MPI_Comm comm2D; // <-------------
MPI_Datatype ReportType;

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
} SharedNode;

typedef struct sharedBaseStation {
	int *satelliteTemp;
	time_t *satelliteTime;
	int terminateFlag;
	int size;
} SharedBaseStation;

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
		baseStation(MPI_COMM_WORLD, newComm, nIntervals);
	} else {
		sensorNode(MPI_COMM_WORLD, newComm, nRows, nCols);
	}

    MPI_Type_free(&ReportType);

	MPI_Finalize();
	return 0;
}

int baseStation(MPI_Comm worldComm, MPI_Comm comm, int nIntervals) {
	/* Base station code */

	int worldSize, myRank;
	MPI_Comm_rank(worldComm, &myRank);
	MPI_Comm_size(worldComm, &worldSize);

	/* Construct shared variables for base station and infrared satellite */
	SharedBaseStation *shared = malloc(sizeof(SharedBaseStation));
	/* Allocate satellite datas array and initialise the array*/
    shared->satelliteTemp = (int*)malloc(sizeof(int) * (worldSize - 1));
    shared->satelliteTime = (time_t*)malloc(sizeof(time_t) * (worldSize - 1));
	for (int i = 0; i < worldSize-1; i++) {
		shared->satelliteTemp[i] = THRESHOLD;
		shared->satelliteTime[i] = time(NULL);
	}
	shared->terminateFlag = 0;
	shared->size = worldSize;

	/* Create a separate thread for the infrared satellite */
	pthread_t tid;
	pthread_create(&tid, NULL, infraredSatellite, shared);

	/*-------------------- Base station listener code --------------------*/
    int interval = 1;
	int iteration = 0, numAlerts = 0, numTrueAlerts = 0;
    int flag = 0; // false
    MPI_Status status;

	FILE *fptr;
	fptr = fopen("base-station-log.txt", "w");
    
    while (shared->terminateFlag == 0) {
        sleep(interval);
        MPI_Iprobe(MPI_ANY_SOURCE, REQUEST, worldComm, &flag, &status);
        /* If a report is received from a node */
        if (flag == 1) {
            Report recv;
            MPI_Recv(&recv, 1, ReportType, status.MPI_SOURCE, REQUEST, worldComm, &status);
			printf("Base station: Report received from node %d\n", status.MPI_SOURCE);
			numAlerts += 1;
            
            /* Compare with infrared satellite readings */
			int falseAlert = 1; // flag that indicates if the report does not match satellite readings
            int nodeSatTemp = shared->satelliteTemp[status.MPI_SOURCE]; // satellite temp reading for reporting node
            if (recv.temp >= nodeSatTemp - TOLERANCE && recv.temp <= nodeSatTemp + TOLERANCE) {
				falseAlert = 0;
				numTrueAlerts += 1;
			}
                
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
			timeToString(shared->satelliteTime[status.MPI_SOURCE], bufferSat);

			fprintf(fptr, "------------------------------------------------\n");
			fprintf(fptr, "Iteration: %d\n", iteration);
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

			flag = 0;
        }
        
        if (iteration == nIntervals) {
            shared->terminateFlag = 1;
            int i;
            MPI_Request terminateRequest[worldSize-1]; 
            MPI_Status terminateStatus[worldSize-1]; 

            /* Send termination messages to sensor nodes*/
            for (i = 0; i < worldSize-1; i++) {
                MPI_Isend(&(shared->terminateFlag), 1, MPI_INT, i, TERMINATE, worldComm, &terminateRequest[i]);
            }   
            MPI_Waitall(worldSize-1, terminateRequest, terminateStatus);

			char buffer[80];
			timeToString(time(NULL), buffer);

			/* Write report summary */
			FILE *pOutfile;
			pOutfile = fopen("base-station-summary.txt", "w");
			
			fprintf(pOutfile, "Terminated on %s\n", buffer);
			fprintf(pOutfile, "Number of true alerts: %d\n", numTrueAlerts);
			fprintf(pOutfile, "Number of false alerts: %d", numAlerts - numTrueAlerts);

			fflush(pOutfile);
			fclose(pOutfile);
        }

        iteration++;
    }

	fclose(fptr);

	free(shared->satelliteTemp);
	free(shared->satelliteTime);
	pthread_join(tid, NULL);
}

void *infraredSatellite(void *arg) {
	SharedBaseStation *shared = (SharedBaseStation*)arg;
	int size = shared->size;

    int rand_node;
    time_t now;

    /* Generate random row, col and temperature */
    while (shared->terminateFlag == 0) {
		sleep(1);

		unsigned int seed = time(NULL);
		int temp = rand_r(&seed) % 20 + 80;
        now = time(NULL);
		rand_node = rand_r(&seed) % (size-1);

        shared->satelliteTemp[rand_node] = temp;
        shared->satelliteTime[rand_node] = now;
    }
    
	return NULL;
}

int sensorNode(MPI_Comm worldComm, MPI_Comm comm, int nRows, int nCols) {
	/* Ground sensor code */
	
	int ndims = 2, myRank, myCartRank, size, worldSize, reorder = 0, ierr = 0, valid = 0;
	int coord[ndims], dims[ndims], wrapAround[ndims];
	int adjacent[4], reply[4];

	MPI_Comm_size(worldComm, &worldSize);
	MPI_Comm_size(comm, &size);
	MPI_Comm_rank(comm, &myRank);

	/* create cartesian topology for processes */
	dims[0] = nRows;
	dims[1] = nCols;
	MPI_Dims_create(size, ndims, dims);
	/* create cartesian mapping */
	wrapAround[0] = 0;
	wrapAround[1] = 0;
	ierr = MPI_Cart_create(comm, ndims, dims, wrapAround, reorder, &comm2D);
	if(ierr != 0) printf("ERROR[%d] creating CART\n", ierr);
	/* Find coordinate and rank in the cartersian topology */
	MPI_Cart_coords(comm2D, myRank, ndims, coord);
	MPI_Cart_rank(comm2D, coord, &myCartRank);
	/* Find adjacent ranks */
	MPI_Cart_shift(comm2D, SHIFT_ROW, DISP, &adjacent[0], &adjacent[1]);
	MPI_Cart_shift(comm2D, SHIFT_COL, DISP, &adjacent[2], &adjacent[3]);

	for (int i = 0; i < 4; i++)
		if (adjacent[i] >= 0)
			valid += 1;

	/* Construct shared variables between threads in a node */
	SharedNode *shared = malloc(sizeof(SharedNode));
	shared->temp = 0;
	shared->terminateFlag = 0;
	shared->rank = myRank;

	pthread_t tid;
	pthread_create(&tid, NULL, sensorListener, shared);

	/* Sensor generates reading */
	int i, interval = 5;
	
	sleep(myRank);

	printf("Sensor %d: Initialise reading\n", myRank);
	fflush(stdout);

	while (shared->terminateFlag == 0) {
		MPI_Request sendRequest[4], recvRequest[4];
		MPI_Status sendStatus[4], recvStatus[4];

		// Initialise replies received to -1
		for (i = 0; i < 4; i++)
			reply[i] = -1;

		sleep(interval);

		unsigned int seed = time(NULL);
		shared->temp = rand_r(&seed) % 10 + 85;

		printf("Sensor %d: Generated %d\n", myRank, shared->temp);
		fflush(stdout);

		if (shared->temp > THRESHOLD) {

			// Send request to adjacent nodes for their temps
			for (i = 0; i < 4; i++)
				if (adjacent[i] >= 0)
					MPI_Isend(&(shared->temp), 1, MPI_INT, adjacent[i], REQUEST, comm2D, &sendRequest[i]);

			int replyCount = 0;
			int replyMsg, terminateMsg;
			MPI_Status replyStatus, recvStatus;
			while (replyCount < valid) {
				MPI_Iprobe(MPI_ANY_SOURCE, REPLY, comm2D, &replyMsg, &replyStatus);
				MPI_Iprobe(MPI_ANY_SOURCE, TERMINATE, worldComm, &terminateMsg, MPI_STATUS_IGNORE);
				if (replyMsg == 1) {
					int c_coord[2], a_coord[2], pos;
					/* Determine the position of the node sending the reply */
					rankToCoord(myRank, c_coord);
					rankToCoord(replyStatus.MPI_SOURCE, a_coord);
					pos = coordToPos(c_coord, a_coord);
					
					MPI_Recv(&reply[pos], 1, MPI_INT, replyStatus.MPI_SOURCE, REPLY, comm2D, MPI_STATUS_IGNORE);
					replyCount++;
				}
				if (terminateMsg == 1) {
					shared->terminateFlag = 1;
					break;
				}
			}

			// Wait for reply for adjacent nodes
			// for (i = 0; i < 4; i++)
			// 	MPI_Irecv(&reply[i], 1, MPI_INT, adjacent[i], REPLY, comm2D, &recvRequest[i]);
			
			// printf("Sensor %d: Waiting for replies\n", *rank);
			// fflush(stdout);
			
			// MPI_Waitall(4, sendRequest, sendStatus);
			
			// MPI_Waitall(4, recvRequest, recvStatus);
			
			printf("Sensor %d: Replies received, top: %d, bottom:%d, left:%d, right:%d\n", myRank, reply[0], reply[1], reply[2], reply[3]);
			fflush(stdout);
            
            /* Compare temps received with its own temp*/
            int withinRangeCount = 0; // Counter for temps within range
            int validRepliesCount = 0; // Counter for valid replies
            for (i = 0; i < 4; i++) {
                // Check if reply is valid
                if (reply[i] != -1) {
                    if (reply[i] >= shared->temp - TOLERANCE && reply[i] <= shared->temp + TOLERANCE) { // Temp is within tolerance range
                        withinRangeCount += 1;
                    }
                    validRepliesCount += 1;
                }
            }
			
            /* An event is confirmed with adjacent nodes */
            if (withinRangeCount >= 2) {
                /* Send a report to the base station */
                Report send;
                
                send.time = time(NULL);
                send.temp = shared->temp;
                send.msg = withinRangeCount;
                for (i = 0; i < 4; i++) {
                    send.adjacentRanks[i] = adjacent[i];
                    send.adjacentTemps[i] = reply[i];
                }
                /* Send message to base station on size worldSize-1 */
                MPI_Send(&send, 1, ReportType, worldSize-1, REQUEST, worldComm);

                printf("Sensor %d: Message sent to base station\n", myRank);
				fflush(stdout);
            }
		}
	}

	pthread_join(tid, NULL);
	MPI_Comm_free(&comm2D);
}

void *sensorListener(void *arg) {
	SharedNode *shared = (SharedNode*)arg;
	int rank = shared->rank;

	printf("Sensor %d: Initialise listener\n", rank);
	fflush(stdout);

    int tempRecv;

    MPI_Status stats;
    
	while(shared->terminateFlag == 0) {
		MPI_Iprobe(MPI_ANY_SOURCE, TERMINATE, MPI_COMM_WORLD, &(shared->terminateFlag), &stats);

		MPI_Request recvRequest;
		MPI_Status recvStatus;

		MPI_Irecv(&tempRecv, 1, MPI_INT, MPI_ANY_SOURCE, REQUEST, comm2D, &recvRequest);
		MPI_Wait(&recvRequest, &recvStatus);

		MPI_Send(&(shared->temp), 1, MPI_INT, recvStatus.MPI_SOURCE, REPLY, comm2D);

		printf("Sensor %d: Reply sent to sensor %d\n", rank, recvStatus.MPI_SOURCE);
		fflush(stdout);
		
	}
    
    return NULL;
}

void rankToCoord(int rank, int *coords) {
	/* Convert rank to coordinates */
	coords[0] = rank / nCols;
	coords[1] = rank % nCols;
}

void timeToString(time_t rawTime, char *buffer) {
	struct tm* info;
	info = localtime(&rawTime);
	strftime(buffer, 80, "%A %Y/%m/%d %H:%M:%S", info);
}

int coordToPos(int *coord1, int *coord2) {
	/* coord1 is center node, coord2 is the adjacent node */
	int rowDif, colDif, i;
	rowDif = coord1[0] - coord2[0];
	colDif = coord1[1] - coord2[1];
	if (rowDif > 0)
		i = 0; //top
	else if (rowDif < 0)
		i = 1; //bottom
	else if (colDif > 0)
		i = 2; //left
	else if (colDif < 0)
		i = 3; //right
	else
		i = -1; //same node
	return i;	
}
