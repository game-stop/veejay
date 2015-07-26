#ifndef VJFONT_H
#define VJFONT_H
/*
 * Linux VeeJay
 *
 * Copyright(C)2002-2006 Niels Elburg < nwelburg@gmail.com >
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 *
 *
 */
void	*vj_font_single_init(int s_w, int s_h, float fps, char *home);

void	*vj_font_init(int s_w, int s_h, float fps, int osd);
int	vj_font_srt_sequence_exists( void *font, int id );
void vj_font_render(void *ctx, void *_picture, long nframe);
void	vj_font_destroy(void *ctx);
int	vj_font_load_srt( void *font, const char *filename );
int	vj_font_save_srt( void *font , const char *filename );
char	*vj_font_get_sequence( void *font, int seq );
void	*vj_font_get_plain_dict( void *font );
int	vj_font_prepare( void *font, long s1, long s2 );
void	vj_font_dictionary_destroy(void *font,void *dict);
int	vj_font_clear_text( void *font );
int	vj_font_new_text( void *font, unsigned char *text,  long s1,long s2, int seq);
void	vj_font_del_text( void *font, int seq );
char 	**vj_font_get_all_fonts( void *font );
void	vj_font_set_lncolor( void *font, int r, int g, int b, int a );
void	vj_font_set_fgcolor( void *font, int r, int g, int b, int a );
void	vj_font_set_bgcolor( void *font, int r, int g, int b, int a );
void	vj_font_set_outline_and_border( void *font, int outline, int border);
void	vj_font_set_position( void *font, int x, int y );
void	vj_font_set_size_and_font( void *font, int f_id, int size );
void    vj_font_set_dict( void *font, void *dict );
void	vj_font_set_osd_text(void *font, char *text );
void    *vj_font_get_dict(void *font);
void	vj_font_update_text( void *font, long s1,long s2, int seq, char *text);
char **vj_font_get_sequences( void *font );
char  *vj_font_get_sequence( void *font , int id );
void                  vj_font_set_current( void *font , int cur );
void    vj_font_xml_pack( xmlNodePtr node, void *font );
void    vj_font_xml_unpack( xmlDocPtr doc, xmlNodePtr node, void *font );
int   vj_font_norender(void *ctx, long position);
void	vj_font_render_osd_status( void *ctx, void *_picture, char *status_str, int placement );
char	*vj_font_default();
#endif
