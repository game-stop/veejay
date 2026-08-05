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
#include "director-wire.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define DIRECTOR_WIRE_SHORT_MAX 999
#define DIRECTOR_WIRE_HEADER_SIZE 5

static int64_t director_wire_now_us(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

static int64_t director_wire_now_ms(void)
{
    return director_wire_now_us() / 1000;
}

void director_wire_init(DirectorWire *wire)
{
    if(wire)
        wire->fd = -1;
}

void director_wire_close(DirectorWire *wire)
{
    if(!wire || wire->fd < 0)
        return;
    shutdown(wire->fd, SHUT_RDWR);
    close(wire->fd);
    wire->fd = -1;
}

static int director_wire_wait(int fd, short events, int timeout_ms, short *revents)
{
    struct pollfd item;
    int result;

    item.fd = fd;
    item.events = events;
    item.revents = 0;
    do {
        result = poll(&item, 1, timeout_ms);
    } while(result < 0 && errno == EINTR);

    if(revents)
        *revents = item.revents;
    return result > 0;
}

static int director_wire_socket_connected(int fd)
{
    int error = 0;
    socklen_t length = sizeof(error);
    return getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 &&
           error == 0;
}

int director_wire_connect(DirectorWire *wire,
                          const char *host,
                          int port,
                          int timeout_ms)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    char port_text[16];
    int fd = -1;

    if(!wire || !host || !*host || port < 1 || port > 65535)
        return 0;

    director_wire_close(wire);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    snprintf(port_text, sizeof(port_text), "%d", port);

    if(getaddrinfo(host, port_text, &hints, &addresses) != 0)
        return 0;

    for(address = addresses; address; address = address->ai_next) {
#ifdef SOCK_CLOEXEC
        fd = socket(address->ai_family,
                    address->ai_socktype | SOCK_CLOEXEC,
                    address->ai_protocol);
#else
        fd = socket(address->ai_family,
                    address->ai_socktype,
                    address->ai_protocol);
#endif
        if(fd < 0)
            continue;

#ifndef SOCK_CLOEXEC
        {
            int descriptor_flags = fcntl(fd, F_GETFD, 0);
            if(descriptor_flags >= 0)
                fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC);
        }
#endif
        {
            int flags = fcntl(fd, F_GETFL, 0);
            if(flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                close(fd);
                fd = -1;
                continue;
            }
        }

        if(connect(fd, address->ai_addr, address->ai_addrlen) == 0)
            break;

        if(errno == EINPROGRESS) {
            short revents = 0;
            if(director_wire_wait(fd, POLLOUT, timeout_ms, &revents) &&
               !(revents & (POLLERR | POLLHUP | POLLNVAL)) &&
               director_wire_socket_connected(fd))
                break;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(addresses);
    if(fd < 0)
        return 0;

    {
        int enabled = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled));
    }
    wire->fd = fd;
    return 1;
}

static int director_wire_send_all(int fd,
                                  const unsigned char *data,
                                  size_t length,
                                  int timeout_ms)
{
    size_t sent = 0;
    int64_t deadline = director_wire_now_ms() + timeout_ms;

    while(sent < length) {
        ssize_t count = send(fd, data + sent, length - sent, MSG_NOSIGNAL);
        if(count > 0) {
            sent += (size_t)count;
            continue;
        }
        if(count < 0 && errno == EINTR)
            continue;
        if(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            int64_t remaining = deadline - director_wire_now_ms();
            short revents = 0;
            if(remaining <= 0 ||
               !director_wire_wait(fd, POLLOUT, (int)remaining, &revents) ||
               (revents & (POLLERR | POLLHUP | POLLNVAL)))
                return 0;
            continue;
        }
        return 0;
    }
    return 1;
}

