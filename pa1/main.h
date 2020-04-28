#ifndef FIRST_MAIN_H
#define FIRST_MAIN_H

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "common.h"
#include "pa1.h"
#include "ipc.h"

int matrix_flat_get(int i, int j, int k);

#endif //FIRST_MAIN_H
