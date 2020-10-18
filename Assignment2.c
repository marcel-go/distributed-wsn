/* Gets the neighbors in a cartesian communicator
* Orginally written by Mary Thomas
* - Updated Mar, 2015
* Link: https://edoras.sdsu.edu/~mthomas/sp17.605/lectures/MPI-Cart-Comms-and-Topos.pdf
* Modifications to fix bugs, include an async send and receive and to revise print output
*/
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

void *baseStationListener(void *arg);
void *groundSensorListener(void *arg);
void *groundSensorReading(void *arg);

int main(int argc, char *argv[]) {

	int ndims=2, size, my_rank, reorder, my_cart_rank, ierr;
	int nrows, ncols;
	int nbr_i_lo, nbr_i_hi;
	int nbr_j_lo, nbr_j_hi;
	MPI_Comm comm2D;
	int dims[ndims],coord[ndims];
	int wrap_around[ndims];
	int base_station_rank;
	
	/* start up initial MPI environment */
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
	
	base_station_rank = size-1;
	
	
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
	
	
	if (my_rank != base_station_rank) {
	    // Ground sensor code
	    
	    /* find my coordinates in the cartesian communicator group */
	    MPI_Cart_coords(comm2D, my_rank, ndims, coord); // coordinated is returned into the coord array
	    /* use my cartesian coordinates to find my rank in cartesian group*/
	    MPI_Cart_rank(comm2D, coord, &my_cart_rank);
	    
	    // Find neighbors' ranks
	    MPI_Cart_shift( comm2D, SHIFT_ROW, DISP, &nbr_i_lo, &nbr_i_hi );
	    MPI_Cart_shift( comm2D, SHIFT_COL, DISP, &nbr_j_lo, &nbr_j_hi );
	    
	    MPI_Request send_request[4], receive_request[4], req;
        MPI_Status send_status[4], receive_status[4];
        
	    sleep(my_rank);
	    unsigned int seed = time(NULL);
	    int randomVal = rand_r(&seed) % 100 + 1;
	    MPI_Isend(&randomVal, 1, MPI_INT, base_station_rank, 0, MPI_COMM_WORLD, &req);
	    MPI_Isend(&randomVal, 1, MPI_INT, nbr_i_lo, 0, comm2D, &send_request[0]);
	    MPI_Isend(&randomVal, 1, MPI_INT, nbr_i_hi, 0, comm2D, &send_request[1]);
	    MPI_Isend(&randomVal, 1, MPI_INT, nbr_j_lo, 0, comm2D, &send_request[2]);
	    MPI_Isend(&randomVal, 1, MPI_INT, nbr_j_hi, 0, comm2D, &send_request[3]);
	    
	    
	    int recvValL = -1, recvValR = -1, recvValT = -1, recvValB = -1;
	    MPI_Irecv(&recvValT, 1, MPI_INT, nbr_i_lo, 0, comm2D, &receive_request[0]);
	    MPI_Irecv(&recvValB, 1, MPI_INT, nbr_i_hi, 0, comm2D, &receive_request[1]);
	    MPI_Irecv(&recvValL, 1, MPI_INT, nbr_j_lo, 0, comm2D, &receive_request[2]);
	    MPI_Irecv(&recvValR, 1, MPI_INT, nbr_j_hi, 0, comm2D, &receive_request[3]);
	    
	    
	    MPI_Waitall(4, send_request, send_status);
	    MPI_Waitall(4, receive_request, receive_status);


	    printf("Global rank: %d. Cart rank: %d. Coord: (%d, %d). Random Val: %d. Recv Top: %d. Recv Bottom: %d. Recv Left: %d. Recv Right: %d.\n", my_rank, my_cart_rank, coord[0], coord[1], randomVal, recvValT, recvValB, recvValL, recvValR);
	    
	    
	    MPI_Comm_free( &comm2D );
	} else {
	    // Base station code (Last rank)
	    
	    pthread_t tid[2];
	    pthread_create(&tid[0], NULL, baseStationListener, &size);
	    pthread_join(tid[0], NULL);
	    
    }
	
	MPI_Finalize();
	return 0;
}


void *baseStationListener(void *arg) {
    int *size = arg;
    printf("Listener initiated\n");
    fflush(stdout);
    
    int i = *size - 1;
    while (i > 0) {
        MPI_Request recv_request;
        MPI_Status recv_status;
        int val;
        
        MPI_Irecv(&val, 1, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &recv_request);
        MPI_Wait(&recv_request, &recv_status);
        
        printf("Received %d from rank %d\n", val, recv_status.MPI_SOURCE);
        fflush(stdout);
        
        i -= 1;
    }
    
    return NULL;
}

void *groundSensorListener(void *arg) {
    
    return NULL;
}

void *groundSensorReading(void *arg) {
    
    return NULL;
}

