#ifndef SPLITDISPLAY
#define SPLITDISPLAY
void	*vj_split_display(int w, int h);
void	vj_split_process_frame( void *sd, uint8_t *work_buffer[3] );

void	vj_split_destroy(void *v);

void	vj_split_change_num_screens( void *sd, int n_screens );
void	vj_split_change_screen_setup(void *sd, int value);

int		vj_split_get_num_screens( void *sd );

void	vj_split_set_stream_in_screen( void *sd, int stream_id, int screen_no );

void	vj_split_get_frame( void *sd, uint8_t *row[3] );

void	vj_split_get_layout( void *sd, char *dst );

int		*vj_split_get_samples( void *sd );

#endif
