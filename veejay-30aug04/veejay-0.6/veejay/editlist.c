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

#include <config.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <errno.h>

#include <utils/mjpeg_logging.h>
#include <veejay/lav_io.h>
#include <veejay/editlist.h>
#include <veejay/vj-common.h>
#include <math.h>

/* Since we use malloc often, here the error handling */

void editlist_free(EditList *el) {

   if(el) {
     int n = el->num_video_files;
     int i;
     for(i =0; i < n; i++) if(el->video_file_list[n]) free(el->video_file_list[n]);
     if(el->frame_list) free(el->frame_list);
     free(el);
     el = NULL;	
   }

}

static void malloc_error(void)
{
    mjpeg_error_exit1("Out of memory - malloc failed");
}

static struct {
	char *name;
} _chroma_str[] = {
   {	"Unknown (invalid)" },
   {	"YUV 4:2:0"	    },
   {	"YUV 4:2:2"	    },
   {	"YUV 4:4:4"	    },
   {	NULL		    },
};

int open_video_file(char *filename, EditList * el, int preserve_pathname)
{
    int i, n, nerr;
    int chroma=0;
    int _fc;
    char realname[PATH_MAX];

    /* Get full pathname of file if the user hasn't specified preservation
       of pathnames...
     */
    bzero(realname, PATH_MAX);

    if (preserve_pathname) {
	strcpy(realname, filename);
    } else if (realpath(filename, realname) == 0) {
	veejay_msg(VEEJAY_MSG_ERROR ,"Cannot deduce real filename:%s",sys_errlist[errno]);
	return -1;
    }

    /* Check if this filename is allready present */

    for (i = 0; i < el->num_video_files; i++)
	if (strcmp(realname, el->video_file_list[i]) == 0) {
	    veejay_msg(VEEJAY_MSG_ERROR, "File %s already open", realname);
	    return i;
	}

    /* Check if MAX_EDIT_LIST_FILES will be exceeded */

    if (el->num_video_files >= MAX_EDIT_LIST_FILES) {
	// mjpeg_error_exit1("Maximum number of video files exceeded");
	veejay_msg(VEEJAY_MSG_ERROR,"Maximum number of video files exceeded\n");
	return -1; 
    }
    if (el->num_video_files >= 1)
	chroma = el->MJPG_chroma;
         
    n = el->num_video_files;
    el->num_video_files++;

    el->lav_fd[n] = lav_open_input_file(filename);
    if (!el->lav_fd[n]) {
	veejay_msg(VEEJAY_MSG_ERROR,"Error opening file [%s]: %s", filename,
		lav_strerror());
	el->num_video_files--;
	return -1;
    }
    _fc = lav_video_MJPG_chroma(el->lav_fd[n]);

    if( !(_fc == CHROMA422 || _fc == CHROMA420 || _fc == CHROMA444 || _fc == CHROMAUNKNOWN ))
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Input file %s is not in a valid format (%d)",filename,_fc);
		el->num_video_files --;
		return -1;

	}

    if(chroma == CHROMAUNKNOWN)
	{ /* set chroma */
  	  el->MJPG_chroma = _fc;
	  chroma = _fc;
	}

    el->num_frames[n] = lav_video_frames(el->lav_fd[n]);
    
    el->video_file_list[n] = strndup(realname, strlen(realname));
    if (el->video_file_list[n] == 0)
	malloc_error();

    /* Debug Output */

    veejay_msg(VEEJAY_MSG_DEBUG,"File: %s, absolute name: %s", filename, realname);
    veejay_msg(VEEJAY_MSG_DEBUG,"\tFrames:          %ld", lav_video_frames(el->lav_fd[n]));
    veejay_msg(VEEJAY_MSG_DEBUG,"\tWidth:           %d", lav_video_width(el->lav_fd[n]));
    veejay_msg(VEEJAY_MSG_DEBUG,"\tHeight:          %d", lav_video_height(el->lav_fd[n]));
    {
	const char *int_msg;
	switch (lav_video_interlacing(el->lav_fd[n])) {
	case LAV_NOT_INTERLACED:
	    int_msg = "Not interlaced";
	    break;
	case LAV_INTER_TOP_FIRST:
	    int_msg = "Top field first";
	    break;
	case LAV_INTER_BOTTOM_FIRST:
	    int_msg = "Bottom field first";
	    break;
	default:
	    int_msg = "Unknown!";
	    break;
	}
    veejay_msg(VEEJAY_MSG_DEBUG,"\tInterlacing:      %s", int_msg);
    }

    veejay_msg(VEEJAY_MSG_DEBUG,"\tFrames/sec:       %f", lav_frame_rate(el->lav_fd[n]));
    veejay_msg(VEEJAY_MSG_DEBUG,"\tSampling format:  %s", _chroma_str[ lav_video_MJPG_chroma(el->lav_fd[n])].name);
    veejay_msg(VEEJAY_MSG_DEBUG,"\tFOURCC:           %s",lav_video_compressor(el->lav_fd[n]));
    veejay_msg(VEEJAY_MSG_DEBUG,"\tAudio samps:      %ld", lav_audio_clips(el->lav_fd[n]));
    veejay_msg(VEEJAY_MSG_DEBUG,"\tAudio chans:      %d", lav_audio_channels(el->lav_fd[n]));
    veejay_msg(VEEJAY_MSG_DEBUG,"\tAudio bits:       %d", lav_audio_bits(el->lav_fd[n]));
    veejay_msg(VEEJAY_MSG_DEBUG,"\tAudio rate:       %ld", lav_audio_rate(el->lav_fd[n]));


    nerr = 0;
    if (n == 0) {
	/* First file determines parameters */

	el->video_height = lav_video_height(el->lav_fd[n]);
	el->video_width = lav_video_width(el->lav_fd[n]);
	el->video_inter = lav_video_interlacing(el->lav_fd[n]);
	el->video_fps = lav_frame_rate(el->lav_fd[n]);
	lav_video_clipaspect(el->lav_fd[n],
			       &el->video_sar_width,
			       &el->video_sar_height);
	if (!el->video_norm) {
	    /* TODO: This guessing here is a bit dubious but it can be over-ridden */
	    if (el->video_fps > 24.95 && el->video_fps < 25.05)
		el->video_norm = 'p';
	    else if (el->video_fps > 29.92 && el->video_fps <= 30.02)
		el->video_norm = 'n';
	}

	el->audio_chans = lav_audio_channels(el->lav_fd[n]);
	if (el->audio_chans > 2) {
	    el->num_video_files --;
	    veejay_msg(VEEJAY_MSG_ERROR, "File %s has %d audio channels - cant play that!",
	              filename,el->audio_chans);
	    nerr++;
	}

	el->has_audio = (el->audio_chans > 0);
	el->audio_bits = lav_audio_bits(el->lav_fd[n]);
	el->audio_rate = lav_audio_rate(el->lav_fd[n]);
	el->audio_bps = (el->audio_bits * el->audio_chans + 7) / 8;
    } else {
	/* All files after first have to match the paramters of the first */

	if (el->video_height != lav_video_height(el->lav_fd[n]) ||
	    el->video_width != lav_video_width(el->lav_fd[n])) {
	    veejay_msg(VEEJAY_MSG_ERROR,"File %s: Geometry %dx%d does not match %ldx%ld.",
			filename, lav_video_width(el->lav_fd[n]),
			lav_video_height(el->lav_fd[n]), el->video_width,
			el->video_height);
	    nerr++;
	}
	if (el->video_inter != lav_video_interlacing(el->lav_fd[n])) {
	    veejay_msg(VEEJAY_MSG_ERROR,"File %s: Interlacing is %d should be %ld",
			filename, lav_video_interlacing(el->lav_fd[n]),
			el->video_inter);
	    nerr++;
	}
	/* give a warning on different fps instead of error , this is better 
	   for live performances */
	if (fabs(el->video_fps - lav_frame_rate(el->lav_fd[n])) >
	    0.0000001) {
	    veejay_msg(VEEJAY_MSG_WARNING,"(Ignoring) File %s: fps is %3.2f should be %3.2f", filename,
		       lav_frame_rate(el->lav_fd[n]), el->video_fps);
	}
	/* If first file has no audio, we don't care about audio */

	if (el->has_audio) {
	    if (el->audio_chans != lav_audio_channels(el->lav_fd[n]) ||
		el->audio_bits != lav_audio_bits(el->lav_fd[n]) ||
		el->audio_rate != lav_audio_rate(el->lav_fd[n])) {
		veejay_msg(VEEJAY_MSG_WARNING,"(Ignoring) File %s: Audio is %d chans %d bit %ld Hz,"
			   " should be %d chans %d bit %ld Hz",
			   filename, lav_audio_channels(el->lav_fd[n]),
			   lav_audio_bits(el->lav_fd[n]),
			   lav_audio_rate(el->lav_fd[n]), el->audio_chans,
			   el->audio_bits, el->audio_rate);
	    }
	}

	if (nerr) {
	    el->num_video_files --;
	    return -1;
        }
    }


    return n;
}

