/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nelburg@looze.net>
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
#ifndef VJ_CLIENT_H
#define VJ_CLIENT_H
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>

typedef struct {
    struct hostent *he;
    struct sockaddr_in server_addr;
    int handle;
} vj_client;


void vj_client_free(vj_client * vjc);
vj_client *vj_client_new(char *, int port);
//int vj_client_read(vj_client * vjc, char *msg);
int vj_client_send(vj_client * vjc, char *msg);
#endif
