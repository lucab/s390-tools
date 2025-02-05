/*
 * zgetdump - Tool for copying and converting System z dumps
 *
 * NGDump dump tool
 *
 * Copyright IBM Corp. 2021
 *
 * s390-tools is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef ZGETDUMP_NGDUMP_H
#define ZGETDUMP_NGDUMP_H

#define NGDUMP_FSTYPE	"ext4"

struct ngdump_meta {
	int version;
	const char *file;
	const char *sha256sum;
};

int ngdump_read_meta_from_device(const char *device, struct ngdump_meta *meta);

int ngdump_get_dump_part(struct zg_fh *zg_fh);

int ngdump_get_disk_part_path(const char *disk_path, int part_num,
			      char **part_path);

#endif /* ZGETDUMP_NGDUMP_H */
