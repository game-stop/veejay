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
#ifndef VEEJAY_DIRECTOR_WIRE_H
#define VEEJAY_DIRECTOR_WIRE_H

#include <stddef.h>

typedef struct {
    int fd;
} DirectorWire;

void director_wire_init(DirectorWire *wire);
void director_wire_close(DirectorWire *wire);
int director_wire_connect(DirectorWire *wire,
                          const char *host,
                          int port,
                          int timeout_ms);
int director_wire_send(DirectorWire *wire,
                       const char *command,
                       int timeout_ms);
int director_wire_query(DirectorWire *wire,
                        const char *command,
                        char *response,
                        size_t response_size,
                        int timeout_ms,
                        int idle_ms);
int director_wire_query_timed(DirectorWire *wire,
                              const char *command,
                              char *response,
                              size_t response_size,
                              int timeout_ms,
                              int idle_ms,
                              double *first_response_ms);
int director_wire_query_framed(DirectorWire *wire,
                               const char *command,
                               char *response,
                               size_t response_size,
                               size_t header_digits,
                               int timeout_ms);
int director_wire_query_preview(DirectorWire *wire,
                                const char *command,
                                unsigned char **payload,
                                size_t *payload_size,
                                int *width,
                                int *height,
                                int *full_range,
                                int timeout_ms);

#endif
