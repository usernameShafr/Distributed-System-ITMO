#ifndef MAIN_FILE
#define MAIN_FILE

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "pa2345.h"
#include "ipc.h"
#include "banking.h"

int matrix_flat_get(int i, int j, int k);

#endif
