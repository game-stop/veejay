 /*
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
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
 */

#ifndef VJ_SDL_FONT_INC
#define VJ_SDL_FONT_INC


void	*vj_sdl_font_init();
void	vj_sdl_font_free(void *font);
void*	vj_sdl_draw_log_line( void *f, uint8_t r, uint8_t g, uint8_t b, uint8_t a, const char *line);
void	vj_sdl_font_logging( void *f, uint8_t *planes[3], int w, int h );
int	vj_sdl_draw_to_buffer( void *f, unsigned int w, unsigned int h );
#endif
