#include "sensor.h"

#define SHIFT_ROW 0
#define SHIFT_COL 1
#define DISP 1
#define REQUEST 0
#define REPLY 1
#define TERMINATE 2
#define THRESHOLD 80
#define TOLERANCE 5
#define INTERVAL 50
#define MAX_IP 16
#define MAX_MAC 18

extern MPI_Datatype ReportType;

int sensorNode(MPI_Comm worldComm, MPI_Comm comm, int nRows, int nCols) {
	/* Ground sensor code */
	int ndims = 2, myRank, myCartRank, size, worldSize, reorder = 0, ierr = 0, valid = 0, i;
	int coord[ndims], dims[ndims], wrapAround[ndims];
	int adjacent[4], reply[4];
	MPI_Comm comm2D;

	MPI_Comm_size(worldComm, &worldSize);
	MPI_Comm_size(comm, &size);
	MPI_Comm_rank(comm, &myRank);

	char bufferIP[MAX_IP], bufferMac[MAX_MAC];
	getIP(bufferIP);
	getMac(bufferMac);
	// printf("IP Address: %s\nMac Address: %s\n", bufferIP, bufferMac);

	// Send IP address and Mac adress of the sensor node to base station
	MPI_Request sendRequest[2];
	MPI_Status sendStatus[2];
	MPI_Isend(bufferIP, MAX_IP, MPI_CHAR, worldSize-1, 3, worldComm, &sendRequest[0]);
	MPI_Isend(bufferMac, MAX_MAC, MPI_CHAR, worldSize-1, 4, worldComm, &sendRequest[1]);

	/* Create cartesian topology for processes */
	dims[0] = nRows;
	dims[1] = nCols;
	MPI_Dims_create(size, ndims, dims);
	wrapAround[0] = 0;
	wrapAround[1] = 0;
	ierr = MPI_Cart_create(comm, ndims, dims, wrapAround, reorder, &comm2D);
	if(ierr != 0) printf("ERROR[%d] creating CART\n", ierr);

	MPI_Cart_coords(comm2D, myRank, ndims, coord); // Find coordinate
	MPI_Cart_rank(comm2D, coord, &myCartRank); // Find rank
	MPI_Cart_shift(comm2D, SHIFT_ROW, DISP, &adjacent[0], &adjacent[1]); // Find adjacent top and bottom
	MPI_Cart_shift(comm2D, SHIFT_COL, DISP, &adjacent[2], &adjacent[3]); // Find adjacent left and right

	for (i = 0; i < 4; i++) {
		// Count the number of valid adjacent nodes for the current node
		if (adjacent[i] >= 0)
			valid += 1;
		// Initialise replies received to -1
		reply[i] = -1;
	}

	int terminateFlag = 0, temp = 0;

	msleep(myRank);

	struct timespec ts, start, end;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	srand((time_t)ts.tv_nsec);

	int tempRecv, requestMsg, replyMsg, replyCount;
	int waiting = 0;

	MPI_Request reqSendReply, reqSendReport, reqSendRequest[4];
	MPI_Status recvStatus, replyStatus;

	MPI_Waitall(2, sendRequest, sendStatus);

	while (terminateFlag == 0) {
		msleep(INTERVAL);

		MPI_Iprobe(MPI_ANY_SOURCE, TERMINATE, worldComm, &terminateFlag, MPI_STATUS_IGNORE);
		MPI_Iprobe(MPI_ANY_SOURCE, REQUEST, comm2D, &requestMsg, &recvStatus);
		MPI_Iprobe(MPI_ANY_SOURCE, REPLY, comm2D, &replyMsg, &replyStatus);

		/* Sensor generates reading */
		if (waiting == 0)
			temp = rand() % 10 + 85;

		// Send reply immediately to requesting node
		if (requestMsg == 1) {
			MPI_Recv(&tempRecv, 1, MPI_INT, recvStatus.MPI_SOURCE, REQUEST, comm2D, MPI_STATUS_IGNORE);
			MPI_Isend(&temp, 1, MPI_INT, recvStatus.MPI_SOURCE, REPLY, comm2D, &reqSendReply);
		}

		if (temp > THRESHOLD && waiting == 0) {

			// Send request to adjacent nodes for their temps
			for (i = 0; i < 4; i++)
				if (adjacent[i] >= 0)
					MPI_Isend(&temp, 1, MPI_INT, adjacent[i], REQUEST, comm2D, &reqSendRequest[i]);
			waiting = 1;
			replyCount = 0;
		}

		if (replyMsg == 1 && waiting == 1) {
			int c_coord[2], a_coord[2], pos;
			rankToCoord(myRank, c_coord, nCols);
			rankToCoord(replyStatus.MPI_SOURCE, a_coord, nCols);
			pos = coordToPos(c_coord, a_coord);

			MPI_Recv(&reply[pos], 1, MPI_INT, replyStatus.MPI_SOURCE, REPLY, comm2D, MPI_STATUS_IGNORE);

			replyCount++;

			if (replyCount == valid) {
				/* Compare temps received with its own temp*/
				int withinRangeCount = 0; // Counter for temps within range
				for (i = 0; i < 4; i++) {
					// Check the number of readings withing threshold
					if (reply[i] != -1) {
						if (reply[i] >= temp - TOLERANCE && reply[i] <= temp + TOLERANCE) { // Temp is within tolerance range
							withinRangeCount += 1;
						}
					}
				}

				/* An event is confirmed with adjacent nodes */
				if (withinRangeCount >= 2) {
					// Measure time since report is about to be sent
					clock_gettime(CLOCK_REALTIME, &start);

					/* Send a report to the base station */
					/* Construct report */
					Report send;
					send.time = time(NULL);
					send.sec = start.tv_sec;
					send.nsec = start.tv_nsec;
					send.temp = temp;
					send.msg = withinRangeCount;
					for (i = 0; i < 4; i++) {
						send.adjacentRanks[i] = adjacent[i];
						send.adjacentTemps[i] = reply[i];
					}

					/* Send message to base station on size worldSize-1 */
					MPI_Isend(&send, 1, ReportType, worldSize-1, REQUEST, worldComm, &reqSendReport);
				}

				// Reset replies received
				for (i = 0; i < 4; i++)
					reply[i] = -1;
				waiting = 0;
			}
		}
	}
	
	MPI_Comm_free(&comm2D);
}
