/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nelburg@looze.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 */

#include <config.h>
#include <veejay/vj-plugin.h>
#include <dlfcn.h>
#include <veejay/vj-effect.h>
#include <veejay/vj-common.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <veejay/hash.h>

static hash_t	*PluginHash;

/*

	This hosts a Veejay Plugin.


*/

typedef struct _veejay_plugin
{
	vj_plugin_info		Info;		// Information
	vj_plugin_init		Init;		// Initialization
	vj_plugin_event		Event;		// Event to start
	vj_plugin_process	Process;	// Frame to process
	vj_plugin_free		Free;		// Cleanup
	vj_plugin_pull		Pull;		// Push data to veejay from plugin
 	int			active;		// Running / Stopped
	int			plugin_type;	// Plugin Type	
	char			*name;		// (Full) path to plugin name
	char			*filename;
	void 			*context;	// Plugin's specific Context	

	void			*handle;	// dl open handle

} plugin_hook_entry;


static int plugins_ready = 0;


void	plugins_allocate()
{
	PluginHash = hash_create( HASHCOUNT_T_MAX, NULL, NULL );
	if(PluginHash) plugins_ready = 1;
}

plugin_hook_entry	*plugins_get(const char *name)
{
	plugin_hook_entry *plugin;
	hnode_t *plugin_node = hash_lookup(PluginHash, (void*) name );
	if( plugin_node )
	{
		plugin = (plugin_hook_entry*) hnode_get(plugin_node);
		if(plugin) return plugin;
	}
	return NULL;
}

int	plugin_exists( const char *name )
{
	hnode_t *plugin_node;
	if(!name) return 0;
	plugin_node = hash_lookup( PluginHash, (void*) name );
	if(!plugin_node) return 0;
	if(!plugins_get( name )) return 0;
	return 1;
}

int	plugins_store( plugin_hook_entry *plugin )
{
	hnode_t *plugin_node;
	if(!plugin) return 0;
	plugin_node = hnode_create( plugin );
	if( !plugin_node) return 0;
	if ( !plugin_exists( plugin->name ) )
	{
		hash_insert( PluginHash, plugin_node, (void*) plugin->name );
		veejay_msg(VEEJAY_MSG_DEBUG, "Stored plugin '%s' ", plugin->name);
		return 1;
	}
	return 0;
}

int 	plugins_load(const char *path)
{
	DIR *dp;
	struct dirent *ep;
	int num_plugins = 0;
	dp = opendir(path);
	if(dp!=NULL)
	{
		while( (ep = readdir(dp)) )
		{
			if(plugins_init(ep->d_name))
			{
				num_plugins++;
			}
		}
		closedir(dp);
		return num_plugins;
	}
	return 0;
}

int	plugins_init( const char *plugin )
{
	char *abs_name[1024];
	plugin_hook_entry *phe;
	VJPluginInfo *info;
	if(!realpath( plugin, abs_name ) )
		return 0;

	phe = vj_malloc( sizeof(plugin_hook_entry));
	if(!phe) return 0;


	phe->handle = dlopen( (const char *)abs_name, RTLD_NOW );

	if(!phe->handle)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot open plugin '%s' : %s" , 
			plugin, dlerror());
		return 0;
	}

	phe->Info = dlsym(phe->handle, "Info");
	phe->Init = dlsym(phe->handle, "Init");
	phe->Event = dlsym(phe->handle, "Event");
	phe->Process = dlsym(phe->handle, "Process");
	phe->Free = dlsym(phe->handle, "Free"); 
	phe->Pull = dlsym(phe->handle, "Pull");
	phe->active = 1;

	phe->filename = (char*) malloc(sizeof(char) * strlen(plugin)+1);
	sprintf(phe->filename, "%s", plugin );

	if(!phe->Process)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to find Process entry point in %s", plugin);
		free(phe);
		return 0;
	}

	if(!phe->Init)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to find Init entrypoint in %s", plugin);
		free(phe);
		return 0;
	}

	if(!phe->Info)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to find Info entrypoint in %s", plugin);
		free(phe);
		return 0;
	}

	if(!phe->Init(&phe->context)) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to configure plugin '%s'", plugin);
		free(phe);
		return 0;
	}


	info = phe->Info(phe->context);
	phe->name = (char*)strdup(info->name);