int el_auto_deinter(EditList *el)
{
	if(el->auto_deinter==0) return 0;
	return 1;
}

/*
   read_video_files:

   Accepts a list of filenames as input and opens these files
   for subsequent playback.

   The list of filenames may consist of:

   - "+p" or "+n" as the first entry (forcing PAL or NTSC)

   - ordinary  AVI or Quicktimefile names

   -  Edit list file names

   - lines starting with a colon (:) are ignored for third-party editlist extensions

*/

int video_files_not_supported(EditList *el)
{
    int n;
    for(n=0; n < el->num_video_files; n++)
    {
       char *c = (char*)lav_video_compressor(el->lav_fd[n]);  
       if( 
#ifdef SUPPORT_READ_DV2
		strncasecmp("dvsd",c,4)!=0 &&
		strncasecmp("dv",c,2)!=0 &&
#endif
		strncasecmp("yuv",c,3)!=0 && 
		strncasecmp("iyuv",c,4)!=0 &&
		strncasecmp("jpeg",c,4)!=0 &&
		strncasecmp("mjpg",c,4)!=0 &&
		strncasecmp("mjpa",c,4)!=0 &&
		strncasecmp("div3",c,4)!=0 &&
		strncasecmp("mp4v",c,4)!=0 )
	{
		return 1;
	}
    }
    return 0;
}

