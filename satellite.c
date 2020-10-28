#include "satellite.h"

void *infraredSatellite(void *arg) {
	SharedBaseStation *shared = (SharedBaseStation*)arg;
	int size = shared->size;

	int interval = 50;
    int rand_node;
    time_t now;

	msleep(size);
	
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	srand((time_t)ts.tv_nsec);

    /* Generate random row, col and temperature */
    while (shared->terminateFlag == 0) {
		msleep(interval);

		int temp = rand() % 20 + 80;
        now = time(NULL);
		rand_node = rand() % (size-1);

        shared->satelliteTemp[rand_node] = temp;
        shared->satelliteTime[rand_node] = now;
    }
    
	return NULL;
}