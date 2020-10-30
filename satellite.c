#include "satellite.h"

#define INTERVAL 50

void *infraredSatellite(void *arg) {
	SharedBaseStation *shared = (SharedBaseStation*)arg;
	int size = shared->size;

    int rand_node;
    time_t now;

	msleep(size);
	
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	srand((time_t)ts.tv_nsec);

    /* Generate random row, col and temperature */
    while (shared->terminateFlag == 0) {
		msleep(INTERVAL);

		int temp = rand() % 40 + 60;
        now = time(NULL);
		rand_node = rand() % (size-1);

        shared->satelliteTemp[rand_node] = temp;
        shared->satelliteTime[rand_node] = now;
    }
    
	return NULL;
}