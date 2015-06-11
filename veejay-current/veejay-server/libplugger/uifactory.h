/*
 * Copyright (C) 2002-2006 Niels Elburg <nwelburg@gmail.com>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */


#ifndef UIFAC
#define UIFAC

//@ returns window name , creates sample
char *		vevosample_construct_ui(void *sample);
//@ create fx chain (all underlying windows)
void		vevosample_construct_ui_fx_chain(void *sample);

//@ create window for some fx slot
void		vevosample_ui_construct_fx_window( void *sample, int k );

//@ create window for some bind slot
void		vevosample_ui_construct_fx_bind_window( void *sample, int k , int i);

void		vevosample_ui_new_vframe(
			void *sample,
			const char *window,
			const char *frame,
			const char *label );

void		vevosample_ui_new_frame(
			void *sample,
			const char *window,
			const char *frame,
			const char *label );

void		vevosample_ui_new_button(
			void *sample,
			const char *window,
			const char *frame,
			const char *label,
			const char *path,
			const char *tooltip );

void		vevosample_ui_new_label(
			void *sample,
			const char *window,
			const char *frame,
			const char *label );	

void		vevosample_ui_new_numeric(
			void *sample,
			const char *window,
			const char *frame,
			const char *label,
			double min,
			double max,
			double value,
			int wrap,
			int extra,
			const char *widget_name,
			const char *path,
			const char *format,
			const char *tooltip);

void		vevosample_ui_new_radiogroup(
			void *osc,
			const char *window,
			const char *framename,	
			const char *prefix,
			const char *label_prefix,
			int n_buttons,
			int active_button );

void		vevosample_ui_new_switch(
			void *sample,
			const char *window,
			const char *framename,
			const char *widget,
			const char *label,	
			int active,
			const char *path,
			const char *tooltip);
#endif
