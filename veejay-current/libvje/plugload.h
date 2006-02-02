#ifndef PLUGINLOADER_
#define PLUGINLOADER_
/*
 * Copyright (C) 2002-2006 Niels Elburg <nelburg@looze.net>
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

#include "vje.h"

void	plug_free(void);
void	plug_init( int w, int h );
int	plug_activate( int fx_id );
int	plug_deactivate( int fx_id );

int	plug_detect_plugins(void);
vj_effect	*plug_get_plugin(int n);

void	plug_process( VJFrame *frame, int fx_id , int src_fmt );

#endif