//	phe->plugin_type = info->plugin_type;
	// there exist no pull plugins yet ...
	phe->plugin_type = VJPLUG_NORMAL;
//	veejay_msg(VEEJAY_MSG_INFO, "Loaded plugin '%s' ", phe->name);

	if( plugins_store( phe ) )
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Stored plugin in hash");
	}


	return 1;
}
void	plugins_process_video_in(void *info, void *picture)
{
	hscan_t it;
	hnode_t *node;

	
	if( hash_isempty( PluginHash)) return;
	
	hash_scan_begin( &it, PluginHash );

	while( (node=hash_scan_next(&it)))
	{
		plugin_hook_entry *phe = (plugin_hook_entry*) hnode_get(node);
			
		if( phe && phe->Process && phe->plugin_type == VJPLUG_VIDI_DRIVER)
		{
			phe->Process(phe->context, info, picture );
		}
	}

}

void	plugins_process_video_out(void *info, void *picture)
{
	hscan_t it;
	hnode_t *node;

	
	if( hash_isempty( PluginHash)) return;
	
	hash_scan_begin( &it, PluginHash );

	while( (node=hash_scan_next(&it)))
	{
		plugin_hook_entry *phe = (plugin_hook_entry*) hnode_get(node);
			
		if( phe && phe->Process && phe->plugin_type == VJPLUG_VIDO_DRIVER)
		{
			phe->Process(phe->context, info, picture );
		}
	}

}

void	plugins_process( void *info, void *picture )
{
	hscan_t it;
	hnode_t *node;

	
	if( hash_isempty( PluginHash)) return;
	
	hash_scan_begin( &it, PluginHash );

	while( (node=hash_scan_next(&it)))
	{
		plugin_hook_entry *phe = (plugin_hook_entry*) hnode_get(node);
			
		if( phe && phe->Process && phe->plugin_type == VJPLUG_NORMAL)
		{
			phe->Process(phe->context, info, picture );
		}
	}
}



void	plugins_pull(void *info, void *dst_picture )
{

}



void	plugins_free( const char *name )
{

	plugin_hook_entry *phe = plugins_get( name );

	if( phe )
	{
		hnode_t *plugin_node = hash_lookup(PluginHash, (void*) name );
		if(plugin_node)
		{
			if(phe->Free) phe->Free(phe->context);
			//if(phe->handle)
			//	dlclose( phe->handle );
			
			hash_delete( PluginHash, plugin_node );

			if(phe->filename) free(phe->filename);

			if(phe->name) free(phe->name);

			if(phe) free(phe);
			
			veejay_msg(VEEJAY_MSG_DEBUG,"Released plugin %s",name);

		}
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Plugin '%s' is not loaded", name);
	}
}


void	plugins_event(const char *name, const char *args)
{

	plugin_hook_entry *phe = plugins_get( name );

	if( phe )
	{
		if(phe->Event)
		{
			phe->Event(phe->context, args);
		}
	}
}

/*

	VJPluginInfo : plugins_get_pluginfo( name of plugin )

	returns information about the plugin

*/

VJPluginInfo	*plugins_get_pluginfo(char *name)
{
	VJPluginInfo *info = NULL;
	plugin_hook_entry *phe = plugins_get(name );

	if( phe )
	{
		info = phe->Info(phe->context);
		veejay_msg(VEEJAY_MSG_DEBUG,
			"Plugin '%s' : %s ",
			info->name,
			info->help);
	}	

	return info;
}
