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
#include <config.h>
#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include "vj-server.h"
#include "vj-client.h"
#include "vj-global.h"

void vj_client_free(vj_client * vjc)
{
    close(vjc->handle);

    if (vjc)
	free(vjc);
    vjc = NULL;
}

vj_client *vj_client_new(char *name, int port)
{
    //int status = 1;
    vj_client *vjc = (vj_client *) malloc(sizeof(vj_client));
    if (!vjc)
	return NULL;
    vjc->he = gethostbyname(name);
    vjc->handle = socket(AF_INET, SOCK_STREAM, 0);

    vjc->server_addr.sin_family = AF_INET;
    vjc->server_addr.sin_port = htons(port);
    vjc->server_addr.sin_addr = *((struct in_addr *) vjc->he->h_addr);

    if (connect(vjc->handle, (struct sockaddr *) &vjc->server_addr,
		sizeof(struct sockaddr)) == -1) {

	printf("Cannot connect to host %s port %d\n", name, port);
	vj_client_free(vjc);
	return NULL;
    } else {
	printf("Connected to host %s port %d\n", name, VJ_PORT);
    }
    return vjc;
}

int vj_client_read(vj_client * vjc, char *msg)
{
    int numbytes = 0;
    int n;
    if (ioctl(vjc->handle, FIONREAD, &numbytes) == -1)
	return -1;

    n = recv(vjc->handle, msg, numbytes, 0);

    return n;
}

int vj_client_send(vj_client * vjc, char *msg)
{
    int total = 0;
    int bytes_left;
    int n;
    char block[MESSAGE_SIZE];
    sprintf(block, "V%03dD%s", strlen(msg), msg);
    bytes_left = strlen(block);
    if (bytes_left <= 0)
	return -1;
    while (total < bytes_left) {
	n = send(vjc->handle, block + total, bytes_left, 0);
	if (n == -1)
	    return -1;
	total += n;
	bytes_left -= n;
    }
    return 0;
}
