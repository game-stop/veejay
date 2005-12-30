/*
 * drawtext.c: print text over the screen
 ******************************************************************************
 * Options:
 * -f <filename>    font filename (MANDATORY!!!)
 * -s <pixel_size>  font size in pixels [default 16]
 * -b               print background
 * -o               outline glyphs (use the bg color)
 * -x <pos>         x position ( >= 0) [default 0]
 * -y <pos>         y position ( >= 0) [default 0]
 * -t <text>        text to print (will be passed to strftime())
 *                  MANDATORY: will be used even when -T is used. 
 *                  in this case, -t will be used if some error 
 *                  occurs
 * -T <filename>    file with the text (re-read every frame)
 * -c <#RRGGBB>     foreground color ('internet' way) [default #ffffff]
 * -C <#RRGGBB>     background color ('internet' way) [default #000000]
 *
 ******************************************************************************
 * Features:
 * - True Type, Type1 and others via FreeType2 library
 * - Font kerning (better output)
 * - Line Wrap (if the text doesn't fit, the next char go to the next line)
 * - Background box
 * - Outline
 ******************************************************************************
 * Author: Gustavo Sverzut Barbieri <gsbarbieri@yahoo.com.br>
 *         Niels Elburg <nelburg@looze.net>
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


#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#ifdef HAVE_FREETYPE
#include <fcntl.h>
#include <ft2build.h>

#include FT_FREETYPE_H
#include FT_GLYPH_H

#include <veejay-plugin.h>

#define RGB_TO_YUV(rgb_color, yuv_color) { \
    yuv_color[0] = ( 0.257 * rgb_color[0]) + (0.504 * rgb_color[1]) + (0.098 * rgb_color[2]) +  16; \
    yuv_color[2] = ( 0.439 * rgb_color[0]) - (0.368 * rgb_color[1]) - (0.071 * rgb_color[2]) + 128; \
    yuv_color[1] = (-0.148 * rgb_color[0]) - (0.291 * rgb_color[1]) + (0.439 * rgb_color[2]) + 128; \
}

#define DEFAULT_FONT  "/usr/X11R6/lib/X11/fonts/TTF/VeraSe.ttf"
#define DEFAULT_FONT_PATH "/usr/X11R6/lib/X11/fonts/TTF/"



typedef struct {
	unsigned char *text;
	unsigned char *file;
	unsigned char *font;
	unsigned int x;
	unsigned int y;
	int bg;
	int outline;
	uint8_t bgcolor[3]; /* YUV */
	uint8_t fgcolor[3]; /* YUV */
	FT_Library library;
	FT_Face    face;
	FT_Glyph   glyphs[ 255 ]; 
	FT_Bitmap  bitmaps[ 255 ];
	int        advance[ 255 ];
	int        bitmap_left[ 255 ];
	int        bitmap_top[ 255 ];
	unsigned int glyphs_index[ 255 ];
	int        text_height;
	int        baseline;
	int use_kerning;
	int size;
	VJPluginInfo *info;
	char	**font_table;
	int	font_index;
	int	rand_font;
} ContextInfo;

#define MAX_FONTS 100

VJPluginInfo *Info(void *ctx)
{
        ContextInfo *ec = (ContextInfo*) ctx;
        return (VJPluginInfo*) ec->info;
}

int	font_selector( const struct dirent *dir )
{	
	if(strstr(dir->d_name, ".ttf" )) return 1;
	if(strstr(dir->d_name, ".TTF" )) return 1;
	if(strstr(dir->d_name, ".pfa" )) return 1;
	if(strstr(dir->d_name, ".pcf.gz" )) return 1;
	return 0;
}


int	find_fonts(ContextInfo *ec, char *path)
{
	struct dirent **files;
	int n = scandir(path, &files, font_selector,alphasort);

	if(n < 0)
		return 0;

	while(n--)
	{
		char tmp[512];
		snprintf(tmp,512,"%s/%s",path,files[n]->d_name);
		if(ec->font_index<MAX_FONTS)
		{
			ec->font_table[ec->font_index] = strdup(tmp);
			ec->font_index++;
		}
	}
	return 1;
}

char 	*select_font( ContextInfo *ec, int id )
{
	int i;
	for( i = 0; i < ec->font_index; i ++)
	{
		if(id == i ) return ec->font_table[i]; 	 
	}
	return NULL;
}

void	print_fonts(ContextInfo *ec)
{
	int i;
	for(i =0 ; i < ec->font_index ; i ++ )
	{
		veejay_msg(VEEJAY_MSG_INFO, "[%3d] : [%s]", 
			i, ec->font_table[i] );
	}
}

