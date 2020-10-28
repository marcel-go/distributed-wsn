#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <mpi.h>
#include <pthread.h>
#include "sharedstruct.h"
#include "utilities.h"
#include "satellite.h"

int baseStation(MPI_Comm worldComm, MPI_Comm comm, int nIntervals, int nRows, int nCols);
