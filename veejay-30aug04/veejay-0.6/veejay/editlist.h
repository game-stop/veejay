/*
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
 
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef EDITLIST_H
#define EDITLIST_H

#include "lav_io.h"


/* If changing MAX_EDIT_LIST_FILES, the macros below
   also have to be adapted. */

/*

#define MAX_EDIT_LIST_FILES 256

#define N_EL_FRAME(x)  ( (x)&0xffffff )
#define N_EL_FILE(x)   ( ((x)>>24)&0xff )
#define EL_ENTRY(file,frame) ( ((file)<<24) | ((frame)&0xffffff) )
*/
#define MAX_EDIT_LIST_FILES 4096

#define N_EL_FRAME(x)  ( (x)&0xfffffffffffffLLU  )
#define N_EL_FILE(x) (int)  ( ((x)>>52)&0xfff ) 
/* ((file)&0xfff<<52) */
#define EL_ENTRY(file,frame) ( ((file)<<52) | ((frame)& 0xfffffffffffffLLU) )

typedef struct {
    long video_frames;
    long video_width;
    long video_height;
    long video_inter;
    long video_norm;
    double video_fps;
    int video_sar_width;	/* clip aspect ratio */
    int video_sar_height;

    long max_frame_size;
    int MJPG_chroma;
    /* TODO: Need to flag mixed chroma model files? */

    int has_audio;
    long audio_rate;
    int audio_chans;
    int audio_bits;
    int audio_bps;

    long num_video_files;
    char *(video_file_list[MAX_EDIT_LIST_FILES]);
    lav_file_t *(lav_fd[MAX_EDIT_LIST_FILES]);
    long num_frames[MAX_EDIT_LIST_FILES];
    uint64_t *frame_list;

    int last_afile;
    long last_apos;
    int auto_deinter; 
} EditList;

void editlist_free(EditList *el);
int video_files_not_supported(EditList *el);
int el_get_video_frame(uint8_t * vbuff, long nframe, EditList * el);
int el_get_audio_data(uint8_t * abuff, long nframe, EditList * el,
		      int mute);
void read_video_files(char **filename, int num_files, EditList * el,
		      int preserve_pathnames, int deinter);
int write_edit_list(char *name, long n1, long n2, EditList * el);
int open_video_file(char *filename, EditList * el, int preserve_pathname);
int el_video_frame_data_format(long nframe, EditList * el);
int el_get_audio_data2(uint8_t * abuff, long nframe, EditList * el);
int	el_get_sub_clip_format(EditList *el, long nframe);
int	el_debug_this_frame(EditList *el, long nframe);
int	el_auto_deinter(EditList *el);
#endif				/* ifndef EDITLIST_H */
