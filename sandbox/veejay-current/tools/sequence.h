int	veejay_get_image( void *data , guchar *pixels);

void	*veejay_sequence_init(int port, char *hostname, gint w, gint h);

void	veejay_configure_sequence( void *data, gint w, gint h );

void	veejay_sequence_free( void *data );

