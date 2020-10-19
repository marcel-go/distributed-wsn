#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <mpi.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#define SHIFT_ROW 0
#define SHIFT_COL 1
#define DISP 1
#define REQUEST 0
#define REPLY 1
#define THRESHOLD 80
#define TOLERANCE 5

void *baseStationListener(void *arg);
void *infraredSatellite(void *arg);
void *groundSensorListener(void *arg);
void *groundSensorReading(void *arg);

/* Initialise global variables */
int temp = 0; // Temperature reading for each node
int adjacent[4]; // 0: top, 1: bottom, 2: left, 3: right
int reply[4];
int terminate_flag = 0; // Termination flag
int nrows, ncols, size;
int *satellite_temp; // latest infrared satellite reading for each sensor node
int *satellite_time; // latest satellite scan time for each sensor node
MPI_Comm comm2D;
MPI_Datatype ReportType;

typedef struct Report {
	time_t time;
	int temp;
	int msg;
	int adjacentRanks[4];
	int adjacentTemps[4];
} report;

int main(int argc, char *argv[]) {

	int ndims=2, size, my_rank, reorder, my_cart_rank, ierr, provided;
	// int nrows, ncols;
	
	int dims[ndims],coord[ndims];
	int wrap_around[ndims];
	
	/* start up initial MPI environment */
	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

	/* Create custom MPI datatype for report */
	int blocksCount = 5;
	int blocksLen[5] = {1, 1, 1, 4, 4};
	MPI_Datatype types[5] = {MPI_DOUBLE, MPI_INT, MPI_INT, MPI_INT, MPI_INT};
	MPI_Aint offsets[5];
	
	offsets[0] = offsetof(report, time);
	offsets[1] = offsetof(report, temp);
	offsets[2] = offsetof(report, msg);
	offsets[3] = offsetof(report, adjacentRanks);
	offsets[4] = offsetof(report, adjacentTemps);
	
	MPI_Type_create_struct(blocksCount, blocksLen, offsets, types, &ReportType);
	MPI_Type_commit(&ReportType);
	
    /* Allocate satellite datas array */
    satellite_temp = (int*)malloc(sizeof(int) * (size - 1));
    satellite_time = (int*)malloc(sizeof(int) * (size - 1));
	
	/* process command line arguments*/
	if (argc == 3) {
		nrows = atoi (argv[1]);
		ncols = atoi (argv[2]);
		dims[0] = nrows; /* number of rows */
		dims[1] = ncols; /* number of columns */
		
		if( (nrows*ncols+1) != size) {
			if( my_rank ==0) printf("ERROR: (nrows*ncols)= %d * %d + 1 = %d != %d\n", nrows, ncols, nrows*ncols+1, size);
			MPI_Finalize(); 
			return 0;
		}

	} else {
		nrows=ncols=(int)sqrt(size);
		dims[0]=dims[1]=0;
	}
	
	/* create cartesian topology for processes */
	MPI_Dims_create(size - 1, ndims, dims);
	
	/* create cartesian mapping */
	wrap_around[0] = 0;
	wrap_around[1] = 0;
	reorder = 1;
	ierr =0;
	ierr = MPI_Cart_create(MPI_COMM_WORLD, ndims, dims, wrap_around, reorder, &comm2D);
	if(ierr != 0) printf("ERROR[%d] creating CART\n", ierr);
	
	
	if (my_rank != size-1) {
	    // Ground sensor code
	    
		/* find my coordinates in the cartesian communicator group */
	    MPI_Cart_coords(comm2D, my_rank, ndims, coord); // coordinated is returned into the coord array
	    /* use my cartesian coordinates to find my rank in cartesian group*/
	    MPI_Cart_rank(comm2D, coord, &my_cart_rank);

		// Find adjacent ranks
	    MPI_Cart_shift(comm2D, SHIFT_ROW, DISP, &adjacent[0], &adjacent[1]);
	    MPI_Cart_shift(comm2D, SHIFT_COL, DISP, &adjacent[2], &adjacent[3]);
		
		pthread_t tid[2];
		pthread_create(&tid[0], NULL, groundSensorListener, &my_rank);
		pthread_create(&tid[1], NULL, groundSensorReading, &my_rank);
		pthread_join(tid[0], NULL);
		pthread_join(tid[1], NULL);


		/*************************************************************************************/
	    // MPI_Request send_request[4], receive_request[4], req;
        // MPI_Status send_status[4], receive_status[4];
        
	    // sleep(my_rank);
	    // unsigned int seed = time(NULL);
	    // int randomVal = rand_r(&seed) % 100 + 1;

	    // MPI_Isend(&randomVal, 1, MPI_INT, base_station_rank, 0, MPI_COMM_WORLD, &req);
		// for (int i = 0; i < 4; i++) {
		// 	MPI_Isend(&randomVal, 1, MPI_INT, adjacent[i], 0, comm2D, &send_request[i]);
		// }
	    // int reply[4] = {-1, -1, -1, -1};
	    // int recvValL = -1, recvValR = -1, recvValT = -1, recvValB = -1;
		// for (int i = 0; i < 4; i++) {
		// 	MPI_Irecv(&reply[i], 1, MPI_INT, adjacent[i], 0, comm2D, &receive_request[i]);
		// }
	    
	    // MPI_Waitall(4, send_request, send_status);
	    // MPI_Waitall(4, receive_request, receive_status);

	    // printf("Global rank: %d. Cart rank: %d. Coord: (%d, %d). Random Val: %d. Recv Top: %d. Recv Bottom: %d. Recv Left: %d. Recv Right: %d.\n", my_rank, my_cart_rank, coord[0], coord[1], randomVal, reply[0], reply[1], reply[2], reply[3]);
		// printf("Adjacent , top: %d, bottom:%d, left:%d, right:%d\n", adjacent[0], adjacent[1], adjacent[2], adjacent[3]);
	    /*************************************************************************************/
		
	    MPI_Comm_free( &comm2D );
		
	} else {
	    /* Base station code (Last rank) */
	    
	    pthread_t tid[2];
	    pthread_create(&tid[0], NULL, baseStationListener, &size);
		pthread_create(&tid[1], NULL, infraredSatellite, &size);
	    pthread_join(tid[0], NULL);
		pthread_join(tid[1], NULL);
    }
	
    free(satellite_temp);
    free(satellite_time);
    
    MPI_Type_free(&ReportType);
	MPI_Finalize();
	
	return 0;
}


