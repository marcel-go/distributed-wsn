#include "satellite.h"

#define INTERVAL 50

void *infraredSatellite(void *arg) {
	SharedBaseStation *shared = (SharedBaseStation*)arg;
	int size = shared->size;

	/* Sleep by size to seed the rand() function */
	msleep(size);
	struct timespec seed;
	clock_gettime(CLOCK_MONOTONIC, &seed);
	srand((time_t)seed.tv_nsec);

    /* Generate a random temperature for a random node */
    while (shared->terminateFlag == 0) {
		msleep(INTERVAL);

		int randTemp = rand() % 40 + 60;
		int randNode = rand() % (size-1);

        shared->satelliteTemp[randNode] = randTemp;
        shared->satelliteTime[randNode] = time(NULL); // use now as the time
    }
    
	return NULL;
}