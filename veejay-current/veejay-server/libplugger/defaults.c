/*
 * Copyright (C) 2015 Niels Elburg <nwelburg@gmail.com>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */
#include <config.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <veejaycore/vjmem.h>
#include "defaults.h"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

FILE *plug_open_config(const char *basedir,
                       const char *filename,
                       const char *mode,
                       int chkdir)
{
	int home_fd = -1, veejay_fd = -1, base_fd = -1, file_fd = -1;
	FILE *fp = NULL;
	char cfgname[PATH_MAX];
	const char *home = getenv("HOME");

	if (!home)
		return NULL;

		home_fd = open(home, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (home_fd < 0)
		goto out;

	if (chkdir) {
		if (mkdirat(home_fd, ".veejay", 0700) == -1 && errno != EEXIST)
			goto out;
	}

	veejay_fd = openat(home_fd, ".veejay",
	                   O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (veejay_fd < 0)
		goto out;

	if (chkdir) {
		if (mkdirat(veejay_fd, basedir, 0700) == -1 && errno != EEXIST)
			goto out;
	}

	base_fd = openat(veejay_fd, basedir,
	                  O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (base_fd < 0)
		goto out;

	snprintf(cfgname, sizeof(cfgname), "%s.cfg", filename);

	int flags = 0;
	if (mode[0] == 'r')
		flags = O_RDONLY;
	else if (mode[0] == 'w')
		flags = O_WRONLY | O_CREAT | O_TRUNC;
	else if (mode[0] == 'a')
		flags = O_WRONLY | O_CREAT | O_APPEND;
	else
		goto out;

	file_fd = openat(base_fd, cfgname,
	                 flags | O_CLOEXEC,
	                 0600);
	if (file_fd < 0)
		goto out;

	fp = fdopen(file_fd, mode);
	if (!fp)
		goto out;

	file_fd = -1;

out:
	if (file_fd >= 0) close(file_fd);
	if (base_fd >= 0) close(base_fd);
	if (veejay_fd >= 0) close(veejay_fd);
	if (home_fd >= 0) close(home_fd);

	return fp;
}
