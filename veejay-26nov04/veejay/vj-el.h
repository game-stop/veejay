#ifndef VJ_MLT_EL_H
#define VJ_MLT_EL_H
#include <config.h>
#include <veejay/vj-global.h>
#include <veejay/vj-avformat.h>
#include <veejay/lav_io.h>

#define N_EL_FRAME(x)  ( (x)&0xfffffffffffffLLU  )
#define N_EL_FILE(x) (int)  ( ((x)>>52)&0xfff ) 
/* ((file)&0xfff<<52) */
#define EL_ENTRY(file,frame) ( ((file)<<52) | ((frame)& 0xfffffffffffffLLU) )



typedef struct 
{
	int has_video;
	int	video_width;  
	int	video_height;
	int	video_inter;
	float	video_fps;
	int	video_sar_width;	
	int 	video_sar_height;	
	char 	video_norm;	

	int	has_audio; 
	long	audio_rate;
	int	audio_chans;
	int	audio_bits;
	int	audio_bps;
	int	play_rate;

	long 	video_frames; 

	long	num_video_files;

	long	max_frame_size;
	int	MJPG_chroma;

	char		*(video_file_list[MAX_EDIT_LIST_FILES]);
	lav_file_t	*(lav_fd[MAX_EDIT_LIST_FILES]);
	long 		num_frames[MAX_EDIT_LIST_FILES];
	uint64_t 	*frame_list;

	int 		last_afile;
	long 		last_apos;
	int		auto_deinter;

	int		pixel_format;
	
} editlist;  


editlist *vj_el_init_with_args(char **filenames, int n, int flags, int deinter, int force);

editlist *vj_el_probe_from_file( char *filename );

void	vj_el_free(editlist *el);

int	vj_el_get_audio_frame_at(editlist *el, uint32_t nframe, uint8_t *dst, int speed );

int	vj_el_append_video_file(editlist *el, char *filename);

int	vj_el_write_editlist( char *filename, long start, long end, editlist *el );

int	vj_el_get_video_frame(editlist *el, long nframe, uint8_t *dst[3], int pix_fmt);

int	vj_el_get_audio_frame(editlist *el, uint32_t nframe, uint8_t *dst);

void	vj_el_print(editlist *el);

#endif
