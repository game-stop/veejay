/* veejay - Linux VeeJay
 *           (C) 2002-2004 Niels Elburg <nelburg@looze.net> 
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <veejay/vj-plug.h>
#include <string.h>
#include <stdlib.h>

typedef struct 
{
	VJPluginInfo *info;
} example_context;


VJPluginInfo *Info(void *ctx)
{
	example_context *ec = (example_context*) ctx;
	return ec->info;
}
	
void	Free(void *ctx)
{
	if(ctx)
		free(ctx);
}

int	Init(void **ctxp)
{
	*ctxp = malloc(sizeof(example_context));
	{
	example_context *ec = (example_context*) *ctxp;
	memset( ec, 0, sizeof(example_context));
	if(!*ctxp) return 0;
	ec->info = (VJPluginInfo*)malloc(sizeof(VJPluginInfo));
	if(!ec->info) return 0;
	ec->info->name = strdup("Dummy");
	ec->info->help = strdup("Example plugin");
	ec->info->plugin_type = VJPLUG_NORMAL;
	}
	return 1;
}

void	Process(void *ctx, void *_info, void *_picture )
{
	VJFrame	*picture = (VJFrame*) _picture;
	
	memset ( picture->data[1], 128, picture->uv_len );
	memset ( picture->data[2], 128, picture->uv_len );

}

void	Event(void *ctx, const char *args)
{
}