void read_video_files(char **filename, int num_files, EditList * el,
		      int preserve_pathnames, int auto_deinter)
{
    FILE *fd;
    char line[1024];
    uint64_t index_list[MAX_EDIT_LIST_FILES];
    int num_list_files;
    int nf,n1=0,n2=0;
    long i,nl=0;
    uint64_t n;

    nf = 0;
    if(!el) {
	fprintf(stderr, "EditList is not initialized.\n");
	}
    memset(el, 0, sizeof(EditList));

    el->MJPG_chroma = CHROMA420;	/* will be reset if not the case for all files */

    /* Check if a norm parameter is present */

    if (strcmp(filename[0], "+p") == 0 || strcmp(filename[0], "+n") == 0) {
	el->video_norm = filename[0][1];
	nf = 1;
	mjpeg_info("Norm set to %s",
		   el->video_norm == 'n' ? "NTSC" : "PAL");
    }

    for (; nf < num_files; nf++) {
	/* Check if filename[nf] is a edit list */

	fd = fopen(filename[nf], "r");

	if (fd == 0) {
	    mjpeg_error_exit1("Error opening %s: %s", filename[nf],
			      strerror(errno));
	}

	fgets(line, 1024, fd);
	if (strcmp(line, "LAV Edit List\n") == 0) {
	    /* Ok, it is a edit list */
	    mjpeg_debug("Edit list %s opened", filename[nf]);

	    /* Read second line: Video norm */

	    fgets(line, 1024, fd);
	    if (line[0] != 'N' && line[0] != 'n' && line[0] != 'P'
		&& line[0] != 'p') {
		mjpeg_error_exit1("Edit list second line is not NTSC/PAL");
	    }

	    mjpeg_debug("Edit list norm is %s", line[0] == 'N'
			|| line[0] == 'n' ? "NTSC" : "PAL");

	    if (line[0] == 'N' || line[0] == 'n') {
		if (el->video_norm == 'p') {
		    mjpeg_error_exit1("Norm allready set to PAL");
		}
		el->video_norm = 'n';
	    } else {
		if (el->video_norm == 'n') {
		    mjpeg_error_exit1("Norm allready set to NTSC");
		}
		el->video_norm = 'p';
	    }

	    /* read third line: Number of files */

	    fgets(line, 1024, fd);
	    sscanf(line, "%d", &num_list_files);

	    mjpeg_debug("Edit list contains %d files", num_list_files);

	    /* read files */

	    for (i = 0; i < num_list_files; i++) {
		fgets(line, 1024, fd);
		n = strlen(line);
		if (line[n - 1] != '\n') {
		    mjpeg_error_exit1("Filename in edit list too long");
		}
		line[n - 1] = 0;	/* Get rid of \n at end */

		index_list[i] =
		    open_video_file(line, el, preserve_pathnames);
	    }

	    /* Read edit list entries */

	    while (fgets(line, 1024, fd)) {
		if (line[0] != ':') {	/* ignore lines starting with a : */
		    sscanf(line, "%ld %d %d", &nl, &n1, &n2);
		   
		    if (nl < 0 || nl >= num_list_files) {
			mjpeg_error_exit1
			    ("Wrong file number in edit list entry");
		    }
		    if (n1 < 0)
			n1 = 0;
		    if (n2 >= el->num_frames[index_list[nl]])
			n2 = el->num_frames[index_list[nl]];
		    if (n2 < n1)
			continue;

		    el->frame_list = (uint64_t *) realloc(el->frame_list,
						      (el->video_frames +
						       n2 - n1 +
						       1) * sizeof(uint64_t));
		    if (el->frame_list == 0)
			malloc_error();
		    for (i = n1; i <= n2; i++) {
			el->frame_list[el->video_frames] =  EL_ENTRY( index_list[ nl], i);

			el->video_frames++;
			}
		}
	    }

	    fclose(fd);
	} else {
	    /* Not an edit list - should be a ordinary video file */

	    fclose(fd);

	    n = open_video_file(filename[nf], el, preserve_pathnames);
	    
	    el->frame_list = (uint64_t *) realloc(el->frame_list,
					      (el->video_frames +
					       el->num_frames[n]) *
					      sizeof(uint64_t));
	    if (el->frame_list == 0)
		malloc_error();
	    for (i = 0; i < el->num_frames[n]; i++) {
		el->frame_list[el->video_frames] = EL_ENTRY(n, i);
		el->video_frames++;
		}
	}
    }

    /* Calculate maximum frame size */

    for (i = 0; i < el->video_frames; i++) {
	n = el->frame_list[i];
	if (lav_frame_size(el->lav_fd[N_EL_FILE(n)], N_EL_FRAME(n)) >
	    el->max_frame_size)
	    el->max_frame_size =
		lav_frame_size(el->lav_fd[N_EL_FILE(n)], N_EL_FRAME(n));
    }

    /* Help for audio positioning */

    el->last_afile = -1;

    el->auto_deinter = auto_deinter;
    if(el->video_inter != 0 ) el->auto_deinter = 0;
}

