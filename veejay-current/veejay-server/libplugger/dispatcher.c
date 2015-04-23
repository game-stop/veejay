
int	veejay_osc_verify_format( void *vevo_port, char const *types )
{
	char *format = get_str_vevo( vevo_port, "format" );
	int n = strlen(types);
	if(!format)
	{
		if( (n ==0 || types == NULL) && format == NULL )
			return 1;
		return 0;
	}
	if( strcasecmp( types,format ) == 0 )
	{
		free(format);
		return 1;
	}
	free(format);
	return 0;
}

int		veejay_osc_property_calls_event( void *instance, const char *path, char *types, void **argv[], void *raw )
{
	veejay_t *v = (veejay_t*) instance;
	void *vevo_port = v->osc_namespace;

	int atom_type = vevo_property_atom_type( vevo_port, path );
	if( atom_type == VEVO_ATOM_TYPE_PORTPTR )
	{
		void *port = NULL;
		int error = vevo_property_get( vevo_port, path,0,&port );
	
		if(error == VEVO_NO_ERROR )
		{
			vevo_event_f f;
			if( veejay_osc_verify_format( port, types ) )
			{
				error = vevo_property_get( port, "func",0,&f );
				if( error == VEVO_NO_ERROR )
				{
					(*f)( instance,path, types, argv,raw );
					return 1;
				}
			}
		}
	}

	return 0;
}

void		deinit() {
 
        veejay_osc_del_methods( info,info->osc_namespace,info ,info);
	//vj_event_stop();

	vevo_port_recursive_free( info->osc_namespace );


}

void		init() {
	char server_port_name[10];
	char *server 	 = veejay_osc_server_get_addr( info->osc_server );
	int   server_port= veejay_osc_server_get_port( info->osc_server );	

	sprintf( server_port_name, "%d", server_port );
	
	veejay_msg(0, "Veejay server '%s' communicates with %s" ,server,uri );

	void *ui = veejay_new_osc_sender_uri( uri );
	veejay_send_osc( ui, "/veejay", "sx", server );
	int error = vevo_property_set( info->clients, uri, VEVO_ATOM_TYPE_VOIDPTR,1,&ui );

}


void	*veejay_get_osc_sender(veejay_t * info )
{
	if(!info)
		return NULL;
	void *sender = NULL;
	if(!info->current_client)
		return NULL;
	int error = vevo_property_get( info->clients, info->current_client, 0, &sender );
	if( error == VEVO_NO_ERROR )
		return sender;
	return NULL;
}

void	*veejay_get_osc_sender_by_uri( veejay_t *info , const char *uri )
{
	void *sender = NULL;
	int error = vevo_property_get( info->clients, uri, 0, &sender );
	if( error == VEVO_NO_ERROR )
		return sender;
	return NULL;

}

void	veejay_init_ui(veejay_t * info , const char *uri)
{
	char veejaystr[100];
	sprintf(veejaystr, "Veejay-NG %s", VERSION );
	void *sender = veejay_get_osc_sender( info );

	veejay_osc_set_window( sender , "MainWindow" );

	veejay_ui_bundle_add( sender, 
			"/create/window","ssx", "MainWindow", veejaystr );
	veejay_ui_bundle_add( sender,
			"/create/frame", "sssx", "MainWindow", "VeejayPanel",
			" " );

	veejay_ui_bundle_add( sender,
			"/show/window", "sx", "MainWindow" );
}




int 	setup(oscplug *info, port)
{
	int port = info->port_offset;
	const char port_str[50];
	sprintf(port_str, "%d",port );
	info->osc_server = veejay_new_osc_server( (void*)info, port_str );
	info->osc_namespace = vpn( VEVO_ANONYMOUS_PORT );
	veejay_osc_namespace_events( (void*) info, "/veejay");
	return 1;
}

