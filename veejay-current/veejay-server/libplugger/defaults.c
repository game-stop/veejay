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
#include <libvjmem/vjmem.h>
#include "defaults.h"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

FILE	*plug_open_config(const char *basedir, const char *filename, char *mode, int chkdir )
{
	char path[PATH_MAX];
	char *home = getenv( "HOME" );
	if(!home) {
		return NULL;
	}

	snprintf( path, sizeof(path), "%s/.veejay/%s", home, basedir);

	if( chkdir ) {
		struct stat st;
		veejay_memset(&st,0,sizeof(struct stat));
		if( stat( path, &st ) == -1 ) {
			if(mkdir( path, 0700 ) == -1 ) {
				return NULL;
			}
		}
	}

	snprintf( path, sizeof(path), "%s/.veejay/%s/%s.cfg", home,basedir, filename);
	return fopen( path, mode );
}
