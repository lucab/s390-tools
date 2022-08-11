/*
 * Copyright IBM Corp. 2022
 *
 * s390-tools is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 *
 * This implements file-locking logic compatible with liblockfile
 * 'lockfile_create()' and 'lockfile_remove()'.
 * It is a simplified port of those, tailored to libap needs.
 * The reference code was written by Miquel van Smoorenburg and
 * released under LGPL-2.0-or-later:
 * https://github.com/miquels/liblockfile/blob/v1.17/lockfile.c
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "lockfile.h"

#define TMPLOCK_EXT ".lk"
#define TMPLOCK_EXT_SIZE strlen(TMPLOCK_EXT)
#define TMPLOCK_PID_SIZE 5
#define TMPLOCK_TIME_SIZE 1
#define TMPLOCK_SUFFIX_SIZE (TMPLOCK_EXT_SIZE + TMPLOCK_PID_SIZE + TMPLOCK_TIME_SIZE)

static int tmplock_filename(const char *lockfile, char *tmplock_buf,
							size_t tmplock_bufsize)
{
	int r;

	if (lockfile == NULL || tmplock_buf == NULL || tmplock_bufsize == 0)
		return LOCK_ERR_GENERIC;

	r = snprintf(tmplock_buf, tmplock_bufsize,
				 "%s%s%0*d%0*x",
				 lockfile,
				 TMPLOCK_EXT,
				 TMPLOCK_PID_SIZE, (int)getpid(),
				 TMPLOCK_TIME_SIZE, (int)time(NULL) & 15);
	tmplock_buf[tmplock_bufsize - 1] = '\0';
	if (r < 0)
		return LOCK_ERR_GENERIC;

	return 0;
}

// Check whether a valid lockfile is present.
static bool lockfile_check_valid(const char *lockfile)
{
	struct stat st, st2;
	char buf[16];
	time_t now;
	pid_t pid;
	int fd, len, r;

	if ((lockfile == NULL) || (stat(lockfile, &st) < 0))
		return false;

	// Get the contents and mtime of the lockfile.
	(void)time(&now);
	pid = 0;
	if ((fd = open(lockfile, O_RDONLY)) >= 0)
	{
		/*
		 *	Try to use 'atime after read' as now, this is
		 *	the time of the filesystem. Should not get
		 *	confused by 'atime' or 'noatime' mount options.
		 */
		len = 0;
		if (fstat(fd, &st) == 0 &&
			(len = read(fd, buf, sizeof(buf))) >= 0 &&
			fstat(fd, &st2) == 0 &&
			st.st_atime != st2.st_atime)
			now = st.st_atime;

		close(fd);
		if (len > 0)
		{
			buf[len] = 0;
			pid = atoi(buf);
		}
	}

	if (pid > 0)
	{
		/*
		 * If we have a pid, see if the process owning the lockfile
		 * is still alive.
		 */
		r = kill(pid, 0);
		if (r == 0 || (r < 0 && errno == EPERM))
			return true;
		if (r < 0 && errno == ESRCH)
			return false;
		// All other cases: fall through.
	}

	/*
	 * Without a pid in the lockfile, the lock is valid if it is newer
	 * than 5 mins.
	 */
	if (now < st.st_mtime + 300)
		return true;

	return false;
}

/**
 * Acquire the lock by filename
 *
 * @retval         0          Lock successfully acquired
 * @retval         != 0       Error, lock was not obtained
 */