int ParseColor(char *text, uint8_t *y , uint8_t *u, uint8_t *v)
{
  uint8_t tmp[3];
  uint8_t rgb_color[3];
  int i;

  tmp[2] = '\0';

  if ((!text) || (strlen(text) != 7) || (text[0] != '#') )
    return -1;

  for (i=0; i < 3; i++)
    {
      tmp[0] = text[i*2+1];
      tmp[1] = text[i*2+2];

      rgb_color[i] = (uint8_t) atoi( tmp );
    }

  *y = (uint8_t) ( (float) ( 0.257 * rgb_color[0]) + (0.504 * rgb_color[1]) + (0.098 * rgb_color[2]) +  16);
  *u = (uint8_t) ( (float) ( 0.439 * rgb_color[0]) - (0.368 * rgb_color[1]) - (0.071 * rgb_color[2]) + 128);
  *v = (uint8_t) ( (float) (-0.148 * rgb_color[0]) - (0.291 * rgb_color[1]) + (0.439 * rgb_color[2]) + 128); 


  return 0;
}

void	Free(void *ctx)
{
	if( ctx )
	{
		ContextInfo *ec = (ContextInfo*) ctx;
		if(ec->info)
		{
			if(ec->info->name) free(ec->info->name);	
			if(ec->info->help) free(ec->info->help);
			free(ec->info);
		}
		if(ec->text) free(ec->text);
		if(ec->font) free(ec->font);
		free(ec);
	}
}

