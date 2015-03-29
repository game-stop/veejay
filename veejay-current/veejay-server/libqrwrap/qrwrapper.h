/* 
 * Linux VeeJay
 *
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
#ifndef QRWRAPPER
#define QRWRAPPER
int	qrwrap_encode_string(const char *outifle, const char *str );
void	qrwrap_free();
void	qrwrap_draw( VJFrame *out, int port_num, const char *homedir, int qr_w, int qr_h, int qr_fmt );
void	qrbitcoin_draw( VJFrame *out, const char *homedir,int qr_w, int qr_h, int qr_fmt );

#endif
