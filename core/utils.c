/*

Copyright (c) 2003,2004 Rusty Russell

This file is part of nfsim.

nfsim is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

nfsim is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with nfsim; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "utils.h"
#include "log.h"
#include "talloc.h"

void barf(const char *fmt, ...)
{
	char *str;
	va_list arglist;

	fprintf(stderr, "FATAL: ");

	va_start(arglist, fmt);
	vasprintf(&str, fmt, arglist);
	va_end(arglist);

	nfsim_log(LOG_ALWAYS, "%s", str);
	free(str);
	exit(EXIT_FAILURE);
}

void barf_perror(const char *fmt, ...)
{
	char *str;
	int err = errno;
	va_list arglist;

	fprintf(stderr, "FATAL: ");

	va_start(arglist, fmt);
	vasprintf(&str, fmt, arglist);
	va_end(arglist);

	nfsim_log(LOG_ALWAYS, "%s: %s", str, strerror(err));
	free(str);
	exit(EXIT_FAILURE);
}

/* This version adds one byte (for nul term) */
void *grab_file(int fd, unsigned long *size)
{
	unsigned int max = 16384;
	int ret;
	void *buffer = talloc_array(NULL, char, max+1);

	*size = 0;
	while ((ret = read(fd, buffer + *size, max - *size)) > 0) {
		*size += ret;
		if (*size == max)
			buffer = talloc_realloc(NULL, buffer, char, max*=2+1);
	}
	if (ret < 0) {
		talloc_free(buffer);
		buffer = NULL;
	}
	return buffer;
}

void release_file(void *data, unsigned long size)
{
	talloc_free(data);
}
