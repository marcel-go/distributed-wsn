#include "basestation.h"

#define REQUEST 0
#define REPLY 1
#define TERMINATE 2
#define THRESHOLD 80
#define TOLERANCE 5

extern MPI_Datatype ReportType;

int baseStation(MPI_Comm worldComm, MPI_Comm comm, int nIntervals, int nRows, int nCols) {
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
    int interval = 50;
	int iteration = 0, numAlerts = 0, numTrueAlerts = 0;
    int flag = 0; // false
    MPI_Status status;

	FILE *fptr;
	fptr = fopen("base-station-log.txt", "w");
    
    while (shared->terminateFlag == 0) {
        msleep(interval);
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
					rankToCoord(recv.adjacentRanks[j], coord, nCols);
				}
				coords[j][0] = coord[0];
				coords[j][1] = coord[1];
			}

			/* Get reporting node coordinate */
			int reportingCoord[2];
			rankToCoord(status.MPI_SOURCE, reportingCoord, nCols);
			
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

