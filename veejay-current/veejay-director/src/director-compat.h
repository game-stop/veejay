/* director - Linux VeeJay - GVeejay GTK+-3/Glade User Interface 
 * (C) 2026 Niels Elburg <nwelburg@gmail.com>
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
#ifndef DIRECTOR_COMPAT_H
#define DIRECTOR_COMPAT_H

#include <glib.h>
#include <string.h>

static inline gchar *director_utf8_make_valid_compat(const gchar *text, gssize len)
{
    const gchar *input = text ? text : "";
    const gssize length = len < 0 ? (gssize)strlen(input) : len;
    const gchar *end = input + length;

    if(g_utf8_validate(input, length, NULL))
        return g_strndup(input, (gsize)length);

    GString *result = g_string_sized_new((gsize)length + 8u);
    const gchar *p = input;
    while(p < end) {
        const gchar *invalid = NULL;
        const gssize remaining = end - p;
        if(g_utf8_validate(p, remaining, &invalid)) {
            g_string_append_len(result, p, remaining);
            break;
        }
        if(invalid && invalid > p)
            g_string_append_len(result, p, invalid - p);
        g_string_append(result, "\xEF\xBF\xBD");
        p = invalid && invalid < end ? invalid + 1 : p + 1;
    }
    return g_string_free(result, FALSE);
}

#endif
