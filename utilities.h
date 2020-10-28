#pragma once

#include <time.h>
#include <errno.h>

void rankToCoord(int rank, int *coord, int nCols);

void timeToString(time_t rawTime, char *buffer);

int coordToPos(int *coord1, int *coord2);

int msleep(long msec);