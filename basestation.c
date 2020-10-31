#include "basestation.h"

#define REQUEST 0
#define REPLY 1
#define TERMINATE 2
#define THRESHOLD 80
#define TOLERANCE 5
#define MAX_IP 16
#define MAX_MAC 18
#define INTERVAL 50

extern MPI_Datatype ReportType;

int baseStation(MPI_Comm worldComm, MPI_Comm comm, int nIntervals, int nRows, int nCols) {
	int worldSize, myRank, i;
	char **ip, **mac; // Array of string for IP and Mac
	struct timespec start, end;
	double totalCommTime = 0;

	clock_gettime(CLOCK_REALTIME, &start); // Get start time of program

	MPI_Comm_rank(worldComm, &myRank);
	MPI_Comm_size(worldComm, &worldSize);

	/* Allocate memory to store ip and mac addresses */
	ip = malloc(nRows*nCols * sizeof(char*));
	mac = malloc(nRows*nCols * sizeof(char*));
	for (i = 0; i < nRows*nCols; i++) {
		ip[i] = malloc(MAX_IP * sizeof(char));
		mac[i] = malloc(MAX_MAC * sizeof(char));
	}

	/* Construct shared variables for base station and infrared satellite */
	SharedBaseStation *shared = malloc(sizeof(SharedBaseStation));
	shared->terminateFlag = 0;
	shared->size = worldSize;

	/* Allocate satellite datas array and initialise the array*/
    shared->satelliteTemp = (int*)malloc(sizeof(int) * (worldSize - 1));
    shared->satelliteTime = (time_t*)malloc(sizeof(time_t) * (worldSize - 1));
	for (i = 0; i < worldSize-1; i++) {
		shared->satelliteTemp[i] = THRESHOLD;
		shared->satelliteTime[i] = time(NULL);
	}
	
	/* Create a separate thread for infrared satellite */
	pthread_t tid;
	pthread_create(&tid, NULL, infraredSatellite, shared);

	/* Base station listener code */
	int iteration = 1, numAlerts = 0, numTrueAlerts = 0; // counters
    int reportFlag = 0; // flag
    

	/* Create a log file for alerts */
	FILE *pAlertFile;
	pAlertFile = fopen("alerts.log", "w");

	/* Wait for IP and Mac sent by nodes */
	MPI_Request reqIP[nRows*nCols], reqMac[nRows*nCols];
	MPI_Status statIP[nRows*nCols], statMac[nRows*nCols];
	for (i = 0; i < nRows*nCols; i++) {
		MPI_Irecv(ip[i], MAX_IP, MPI_CHAR, i, 3, worldComm, &reqIP[i]);
		MPI_Irecv(mac[i], MAX_MAC, MPI_CHAR, i, 4, worldComm, &reqMac[i]);
	}
	MPI_Waitall(nRows*nCols, reqIP, statIP);
	MPI_Waitall(nRows*nCols, reqMac, statMac);
	
    while (shared->terminateFlag == 0) {
        msleep(INTERVAL);
		MPI_Status status;
        MPI_Iprobe(MPI_ANY_SOURCE, REQUEST, worldComm, &reportFlag, &status); // Listen for reports from sensors

        /* If a report is received from a node */
        if (reportFlag == 1) {
			int reportingNode = status.MPI_SOURCE;
            Report recv;

            MPI_Recv(&recv, 1, ReportType, reportingNode, REQUEST, worldComm, &status);
			numAlerts++; // Increment number of alerts

			// Calculate communication time between reporting node and base station
			clock_gettime(CLOCK_REALTIME, &end);
			double commTime = (end.tv_sec - recv.sec) * 1e9;
			commTime = (commTime + (end.tv_nsec - recv.nsec)) * 1e-9;
			totalCommTime += commTime;
			
            // Compare alert with infrared satellite readings
			int falseAlert = 1; // flag that indicate alert types
            int nodeSatTemp = shared->satelliteTemp[reportingNode]; // get satellite temp reading for reporting node
            if (recv.temp >= nodeSatTemp - TOLERANCE && recv.temp <= nodeSatTemp + TOLERANCE) {
				falseAlert = 0;
				numTrueAlerts += 1;
			}
                
			/* Get adjacent coordinates received into coords */
			int coords[4][2];
			for (i = 0; i < 4; i++) {
				int coord[2] = {-1, -1};
				if (recv.adjacentRanks[i] >= 0) {
					rankToCoord(recv.adjacentRanks[i], coord, nCols);
				}
				coords[i][0] = coord[0];
				coords[i][1] = coord[1];
			}

			/* Get reporting node coordinate */
			int reportingCoord[2];
			rankToCoord(reportingNode, reportingCoord, nCols);
			
			/* Format alert, log, and satellite times */
			char bufferSat[80], bufferLog[80], bufferAlert[80];
			timeToString(time(NULL), bufferLog);
			timeToString(recv.time, bufferAlert);
			timeToString(shared->satelliteTime[reportingNode], bufferSat);
			
			/* Log alert into a file */
			fprintf(pAlertFile, "------------------------------------------------\n");
			fprintf(pAlertFile, "Iteration: %d\n", iteration);
			fprintf(pAlertFile, "Logged time: %s\n", bufferLog);
			fprintf(pAlertFile, "Alert time: %s\n", bufferAlert);
			fprintf(pAlertFile, "Alert type: %s\n\n", (falseAlert == 1) ? "False" : "True");

			fprintf(pAlertFile, "Reporting Node\t\tCoord\tTemp\tIP\t\t\t\t\tMAC\n");
	    	fprintf(pAlertFile, "%d\t\t\t\t\t(%d,%d)\t%d\t\t%s\t\t\t%s\n\n",
				reportingNode, reportingCoord[0], reportingCoord[1], recv.temp,
				ip[reportingNode], mac[reportingNode]);
			fprintf(pAlertFile, "Adjacent Nodes Temp\tCoord\tTemp\tIP\t\t\t\t\tMAC\n");
			for (i = 0; i < 4; i++)
				if (recv.adjacentRanks[i] >= 0)
					fprintf(pAlertFile, "%d\t\t\t\t\t(%d,%d)\t%d\t\t%s\t\t\t%s\n",
						recv.adjacentRanks[i], coords[i][0], coords[i][1], recv.adjacentTemps[i],
						ip[recv.adjacentRanks[i]], mac[recv.adjacentRanks[i]]);

			fprintf(pAlertFile, "\nInfrared satellite reporting time: %s\n", bufferSat);
			fprintf(pAlertFile, "Infrared satellite reporting: %d\n", nodeSatTemp);
			fprintf(pAlertFile, "Infrared satellite reporting coord: (%d,%d)\n\n", reportingCoord[0], reportingCoord[1]);

			fprintf(pAlertFile, "Communication time: %f\n", commTime);
			fprintf(pAlertFile, "Total messages send between reporting node and base station: 1\n"); // Each alert sends 1 message between node and base station
			fprintf(pAlertFile, "Number of adjacent matches to reporting node: %d\n", recv.msg);
			
			fprintf(pAlertFile, "------------------------------------------------\n");

			fflush(pAlertFile);

			reportFlag = 0;
        }
        
		/* If num interval is reached */
        if (iteration == nIntervals) {
            shared->terminateFlag = 1;
            
            /* Send termination messages to all sensor nodes*/
			MPI_Request terminateRequest[worldSize-1]; 
            MPI_Status terminateStatus[worldSize-1]; 
            for (i = 0; i < worldSize-1; i++) {
                MPI_Isend(&(shared->terminateFlag), 1, MPI_INT, i, TERMINATE, worldComm, &terminateRequest[i]);
            }   
            MPI_Waitall(worldSize-1, terminateRequest, terminateStatus);

			/* Format program's start and end times */
			char bufferEnd[80], bufferStart[80];
			timeToString(start.tv_sec, bufferStart);
			timeToString(time(NULL), bufferEnd);

			/* Log report summary into a file */
			FILE *pSumFile;
			pSumFile = fopen("summary.log", "w");
			
			fprintf(pSumFile, "Program started on %s\n", bufferStart);
			fprintf(pSumFile, "Program terminated on %s\n", bufferEnd);
			fprintf(pSumFile, "Total messages sent from node to base station: %d\n", numAlerts + (worldSize-1)*2); 
			fprintf(pSumFile, "Number of true alerts: %d\n", numTrueAlerts);
			fprintf(pSumFile, "Number of false alerts: %d\n", numAlerts - numTrueAlerts);
			fprintf(pSumFile, "Total communication time (seconds): %f\n", totalCommTime);

			/* Print summary to console */
			printf("Program started on %s\n", bufferStart);
			printf("Program terminated on %s\n", bufferEnd);
			printf("Total messages sent from node to base station: %d\n", numAlerts + (worldSize-1)*2); 
			printf("Number of true alerts: %d\n", numTrueAlerts);
			printf("Number of false alerts: %d\n", numAlerts - numTrueAlerts);
			printf("Total communication time (seconds): %f\n", totalCommTime);
			fflush(stdout);

			fflush(pSumFile);
			fclose(pSumFile);
        }

        iteration++;
    }

	fclose(pAlertFile);

	free(shared->satelliteTemp);
	free(shared->satelliteTime);

	pthread_join(tid, NULL);
}