int write_edit_list(char *name, long n1, long n2, EditList * el)
{
    FILE *fd;
    int i, num_files, oldfile, oldframe;
    uint64_t index[MAX_EDIT_LIST_FILES];
    uint64_t n;
    /* check n1 and n2 for correctness */

    if (n1 < 0)
	n1 = 0;
    if (n2 >= el->video_frames)
	n2 = el->video_frames - 1;
    mjpeg_info("Write edit list: %ld %ld %s", n1, n2, name);

    fd = fopen(name, "w");
    if (fd == 0) {
	mjpeg_error("Can not open %s - no edit list written!", name);
	return -1;
    }
    fprintf(fd, "LAV Edit List\n");
    fprintf(fd, "%s\n", el->video_norm == 'n' ? "NTSC" : "PAL");

    /* get which files are actually referenced in the edit list entries */

    for (i = 0; i < MAX_EDIT_LIST_FILES; i++)
	index[i] = -1;

    for (i = n1; i <= n2; i++)
	index[N_EL_FILE(el->frame_list[i])] = 1;

    num_files = 0;
    for (i = 0; i < MAX_EDIT_LIST_FILES; i++)
	if (index[i] == 1)
	    index[i] = num_files++;

    fprintf(fd, "%d\n", num_files);
    for (i = 0; i < MAX_EDIT_LIST_FILES; i++)
	if (index[i] >= 0)
	    fprintf(fd, "%s\n", el->video_file_list[i]);

    oldfile = index[N_EL_FILE(el->frame_list[n1])];
    oldframe = N_EL_FRAME(el->frame_list[n1]);
    fprintf(fd, "%d %d ", oldfile, oldframe);

    for (i = n1 + 1; i <= n2; i++) {
	n = el->frame_list[i];
	if (index[N_EL_FILE(n)] != oldfile
	    || N_EL_FRAME(n) != oldframe + 1) {
	    fprintf(fd, "%d\n", oldframe);
	    fprintf(fd, "%ld %ld ",(unsigned long) index[N_EL_FILE(n)], (unsigned long)N_EL_FRAME(n));
	}
	oldfile = index[N_EL_FILE(n)];
	oldframe = N_EL_FRAME(n);
    }
    n = fprintf(fd, "%d\n", oldframe);

    /* We did not check if all our prints succeeded, so check at least the last one */

    if (n <= 0) {
	mjpeg_error("Error writing edit list: %s", strerror(errno));
	return -1;
    }

    fclose(fd);

    return 0;
}