int	configure(void *ctx, int size)
{
	int c,error;
//	char *font = "/usr/X11R6/lib/X11/fonts/TTF/luxisri.ttf";

	FT_BBox bbox;
	int yMax,yMin;
	ContextInfo *ci = (ContextInfo*) ctx;

	ci->size = size;

	if(ci->rand_font)
	{
		int ran = 1 + (int) (((double)ci->rand_font * rand())/(RAND_MAX+1.0));
		sprintf( ci->font, "%s", select_font(ci, ran ) );
		veejay_msg(VEEJAY_MSG_DEBUG,"Random %d font  [%s]", ran,ci->font);	
	}
	if(ci->font == NULL )
		return 0;
	
	if ( (error = FT_Init_FreeType(&(ci->library))) != 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Cannot load FreeType (error #%d) \n",error);
		return 0;
	}
	if ( (error = FT_New_Face( ci->library, ci->font, 0, &(ci->face) )) != 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Cannot load face: %s (error #%d)\n ", ci->font, error);
		return 0;
	}
	if ( (error = FT_Set_Pixel_Sizes( ci->face, 0, ci->size)) != 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot set font size to %d pixels (error #%d)\n", ci->size, error );
		return 0;
	}

	ci->use_kerning	=	FT_HAS_KERNING(ci->face);
	
	yMax = -32000;
	yMin = 32000;
	for( c = 0; c < 256 ; c ++)
	{
		// load char
		error = FT_Load_Char( ci->face, (unsigned char) c , FT_LOAD_RENDER | FT_LOAD_MONOCHROME );
		if(error)
		{
			veejay_msg(VEEJAY_MSG_ERROR,"Cannot load char '%c' \n", (unsigned char)c);
			sprintf(ci->font, "%s", DEFAULT_FONT);
			return 0;
		}
		if(error) continue;

		ci->bitmaps[c] = ci->face->glyph->bitmap;
		// save bitmap left
		ci->bitmap_left[c] = ci->face->glyph->bitmap_left;
		// save bitmap top
		ci->bitmap_top[c] = ci->face->glyph->bitmap_top;
		// save advance
		ci->advance[c] = ci->face->glyph->advance.x >> 6;
		// save glyph
		error = FT_Get_Glyph( ci->face->glyph, &(ci->glyphs[c]) );

		// save glyph index
		ci->glyphs_index[c] = FT_Get_Char_Index( ci->face, (unsigned char) c );
		// measure text height to calculate text_height (or the maximum text height)
		FT_Glyph_Get_CBox( ci->glyphs[ c ] , ft_glyph_bbox_pixels, &bbox);

		if( bbox.yMax > yMax ) yMax = bbox.yMax;
		if( bbox.yMin < yMin ) yMin = bbox.yMin;
	}

	ci->text_height = yMax - yMin;
	ci->baseline = yMax;


	return 1;
}

int Init(void **ctxp)
{
	ContextInfo *ci = NULL;
	*ctxp = malloc(sizeof(ContextInfo));
	if( ! *ctxp ) return 0;

	ci = (ContextInfo*) *ctxp;
	memset(ci, 0, sizeof(ContextInfo));

	ci->text = (unsigned char*) malloc(sizeof(unsigned char) * 1024);
	ci->font = (unsigned char*) malloc(sizeof(unsigned char) * 1024);
	ci->file = NULL;
	ci->x = 0;
	ci->y = 0;
	ci->fgcolor[0] = 255;
	ci->fgcolor[1] = 128;
	ci->fgcolor[2] = 128;
	ci->bgcolor[0] = 16;
	ci->bgcolor[1] = 128;
	ci->bgcolor[2] = 128;
	ci->bg = 0;
	ci->outline = 0;
	ci->text_height = 0;
	ci->info = (VJPluginInfo*)malloc(sizeof(VJPluginInfo)) ; 
	ci->info->name = strdup("DrawText");
	ci->info->help = strdup("Draws a Text on the screen");
	ci->info->plugin_type = VJPLUG_NORMAL;
	ci->size = 40;

	bzero(ci->text, 1024);
	bzero(ci->font, 1024);
	if( !ci->text )	
	{
		veejay_msg(VEEJAY_MSG_ERROR,"No text provided (-t text)\n");
		return -1;
	}
	if ( ci->file )
	{
		FILE *fp;
		if( (fp=fopen(ci->file, "r")) == NULL )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "The file could not be opened.\n");
			return 0; 
		}
	}
	sprintf(ci->font, "%s",DEFAULT_FONT);

	ci->font_table = (char**) malloc(sizeof(char*) * MAX_FONTS );
	ci->font_index =0;
	ci->rand_font = 0;

	find_fonts(ci,"/usr/X11R6/lib/fonts/TTF");
	find_fonts(ci,"/usr/X11R6/lib/fonts/Type1");
	find_fonts(ci,"/usr/X11R6/lib/truetype");
	find_fonts(ci,"/usr/X11R6/lib/TrueType");
	find_fonts(ci,"/usr/X11R6/lib/75dpi");
	find_fonts(ci,"/usr/X11R6/lib/cyrillic");
	find_fonts(ci,"~/veejay-fonts");
	if(!configure((void*)ci, ci->size))
	{
		Free(ci);
		return 0;
	}

	print_fonts(ci);

	return 1;

}

void draw_glyph( 
	VJFrameInfo *info,
	VJFrame *picture,
	FT_Bitmap *bitmap,
	unsigned int x,
	unsigned int y,
	unsigned int width,
	unsigned int height,	
	unsigned char yuv_fgcolor[3],
	unsigned char yuv_bgcolor[3],
	int outline)
{

  int r, c;
  int spixel, dpixel[3], in_glyph=0;
	dpixel[2] = 128; dpixel[1] = 128;


	uint8_t *Y = picture->data[0];
	uint8_t *U = picture->data[1];
	uint8_t *V = picture->data[2];


  if (bitmap->pixel_mode == ft_pixel_mode_mono)
    {
      in_glyph = 0;
      for (r=0; (r < bitmap->rows) && (r+y < height); r++)
	{
	  for (c=0; (c < bitmap->width) && (c+x < width); c++)
	    {
	      /* pixel in the picture (destination) */
	      dpixel[0] = Y[ (c+x) + ((y+r)*info->width)];
	      dpixel[1] = U[ ((c+x)>>picture->shift_h) + (((y+r)>>picture->shift_v) * picture->uv_width)];
	      dpixel[2] = V[ ((c+x)>>picture->shift_h) + (((y+r)>>picture->shift_v) * picture->uv_width)];

	      /* pixel in the glyph bitmap (source) */
	      spixel = bitmap->buffer[r*bitmap->pitch +c/8] & (0x80>>(c%8)); 
	      
	      if (spixel) 
	      {
	     		dpixel[0] = yuv_fgcolor[0];
			dpixel[1] = yuv_fgcolor[1];
			dpixel[2] = yuv_fgcolor[2];
	      } 

	      if (outline)
		{
		  /* border detection: */	      
		  if ( (!in_glyph) && (spixel) )
		    /* left border detected */
		    {
		      in_glyph = 1;
		      /* draw left pixel border */
		      if (c-1 >= 0)
		      {
				Y[ (c+x-1) + ((y+r) * info->width )] = yuv_bgcolor[0];	
				U[ ((c+x-1) >> picture->shift_h) + ((y+r) >> picture->shift_v) * picture->uv_width] = yuv_bgcolor[1];
				V[ ((c+x-1) >> picture->shift_h) + ((y+r) >> picture->shift_v) * picture->uv_width] = yuv_bgcolor[2]; 
		      }
		    }
		  else if ( (in_glyph) && (!spixel) )
		    /* right border detected */
		    {
		      in_glyph = 0;
		      /* 'draw' right pixel border */
		      dpixel[0] = yuv_bgcolor[0];
		      dpixel[1] = yuv_bgcolor[1];
		      dpixel[2] = yuv_bgcolor[2];
		    }
		 
		  if (in_glyph) 
		    /* see if we have a top/bottom border */
		    {
		      /* top */
		      if ( (r-1 >= 0) && (! bitmap->buffer[(r-1)*bitmap->pitch +c/8] & (0x80>>(c%8))) )
			{
			/* we have a top border */
			Y[ (c+x) + ((y+r-1) * info->width )] = yuv_bgcolor[0];	
			U[ ((c+x) >> picture->shift_h) + ((y+r-1) >> picture->shift_v) * picture->uv_width] = yuv_bgcolor[1];
			V[ ((c+x) >> picture->shift_h) + ((y+r-1) >> picture->shift_v) * picture->uv_width] = yuv_bgcolor[2]; 

			}
		      /* bottom */
		      if ( (r+1 < height) && (! bitmap->buffer[(r+1)*bitmap->pitch +c/8] & (0x80>>(c%8))) )
		      {
			/* we have a bottom border */
			Y[ (c+x) + ((y+r+1) * info->width )] = yuv_bgcolor[0];	
			U[ ((c+x) >> picture->shift_h) + ((y+r+1) >> picture->shift_v) * picture->uv_width] = yuv_bgcolor[1];
			V[ ((c+x) >> picture->shift_h) + ((y+r+1) >> picture->shift_v) * picture->uv_width] = yuv_bgcolor[2]; 


		      }
		    }
		}
		Y[ (c+x) + ((y+r) * info->width )] = dpixel[0];	
		U[ ((c+x) >> picture->shift_h) + ((y+r) >> picture->shift_v) * picture->uv_width] = dpixel[1];
		V[ ((c+x) >> picture->shift_h) + ((y+r) >> picture->shift_v) * picture->uv_width] = dpixel[2]; 
	    }
	}
    }
}


inline void draw_box(VJFrameInfo *info, VJFrame *picture, unsigned int x, unsigned int y, unsigned int width, unsigned int height, unsigned char yuv_color[3])
{
  int i, j;

  for (j = 0; (j < height); j++)
    for (i = 0; (i < width); i++) 
      {	
	picture->data[0][ (i+x) + ((y+j) * info->width )] = yuv_color[0];	
	picture->data[1][ ((i+x) >> picture->shift_h) + ((y+j) >> picture->shift_v) * picture->uv_width] = yuv_color[1];
	picture->data[2][ ((i+x) >> picture->shift_h) + ((y+j) >> picture->shift_v) * picture->uv_width] = yuv_color[2]; 
      }
  
}

#define MAXSIZE_TEXT 1024


void Event(void *ctx, const char *tokens)
{
	char tmp[1024];
	int num = 0;
	int post_init = 0;
	ContextInfo *ci = (ContextInfo*) ctx;
	bzero(tmp, 1024);

	
	if(OptionParse( "%d", &num, "size", tokens))
	{
		post_init = 1;
	}

	if(OptionParse("%d", &num, "font", tokens))
	{
		if(num>=0 && num < MAX_FONTS)
		{
			char *fnt  = select_font( ci,num );
			if(fnt!=NULL)
				sprintf(ci->font, "%s",fnt);
		}
		post_init = 1;
	}

	if(post_init)
	{
		if(!configure( (void*) ci, num  ))
		{
			configure( (void*)ci, 16);
		}
	}

//	printf("Parse tokens [%s]\n", tokens);
	if(OptionParse("%s", tmp, "text",tokens))
	{
		bzero(ci->text, 1024);
		strcpy(ci->text,tmp);
		bzero(tmp,1024);
	}

	if(OptionParse("%d", &num, "x", tokens))
	{
		ci->x = num;
	}
	if(OptionParse("%d", &num, "y", tokens))
	{
		ci->y = num;
	}
	if(OptionParse("%d", &num, "textheight",tokens))
	{
		ci->text_height = num;
	}
	if(OptionParse("%e", &num, "baseline", tokens))
	{
		ci->baseline = num;
	}

	if(OptionParse(NULL, &num, "border", tokens))
	{
		ci->outline = num;
	}
	if(OptionParse("%d", &num, "rand",tokens))
	{ 
		ci->rand_font = num;
	}
	if(OptionParse(NULL, &num, "bg", tokens))
	{
		ci->bg = num;
	}
	if(OptionParse("%s", &tmp, "bgcolor", tokens))
	{
		if(ParseColor( tmp, &(ci->bgcolor[0]), &(ci->bgcolor[1]), &(ci->bgcolor[2]) )<0)
		 veejay_msg(VEEJAY_MSG_ERROR, "Use hexdecimal notation (bgcolor=\"#ffffff\" ([%s])) ",tmp );
		bzero(tmp,1024);
	}
	if(OptionParse("%s", &tmp, "fgcolor", tokens))
	{
		if(ParseColor( tmp, &(ci->fgcolor[0]) , &(ci->fgcolor[1]) , &(ci->fgcolor[2]) )<0)
		 veejay_msg(VEEJAY_MSG_ERROR, "Use hexadecimal notation (fgcolor=\"#000000\") ([%s]) ",tmp);
		bzero(tmp,1024);
	}


}


void Process(void *ctx, void *_info, void *_picture)
{
  ContextInfo *ci = (ContextInfo *) ctx;
  FT_Face face = ci->face;
  FT_GlyphSlot  slot = face->glyph;  
  unsigned char *text = ci->text;
  unsigned char c;
  int x = 0, y = 0, i=0, size=0;
  unsigned char buff[MAXSIZE_TEXT];
  unsigned char tbuff[MAXSIZE_TEXT];
  time_t now = time(0);
  int str_w, str_w_max;

  VJFrameInfo *info = (VJFrameInfo*) _info;
  VJFrame *picture = (VJFrame*) _picture;

  int width = info->width;
  int height = info->height;

  FT_Vector pos[MAXSIZE_TEXT];  
  FT_Vector delta;

  if (ci->file) 
    {
      int fd = open(ci->file, O_RDONLY);
      
      if (fd < 0) 
	{
	  text = ci->text;
	  perror("WARNING: the file could not be opened. Using text provided with -t switch. ");
	} 
      else 
	{
	  int l = read(fd, tbuff, sizeof(tbuff) - 1);
	  
	  if (l >= 0) 
	    {
	      tbuff[l] = 0;
	      text = tbuff;
	    } 
	  else 
	    {
	      text = ci->text;
	      perror("WARNING: the file could not be opened. Using text provided with -t switch. ");
	    }
	  close(fd);
	}
    }
  else
    {
      text = ci->text;
    }

  strftime(buff, sizeof(buff), text, localtime(&now));

  text = buff;

  size = strlen(text);
  
  /* measure string size and save glyphs position*/
  str_w = str_w_max = 0;
  x = ci->x; 
  y = ci->y;


  for (i=0; i < size; i++)
    {
      c = text[i];

      /* kerning */
      if ( (ci->use_kerning) && (i > 0) && (ci->glyphs_index[c]) )
	{
	  FT_Get_Kerning( ci->face, 
			  ci->glyphs_index[ text[i-1] ], 
			  ci->glyphs_index[c],
			  ft_kerning_default, 
			  &delta );
	  
	  x += delta.x >> 6;
	}
      
      if (( (x + ci->advance[ c ]) >= width ) || ( c == '\n' ))
	{
	  str_w = width - ci->x - 1;

	  y += ci->text_height;
	  x = ci->x;
	}


      /* save position */
      pos[i].x = x + ci->bitmap_left[c];
      pos[i].y = y - ci->bitmap_top[c] + ci->baseline;


      x += ci->advance[c];


      if (str_w > str_w_max)
	str_w_max = str_w;

    }

  


  if (ci->bg)
    {
      /* Check if it doesn't pass the limits */
      if ( str_w_max + ci->x >= width )
	str_w_max = width - ci->x - 1;
      if ( y >= height )
	y = height - 1 - 2*ci->y;

      /* Draw Background */
      draw_box( info, picture, ci->x, ci->y, str_w_max, y - ci->y, ci->bgcolor );    
    }



  /* Draw Glyphs */
  for (i=0; i < size; i++)
    {
      c = text[i];

      if (
	  ( (c == '_') && (text == ci->text) ) || /* skip '_' (consider as space) 
						     IF text was specified in cmd line 
						     (which doesn't like neasted quotes)  */
	  ( c == '\n' ) /* Skip new line char, just go to new line */
	  )
	continue;

	/* now, draw to our target surface */

	draw_glyph( info,
		    picture, 
		    &(ci->bitmaps[ c ]),
		    pos[i].x,
		    pos[i].y,
		    width, 
		    height,
		    ci->fgcolor,
		    ci->bgcolor,
		    ci->outline );
		    
      /* increment pen position */
      x += slot->advance.x >> 6;
    }


}

#endif
