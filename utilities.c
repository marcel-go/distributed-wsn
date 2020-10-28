#include "utilities.h"

void rankToCoord(int rank, int *coords, int nCols) {
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

int msleep(long msec) {
    struct timespec ts;
    int res;

    if (msec < 0) {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return res;
}