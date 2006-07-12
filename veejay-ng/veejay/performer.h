#ifndef PERFORMER_H
#define PERFORMER_H
void	*performer_init( veejay_t *info, const int n );
void	*performer_get_output_frame( veejay_t *info );
void	performer_convert_output_frame( veejay_t *info, uint8_t **frame);
int	performer_queue_audio_frame( veejay_t *info , int skip_incr );
int	performer_queue_video_frame( veejay_t *info , int skip_incr );
int	performer_queue_frame( veejay_t *info, int skip_incr );
void	performer_destroy( veejay_t *info );
#endif