static int lockfile_try_create(char *lockfile, pid_t pid, char *tmplock_buf,
							   size_t tmplock_bufsize, int retries)
{
	struct stat st, st1;
	char pidbuf[40];
	int sleeptime = 0;
	int statfailed = 0;
	int i, pidlen, r;
	int dontsleep = 1;
	int tries = retries + 1;

	pidlen = snprintf(pidbuf, sizeof(pidbuf), "%d\n", pid);
	if ((pidlen < 0) || ((size_t)pidlen > sizeof(pidbuf) - 1))
	{
		return LOCK_ERR_GENERIC;
	}

	// Create temporary lockfile.
	r = tmplock_filename(lockfile, tmplock_buf, tmplock_bufsize);
	if (r != 0)
		return r;
	int fd = open(tmplock_buf, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
	if (fd < 0)
	{
		return LOCK_ERR_TMPLOCK;
	}
	int written = write(fd, pidbuf, pidlen);
	r = close(fd);
	if (r != 0 || written != pidlen)
	{
		(void)unlink(tmplock_buf);
		return LOCK_ERR_TMPWRITE;
	}

	// Now try to link the temporary lock to the lock.
	for (i = 0; i < tries && tries > 0; i++)
	{
		if (!dontsleep)
		{
			sleeptime += 5;

			if (sleeptime > 60)
				sleeptime = 60;
			sleep(sleeptime);
		}
		dontsleep = 0;

		/*
		 *	Now lock by linking the tempfile to the lock.
		 *
		 *	KLUDGE: some people say the return code of
		 *	link() over NFS can't be trusted.
		 *	EXTRA FIX: the value of the nlink field
		 *	can't be trusted (may be cached).
		 */
		(void)link(tmplock_buf, lockfile);

		if (lstat(tmplock_buf, &st1) < 0)
			return LOCK_ERR_GENERIC;

		if (lstat(lockfile, &st) < 0)
		{
			if (statfailed++ > 5)
			{
				/*
				 *	Normally, this can't happen; either
				 *	another process holds the lockfile or
				 *	we do. So if this error pops up
				 *	repeatedly, just exit...
				 */
				(void)unlink(tmplock_buf);
				return LOCK_ERR_MAXRETRIES;
			}
			continue;
		}

		// See if we got the lock.
		if (st.st_rdev == st1.st_rdev &&
			st.st_ino == st1.st_ino)
		{
			(void)unlink(tmplock_buf);
			return 0;
		}
		statfailed = 0;

		// There may be an invalid lockfile left over, try to remove it.
		if (!lockfile_check_valid(lockfile))
		{
			if (unlink(lockfile) < 0 && errno != ENOENT)
			{
				/* We failed to unlink the stale lockfile, give up. */
				return LOCK_ERR_RMSTALE;
			}
			dontsleep = 1;
			/*
			 * If the lockfile was invalid, then the first try
			 * wasn't valid either - make sure we try at least once more.
			 */
			if (tries == 1)
				tries++;
		}
	}

	(void)unlink(tmplock_buf);
	return LOCK_ERR_MAXRETRIES;
}

/**
 * Acquire the ap config lock using the given ID
 *
 * @retval         0          Lock successfully acquired
 * @retval         != 0       Error, lock was not obtained
 */
int ap_lockfile_create(char *lockfile, pid_t pid, unsigned int retries)
{
	size_t tmplock_bufsize;
	char *tmplock_buf;

	if (lockfile == NULL || retries == 0)
		return LOCK_ERR_GENERIC;

	tmplock_bufsize = strlen(lockfile) + TMPLOCK_SUFFIX_SIZE + 1;
	tmplock_buf = (char *)malloc(tmplock_bufsize);
	if (tmplock_buf == NULL)
		return LOCK_ERR_GENERIC;
	tmplock_buf[0] = 0;

	int r = lockfile_try_create(lockfile, pid, tmplock_buf, tmplock_bufsize,
								retries);
	(void)free(tmplock_buf);

	return r;
}

/**
 * Release the ap config lock
 *
 * @retval         0          Lock successfully released or file didn't exist
 * @retval         != 0       Error removing the lockfile
 */
int ap_lockfile_release(char *lockfile)
{
	if (lockfile == NULL)
		return UNLOCK_ERR_GENERIC;

	if ((unlink(lockfile) < 0) && (errno != ENOENT))
		return UNLOCK_ERR_GENERIC;

	return 0;
}