void *baseStationListener(void *arg) {
    /* Base station runs in 2 seconds interval iteration */
    
    int interval = 2;
    int intervalNum = 100;
    int i = 0;
    int flag = 0; // .false.
    MPI_Status status;
    
    while (terminate_flag == 0) {
        sleep(interval);
        MPI_Iprobe(MPI_ANY_SOURCE, REQUEST, MPI_COMM_WORLD, &flag, &status);
        /* If a report is received from a node */
        if (flag == 1) {
            report recv;
            MPI_Recv(&recv, 1, ReportType, status.MPI_SOURCE, REQUEST, MPI_COMM_WORLD, &status);
            
            time_t recv_time = time(NULL);
            
            /* Compare with infrared satellite readings */
            int cur_sat_temp = satellite_temp[status.MPI_SOURCE];
            if (recv.temp >= cur_sat_temp - TOLERANCE && recv.temp <= cur_sat_temp + TOLERANCE) {
                /* log the report into a file */
                
                /* Parse adjacent coordinates received */
                int coords[4][2];
                for (int j = 0; j < 4; j++) {
                    int cur_coord[2] = {-1, -1};
                    if (recv.adjacentRanks[j] >= 0) {
                        MPI_Cart_coords(comm2D, status.MPI_SOURCE, 2, cur_coord);
                    }
                    coords[j][0] = cur_coord[0];
                    coords[j][1] = cur_coord[1];
                }
                
                FILE *fptr;
                fptr = fopen("base-station-log.txt", "a");
                
                
                
                fclose(fptr);
                flag = 0;
            }
        }
        
        if (i == intervalNum) {
        /* Broadcast termination message */
        }
        
        i++;
    }
    
    return NULL;
}