int director_wire_send(DirectorWire *wire,
                       const char *command,
                       int timeout_ms)
{
    char header[DIRECTOR_WIRE_HEADER_SIZE + 1];
    size_t length;

    if(!wire || wire->fd < 0 || !command || !*command)
        return 0;
    length = strlen(command);
    if(length > DIRECTOR_WIRE_SHORT_MAX)
        return 0;

    snprintf(header, sizeof(header), "V%03uD", (unsigned)length);
    return director_wire_send_all(wire->fd,
                                  (const unsigned char*)header,
                                  DIRECTOR_WIRE_HEADER_SIZE,
                                  timeout_ms) &&
           director_wire_send_all(wire->fd,
                                  (const unsigned char*)command,
                                  length,
                                  timeout_ms);
}

static int director_wire_receive(DirectorWire *wire,
                                 char *response,
                                 size_t response_size,
                                 int timeout_ms,
                                 int idle_ms,
                                 int64_t *first_byte_us)
{
    size_t used = 0;
    int received = 0;
    int64_t deadline = director_wire_now_ms() + timeout_ms;
    int64_t idle_deadline = 0;

    if(first_byte_us)
        *first_byte_us = 0;

    if(!wire || wire->fd < 0 || !response || response_size < 2)
        return 0;
    response[0] = '\0';

    while(director_wire_now_ms() < deadline) {
        int64_t now = director_wire_now_ms();
        int64_t target = received && idle_deadline < deadline ? idle_deadline : deadline;
        int64_t remaining = target - now;
        short revents = 0;

        if(remaining <= 0)
            break;
        if(!director_wire_wait(wire->fd, POLLIN, (int)remaining, &revents))
            continue;

        if(revents & POLLIN) {
            for(;;) {
                ssize_t count;
                if(used + 1 >= response_size)
                    return 0;
                count = recv(wire->fd,
                             response + used,
                             response_size - used - 1,
                             MSG_DONTWAIT);
                if(count > 0) {
                    if(!received && first_byte_us)
                        *first_byte_us = director_wire_now_us();
                    used += (size_t)count;
                    response[used] = '\0';
                    received = 1;
                    idle_deadline = director_wire_now_ms() + idle_ms;
                    continue;
                }
                if(count == 0)
                    return 0;
                if(errno == EINTR)
                    continue;
                if(errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return 0;
            }
        }

        if(revents & (POLLERR | POLLNVAL))
            return 0;
        if((revents & POLLHUP) && !(revents & POLLIN))
            return 0;
    }

    return received;
}

int director_wire_query(DirectorWire *wire,
                        const char *command,
                        char *response,
                        size_t response_size,
                        int timeout_ms,
                        int idle_ms)
{
    if(!director_wire_send(wire, command, timeout_ms))
        return 0;
    return director_wire_receive(wire,
                                 response,
                                 response_size,
                                 timeout_ms,
                                 idle_ms,
                                 NULL);
}

int director_wire_query_timed(DirectorWire *wire,
                              const char *command,
                              char *response,
                              size_t response_size,
                              int timeout_ms,
                              int idle_ms,
                              double *first_response_ms)
{
    int64_t first_byte_us = 0;
    const int64_t started_us = director_wire_now_us();
    if(first_response_ms)
        *first_response_ms = 0.0;
    if(!director_wire_send(wire, command, timeout_ms) ||
       !director_wire_receive(wire, response, response_size, timeout_ms, idle_ms,
                              &first_byte_us))
        return 0;
    if(first_response_ms && first_byte_us >= started_us)
        *first_response_ms = (double)(first_byte_us - started_us) / 1000.0;
    return 1;
}


static int director_wire_receive_exact(DirectorWire *wire,
                                       unsigned char *buffer,
                                       size_t length,
                                       int timeout_ms)
{
    size_t used = 0;
    int64_t deadline;

    if(!wire || wire->fd < 0 || !buffer || length == 0)
        return 0;

    deadline = director_wire_now_ms() + timeout_ms;
    while(used < length) {
        ssize_t count = recv(wire->fd, buffer + used, length - used, MSG_DONTWAIT);
        if(count > 0) {
            used += (size_t)count;
            continue;
        }
        if(count == 0)
            return 0;
        if(errno == EINTR)
            continue;
        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            int64_t remaining = deadline - director_wire_now_ms();
            short revents = 0;
            if(remaining <= 0 ||
               !director_wire_wait(wire->fd, POLLIN, (int)remaining, &revents) ||
               (revents & (POLLERR | POLLNVAL)) ||
               ((revents & POLLHUP) && !(revents & POLLIN)))
                return 0;
            continue;
        }
        return 0;
    }
    return 1;
}