int el_get_video_frame(uint8_t * vbuff, long nframe, EditList * el)
{
    int res;
    uint64_t n;
    if (nframe < 0)
	nframe = 0;
    if (nframe > el->video_frames)
	nframe = el->video_frames;
    n = el->frame_list[nframe];
     
    res = lav_set_video_position(el->lav_fd[N_EL_FILE(n)], N_EL_FRAME(n));
    if (res < 0) {
	mjpeg_error_exit1("Error setting video position: %s",
			  lav_strerror());
    }
    res = lav_read_frame(el->lav_fd[N_EL_FILE(n)], vbuff);
    if (res < 0) {
	mjpeg_error("Error reading video frame: %s", lav_strerror());
    }

    return res;
}

int el_debug_this_frame(EditList *el, long nframe) 
{
	uint64_t n;
	int res;
	n = el->frame_list[nframe];
	res = lav_set_video_position(el->lav_fd[N_EL_FILE(n)], N_EL_FRAME(n));
	if(res < 0) 
	{
		mjpeg_error("Error setting video position %s",lav_strerror());
	}
	mjpeg_error("Frame %ld (%ld) in file num %ld: [%s]",
		nframe,(unsigned long)N_EL_FRAME(n),(unsigned long) n,el->video_file_list[n]);
	return 0;
}


int el_get_sub_clip_format(EditList *el, long nframe)
{
	int res = el->MJPG_chroma;
	uint64_t n;
	n = el->frame_list[nframe];
	res = lav_video_MJPG_chroma( el->lav_fd[N_EL_FILE(n)]);
	return res;
}

