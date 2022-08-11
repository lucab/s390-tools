/*
 * Copyright IBM Corp. 2022
 *
 * s390-tools is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _LIBAP_LOCKFILE_H
#define _LIBAP_LOCKFILE_H

#include <unistd.h>

/*
 * Locking error-codes, compatible with previous implemention based
 * on liblockfile.
 */
#define LOCK_ERR_TMPLOCK 2
#define LOCK_ERR_TMPWRITE 3
#define LOCK_ERR_MAXRETRIES 4
#define LOCK_ERR_GENERIC 5
#define LOCK_ERR_ORPHANED 7
#define LOCK_ERR_RMSTALE 8

/*
 * Unlocking error-codes, compatible with previous implemention based
 * on liblockfile.
 */
#define UNLOCK_ERR_GENERIC -1

int ap_lockfile_create(char *lockfile, pid_t pid, unsigned int retries);
int ap_lockfile_release(char *lockfile);

#endif
