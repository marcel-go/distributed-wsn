#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "sharedstruct.h"
#include "utilities.h"
#include "network.h"

int sensorNode(MPI_Comm worldComm, MPI_Comm comm, int nRows, int nCols);