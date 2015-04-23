

int	describe_plug(void *info, void *fx_instance, void *in_values)
{
	char tmp[128];
	int id = 0;
	//@ generate unique ID
	snprintf(tmp,sizeof(tmp),"%d" id );

	plug_get_defaults( fx_instance, in_values );

	int n_channels = vevo_property_num_elements( fx_instance, "in_channels" );
	
	if( n_channels == 0 ) {

	}

	for( i = 0; i  < n_channels; i ++ ) {
		//@ find input channels
	}

	plug_build_name_space( NULL, fx_instance, user_data, NULL, NULL,
				notify_parameter, info );

	// vevo_sample_ui_construct_fx_window
	return 1;
}

static  void    sample_notify_parameter( void *sample, void *parameter, void *value )
{
        char *osc_path = vevo_property_get_string( parameter, "HOST_osc_path" );
        char *osc_types = vevo_property_get_string( parameter, "HOST_osc_types");
        
        sample_runtime_data *srd = (sample_runtime_data*) sample;
        int fx_entry = sample_extract_fx_entry_from_path( sample, osc_path );
        void *sender = veejay_get_osc_sender( srd->user_data );

        if( fx_entry >= 0 && fx_entry < SAMPLE_CHAIN_LEN && sender)
        {
                fx_slot_t *slot = sample_get_fx_port_ptr( sample, fx_entry );
                veejay_bundle_plugin_add( sender, slot->frame, osc_path, osc_types, value );
        }
}

