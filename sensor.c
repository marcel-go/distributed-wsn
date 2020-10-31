#include "sensor.h"

#define SHIFT_ROW 0
#define SHIFT_COL 1
#define DISP 1
#define REQUEST 0
#define REPLY 1
#define TERMINATE 2
#define THRESHOLD 80
#define TOLERANCE 5
#define INTERVAL 80
#define MAX_IP 16
#define MAX_MAC 18

extern MPI_Datatype ReportType;

int sensorNode(MPI_Comm worldComm, MPI_Comm comm, int nRows, int nCols) {
	int ndims = 2, myRank, myCartRank, size, worldSize, reorder = 0, ierr = 0, i;
	int coord[ndims], dims[ndims], wrapAround[ndims];
	int adjacent[4], reply[4];
	MPI_Comm comm2D; // Communicator within 2D cartesian grid
	
	/* Initialise MPI variables */
	MPI_Comm_size(worldComm, &worldSize);
	MPI_Comm_size(comm, &size);
	MPI_Comm_rank(comm, &myRank);

	/* Create cartesian topology for processes */
	dims[0] = nRows;
	dims[1] = nCols;
	MPI_Dims_create(size, ndims, dims);
	wrapAround[0] = 0;
	wrapAround[1] = 0;
	ierr = MPI_Cart_create(comm, ndims, dims, wrapAround, reorder, &comm2D);
	if (ierr != 0) printf("ERROR[%d] creating CART\n", ierr);

	MPI_Cart_coords(comm2D, myRank, ndims, coord); // Find coordinate
	MPI_Cart_rank(comm2D, coord, &myCartRank); // Find rank
	MPI_Cart_shift(comm2D, SHIFT_ROW, DISP, &adjacent[0], &adjacent[1]); // Find adjacent top and bottom
	MPI_Cart_shift(comm2D, SHIFT_COL, DISP, &adjacent[2], &adjacent[3]); // Find adjacent left and right
	
	int temp = 0; // Temperature of the given node
	int terminateFlag = 0; // Controls the main loop
	int waiting = 0; // .true. if node is waiting for replies
	int replyCount; // counter for replies received after sending requests
	int tempRecv, requestMsg, replyMsg; // Flags for MPI_Iprobe
	int valid = 0;
	char bufferIP[MAX_IP], bufferMac[MAX_MAC]; // Buffer to store IP and Mac addresses
	struct timespec seed, start;

	for (i = 0; i < 4; i++) {
		/* Count the number of valid adjacent nodes for the current node */
		if (adjacent[i] >= 0)
			valid += 1;
		reply[i] = -1; // Initialise replies received to -1
	}

	/* Sleep by rank and get time to seed the rand() function */
	msleep(myRank);
	clock_gettime(CLOCK_MONOTONIC, &seed);
	srand((time_t)seed.tv_nsec);

	MPI_Request reqSendReply, reqSendReport, reqSendRequest[4], reqIPMac[2];
	MPI_Status recvStatus, replyStatus;

	/*
	Send IP address and Mac adress of the sensor node to base station
	as a confirmation that the node is ready
	*/
	getIP(bufferIP);
	getMac(bufferMac);
	MPI_Isend(bufferIP, MAX_IP, MPI_CHAR, worldSize-1, 3, worldComm, &reqIPMac[0]);
	MPI_Isend(bufferMac, MAX_MAC, MPI_CHAR, worldSize-1, 4, worldComm, &reqIPMac[1]);
	MPI_Waitall(2, reqIPMac, MPI_STATUSES_IGNORE);

	/* Main loop for sensor node */
	while (terminateFlag == 0) {
		msleep(INTERVAL);

		MPI_Iprobe(MPI_ANY_SOURCE, TERMINATE, worldComm, &terminateFlag, MPI_STATUS_IGNORE);
		MPI_Iprobe(MPI_ANY_SOURCE, REQUEST, comm2D, &requestMsg, &recvStatus);
		MPI_Iprobe(MPI_ANY_SOURCE, REPLY, comm2D, &replyMsg, &replyStatus);

		/* Sensor generates a new reading if it is not waiting for replies */
		if (waiting == 0)
			temp = rand() % 40 + 60;

		/* Send reply immediately to requesting node */
		if (requestMsg == 1) {
			MPI_Recv(&tempRecv, 1, MPI_INT, recvStatus.MPI_SOURCE, REQUEST, comm2D, MPI_STATUS_IGNORE);
			MPI_Isend(&temp, 1, MPI_INT, recvStatus.MPI_SOURCE, REPLY, comm2D, &reqSendReply);
		}

		/* An event is detected */
		if (temp > THRESHOLD && waiting == 0) {
			/* Send request to adjacent nodes for temps */
			for (i = 0; i < 4; i++)
				if (adjacent[i] >= 0)
					MPI_Isend(&temp, 1, MPI_INT, adjacent[i], REQUEST, comm2D, &reqSendRequest[i]);
			waiting = 1; // Set waiting flag to 1
			replyCount = 0; // Reset reply count to 0
		}

		/* Sensor receives a reply */
		if (replyMsg == 1 && waiting == 1) {
			/* Get position of replying node */
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
					/* Check the number of readings withing threshold */
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