int el_get_audio_data2(uint8_t * abuff, long nframe, EditList * el)
{
    long pos, asize;
    int ret = 0;
    uint64_t n;	
	int ns0, ns1;

    if (!el->has_audio)
	return 0;
    if (nframe < 0)
	nframe = 0;
    if (nframe > el->video_frames)
	nframe = el->video_frames;

    n = el->frame_list[nframe];
    ns1 = (double) (N_EL_FRAME(n) + 1) * el->audio_rate / el->video_fps;
    ns0 = (double) N_EL_FRAME(n) * el->audio_rate / el->video_fps;

    //asize = el->audio_rate / el->video_fps;
    pos = nframe * asize;
    ret = lav_set_audio_position(el->lav_fd[N_EL_FILE(n)], ns0);
    if (ret < 0)
	return -1;
    ret = lav_read_audio(el->lav_fd[N_EL_FILE(n)], abuff, (ns1 - ns0));
    if (ret < 0)
	return -1;
    return (ns1 - ns0);
}
/*
int el_get_audio_data3(uint8_t * abuff, long nframe, EditList * el,
		       int mute)
{
    int res, n, ns0, ns1, asamps;

    if (!el->has_audio)
	return 0;

    if (nframe < 0)
	nframe = 0;
    if (nframe > el->video_frames)
	nframe = el->video_frames;
    n = el->frame_list[nframe];

    ns1 = (double) (N_EL_FRAME(n) + 1) * el->audio_rate / el->video_fps;
    ns0 = (double) N_EL_FRAME(n) * el->audio_rate / el->video_fps;

    asamps = ns1 - ns0;


    if (mute) {
	memset(abuff, 0, asamps * el->audio_bps);
	return asamps * el->audio_bps;
    }

    if (el->last_afile != N_EL_FILE(n) || el->last_apos != ns0)
	lav_set_audio_position(el->lav_fd[N_EL_FILE(n)], ns0);

    res = lav_read_audio(el->lav_fd[N_EL_FILE(n)], abuff, asamps);
    if (res < 0) {
	mjpeg_error_exit1("Error reading audio: %s", lav_strerror());
    }

    if (res < asamps)
	memset(abuff + res * el->audio_bps, 0,
	       (asamps - res) * el->audio_bps);

    el->last_afile = N_EL_FILE(n);
    el->last_apos = ns1;
    return asamps;
}
*/
/* TODO: grab N x audio frames */
int el_get_audio_data(uint8_t * abuff, long nframe, EditList * el,
		      int mute)
{
    int res,    ns1, asamps;
    uint64_t n,ns0;
    if (!el->has_audio)
	return 0;

    if (nframe < 0)
	nframe = 0;
    if (nframe > el->video_frames)
	nframe = el->video_frames;
    n = el->frame_list[nframe];

    ns1 = (double) (N_EL_FRAME(n) + 1) * el->audio_rate / el->video_fps;
    ns0 = (double) N_EL_FRAME(n) * el->audio_rate / el->video_fps;

    asamps = ns1 - ns0;

    /* if mute flag is set, don't read actually, just return zero data */

    if (mute) {
	/* TODO: A.Stevens 2000 - this looks like a potential overflow
	   bug to me... non muted we only ever return asamps/FPS clips */
	memset(abuff, 0, asamps * el->audio_bps);
	return asamps * el->audio_bps;
    }

    if (el->last_afile != N_EL_FILE(n) || el->last_apos != ns0)
	lav_set_audio_position(el->lav_fd[N_EL_FILE(n)], ns0);

    res = lav_read_audio(el->lav_fd[N_EL_FILE(n)], abuff, asamps);
    if (res < 0) {
	mjpeg_error_exit1("Error reading audio: %s", lav_strerror());
    }

    if (res < asamps)
	memset(abuff + res * el->audio_bps, 0,
	       (asamps - res) * el->audio_bps);

    el->last_afile = N_EL_FILE(n);
    el->last_apos = ns1;
    return asamps * el->audio_bps;
}

int el_video_frame_data_format(long nframe, EditList * el)
{
    int n;
    const char *comp;

    if (el->video_frames <= 0)
	return DATAFORMAT_MJPG;	/* empty editlist, return default */
    if (nframe < 0)
	nframe = 0;
    if (nframe > el->video_frames)
	nframe = el->video_frames;
    n = N_EL_FILE(el->frame_list[nframe]);
    comp = lav_video_compressor(el->lav_fd[n]);

#ifdef SUPPORT_READ_DV2
    if (strncasecmp(comp, "dsvd",4) ==0)
	return DATAFORMAT_DV2;
    if (strncasecmp(comp, "dv", 2) == 0)
	return DATAFORMAT_DV2;
#endif
    if (strncasecmp(comp, "mjp", 3) == 0)
	return DATAFORMAT_MJPG;
    if (strncasecmp(comp, "jpeg", 4) == 0)
	return DATAFORMAT_MJPG;
    if (strncasecmp(comp, "iyuv", 4) == 0)
	return DATAFORMAT_YUV420;
    if (strncasecmp(comp, "div3", 4) == 0)
	return DATAFORMAT_DIVX;
    if (strncasecmp(comp, "yuv2", 4) == 0)
	return DATAFORMAT_YUV422;
    if (strncasecmp(comp, "mp4v", 4) == 0)
	return DATAFORMAT_MPEG4;
 

    return -1;
}