void *infraredSatellite(void *arg) {
    int *size = arg;
    int rand_node;
    time_t now;

    /* Generate random row, col and temperature */
    while (terminate_flag == 0) {
		sleep(5);
		unsigned int seed = time(NULL);
		temp = rand_r(&seed) % 60 + 51;
        now = time(NULL);
		rand_node = rand_r(&seed) % (*size-1);

        satellite_temp[rand_node] = temp;
        satellite_time[rand_node] = now;
    }
    
	return NULL;
}


void *groundSensorListener(void *arg) {
	int *rank = arg;

	printf("Sensor %d: Initialise listener\n", *rank);
	fflush(stdout);

    int temp_recv;
    
	while(terminate_flag == 0) {
		MPI_Request send_request, recv_request;
		MPI_Status send_status, recv_status;

		MPI_Irecv(&temp_recv, 1, MPI_INT, MPI_ANY_SOURCE, REQUEST, comm2D, &recv_request);
		MPI_Wait(&recv_request, &recv_status);

		// printf("Sensor %d: Request received from sensor %d\n", *rank, recv_status.MPI_SOURCE);
		// fflush(stdout);

		MPI_Isend(&temp, 1, MPI_INT, recv_status.MPI_SOURCE, REPLY, comm2D, &send_request);
		MPI_Wait(&send_request, &send_status);

		printf("Sensor %d: Reply sent to sensor %d\n", *rank, recv_status.MPI_SOURCE);
		fflush(stdout);
		
	}
    
    return NULL;
}


void *groundSensorReading(void *arg) {
	int *rank = arg;
	int i;
	
	sleep(*rank);

	printf("Sensor %d: Initialise reading\n", *rank);
	fflush(stdout);

	while (terminate_flag == 0) {
		MPI_Request send_request[4], recv_request[4];
		MPI_Status send_status[4], recv_status[4];

		// Initialise replies received to -1
		for (i = 0; i < 4; i++)
			reply[i] = -1;

		sleep(5);
		unsigned int seed = time(NULL);
		temp = rand_r(&seed) % 60 + 40;

		printf("Sensor %d: Generated %d\n", *rank, temp);
		fflush(stdout);

		if (temp > THRESHOLD) {

			// Send request to adjacent nodes for their temps
			for (i = 0; i < 4; i++)
				MPI_Isend(&temp, 1, MPI_INT, adjacent[i], REQUEST, comm2D, &send_request[i]);
			// Wait for reply for adjacent nodes
			for (i = 0; i < 4; i++)
				MPI_Irecv(&reply[i], 1, MPI_INT, adjacent[i], REPLY, comm2D, &recv_request[i]);
			
			/*
			printf("Sensor %d: Waiting for replies\n", *rank);
			fflush(stdout);
			*/
			
			MPI_Waitall(4, send_request, send_status);
			MPI_Waitall(4, recv_request, recv_status);
			
			printf("Sensor %d: Replies received, top: %d, bottom:%d, left:%d, right:%d\n", *rank, reply[0], reply[1], reply[2], reply[3]);
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
                report send;
                
                send.time = time(NULL);
                send.temp = temp;
                send.msg = withinRangeCount;
                for (i = 0; i < 4; i++) {
                    send.adjacentRanks[i] = adjacent[i];
                    send.adjacentTemps[i] = reply[i];
                }
                /* Send message to base station on rank size-1 */
                MPI_Send(&send, 1, ReportType, size-1, REQUEST, MPI_COMM_WORLD);
                
            }
		}
	}

    return NULL;
}