int director_wire_query_framed(DirectorWire *wire,
                               const char *command,
                               char *response,
                               size_t response_size,
                               size_t header_digits,
                               int timeout_ms)
{
    unsigned char header[17];
    size_t length = 0;

    if(!wire || !command || !response || response_size < 2 ||
       header_digits == 0 || header_digits >= sizeof(header))
        return 0;
    response[0] = '\0';

    if(!director_wire_send(wire, command, timeout_ms) ||
       !director_wire_receive_exact(wire, header, header_digits, timeout_ms))
        return 0;
    header[header_digits] = '\0';
    for(size_t i = 0; i < header_digits; i++) {
        if(header[i] < '0' || header[i] > '9')
            return 0;
        length = length * 10u + (size_t)(header[i] - '0');
    }
    if(length >= response_size)
        return 0;
    if(length == 0) {
        response[0] = '\0';
        return 1;
    }
    if(!director_wire_receive_exact(wire, (unsigned char*)response, length, timeout_ms))
        return 0;
    response[length] = '\0';
    return 1;
}

int director_wire_query_preview(DirectorWire *wire,
                                const char *command,
                                unsigned char **payload,
                                size_t *payload_size,
                                int *width,
                                int *height,
                                int *full_range,
                                int timeout_ms)
{
    unsigned char header[14];
    size_t length = 0;
    int requested_width = 0;
    int requested_height = 0;
    int view_mode = 0;
    int frame_width = 0;
    int frame_height = 0;
    int range = 0;
    unsigned char *data;

    if(payload)
        *payload = NULL;
    if(payload_size)
        *payload_size = 0;
    if(width)
        *width = 0;
    if(height)
        *height = 0;
    if(full_range)
        *full_range = 0;

    if(!wire || !command || !payload || !payload_size ||
       !width || !height || !full_range)
        return 0;

    if(sscanf(command, "433:%d %d %d;",
              &requested_width, &requested_height, &view_mode) != 3 ||
       requested_width <= 0 || requested_height <= 0 ||
       requested_width > 4096 || requested_height > 4096 ||
       view_mode < 0 || view_mode > 2)
        return 0;

    if(!director_wire_send(wire, command, timeout_ms))
        return 0;

    if(view_mode == 2) {
        if(!director_wire_receive_exact(wire, header, 9, timeout_ms))
            return 0;
        header[9] = '\0';
        for(int i = 0; i < 8; i++) {
            if(header[i] < '0' || header[i] > '9')
                return 0;
            length = length * 10u + (size_t)(header[i] - '0');
        }
        if(header[8] != '0' && header[8] != '1')
            return 0;
        range = header[8] - '0';
        frame_width = requested_width;
        frame_height = requested_height;
    }
    else {
        int wire_width = 0;
        int wire_height = 0;
        if(!director_wire_receive_exact(wire, header, 13, timeout_ms))
            return 0;
        header[13] = '\0';
        if(sscanf((const char*)header, "%6zu%4d%2d%1d",
                  &length, &wire_width, &wire_height, &range) != 4)
            return 0;
        if(wire_width <= 0 || wire_width > 4096 ||
           wire_height < 0 || wire_height > 99 ||
           wire_width != requested_width ||
           wire_height != (requested_height % 100))
            return 0;
        frame_width = requested_width;
        frame_height = requested_height;
    }

    if(length > 16U * 1024U * 1024U ||
       (range != 0 && range != 1))
        return 0;
    if(length == 0) {
        *width = frame_width;
        *height = frame_height;
        *full_range = range;
        return 1;
    }

    data = (unsigned char*)malloc(length);
    if(!data)
        return 0;
    if(!director_wire_receive_exact(wire, data, length, timeout_ms)) {
        free(data);
        return 0;
    }

    *payload = data;
    *payload_size = length;
    *width = frame_width;
    *height = frame_height;
    *full_range = range;
    return 1;
}
