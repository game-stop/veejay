/* sendVIMS - very simple client for VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nwelburg@gmail.com>
 *
 * puredata module by Tom Schouten <doelie@zzz.kotnet.org>
 *
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
#include <setjmp.h>

#include "m_pd.h"

#define POLL_INTERVAL_CONNECTED (5.0f)
#define POLL_INTERVAL_IDLE      (25.0f)
#define MAX_MSG                 256

#define SENDVIMS_STATUS_TOKENS    82  /* lock-step with vims.h / VIMS_STATUS_TOKENS */
#define SENDVIMS_STATUS_BODY_MAX  999 /* V%03dD protocol body-size limit */
#define SENDVIMS_HEADER_SIZE      5

#define QUEUE_SIZE (1 << 8)
#define QUEUE_MASK (QUEUE_SIZE - 1)

static t_symbol *s_disconnect = 0;
static t_symbol *s_veejay = 0;
static t_symbol *selector[602];

typedef struct {
    struct hostent *he;
    struct sockaddr_in server_addr;
    int handle;
} veejay_t;

typedef struct {
    int delay;
    char msg[MAX_MSG - sizeof(int)];
} vj_msg_t;

typedef struct {
    t_symbol *selector;
    int argc;
    t_atom argv[SENDVIMS_STATUS_TOKENS];
} pd_msg_t;

typedef struct {
    void *queue[QUEUE_SIZE];
    unsigned int read;
    unsigned int write;
    pthread_mutex_t lock;
} queue_t;

typedef struct {
    t_object obj;
    t_outlet *outlet;

    t_symbol *hostname;
    int port;
    veejay_t status_socket;
    veejay_t command_socket;

    queue_t vq;
    queue_t pq;
    t_clock *clock;

    pthread_t thread;
    jmp_buf errorhandler;

    int connected;
    int thread_started;
    volatile int run;
} sendVIMS_t;

static void msg_free(void *m)
{
    if (m)
        free(m);
}

static int sendVIMS_is_digit(char c)
{
    return (c >= '0' && c <= '9');
}

static int selector_map(t_symbol *s)
{
    const char *name = s->s_name;
    int i;

    if (name[0] == 'p' && sendVIMS_is_digit(name[1]))
        return atoi(name + 1);

    for (i = 0; i < 602; i++) {
        if (s == selector[i])
            return i;
    }

    post("sendVIMS: selector %s not recognized", s->s_name);
    return 0;
}

static void setup_selectors(void)
{
    memset(selector, 0, sizeof(selector));
#define SELECTOR(name, id) selector[id] = gensym(name)
#include "selectors.h"
#undef SELECTOR
}

static int sendVIMS_append(char **dst, char *end, const char *fmt, ...)
{
    va_list ap;
    int n;
    int left = (int)(end - *dst);

    if (left <= 0)
        return -1;

    va_start(ap, fmt);
    n = vsnprintf(*dst, (size_t)left, fmt, ap);
    va_end(ap);

    if (n < 0 || n >= left)
        return -1;

    *dst += n;
    return 0;
}

static int sendVIMS_parse_status_int(char **pp, int *out)
{
    char *p = *pp;
    long v = 0;
    int sign = 1;

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;

    if (*p == '\0' || *p == ';') {
        *pp = p;
        return 0;
    }

    if (*p == '-') {
        sign = -1;
        p++;
    }

    if (!sendVIMS_is_digit(*p)) {
        *pp = p;
        return -1;
    }

    while (sendVIMS_is_digit(*p)) {
        v = (v * 10) + (long)(*p - '0');
        p++;
    }

    *out = (int)(sign * v);
    *pp = p;
    return 1;
}

static pd_msg_t *pd_msg_new(char *msg)
{
    char *p = msg + SENDVIMS_HEADER_SIZE;
    pd_msg_t *m = (pd_msg_t *)malloc(sizeof(*m));
    int n = 0;

    if (!m)
        return NULL;

    m->selector = s_veejay;
    m->argc = 0;

    while (n < SENDVIMS_STATUS_TOKENS) {
        int v = 0;
        int r = sendVIMS_parse_status_int(&p, &v);

        if (r == 0)
            break;
        if (r < 0)
            break;

        SETFLOAT(m->argv + n, (float)v);
        n++;
    }

    m->argc = n;

    if (n != SENDVIMS_STATUS_TOKENS) {
        post("sendVIMS: parsed %d status tokens, expected %d",
             n, SENDVIMS_STATUS_TOKENS);
    }

    return m;
}

static vj_msg_t *vj_msg_new(t_symbol *sel, int argc, t_atom *argv)
{
    vj_msg_t *m = (vj_msg_t *)malloc(sizeof(*m));
    char *c;
    char *body;
    char *end;
    int body_len;

    if (!m)
        return NULL;

    m->delay = 0;
    m->msg[0] = '\0';

    if (sel == gensym("+")) {
        if (!argc || argv->a_type != A_FLOAT)
            goto error;

        m->delay = (int)argv->a_w.w_float;
        argc--;
        argv++;

        if (!argc || argv->a_type != A_SYMBOL)
            goto error;

        sel = argv->a_w.w_symbol;
        argc--;
        argv++;
    }

    c = m->msg;
    end = m->msg + sizeof(m->msg);

    if (sendVIMS_append(&c, end, "V000D") < 0)
        goto error;

    body = c;

    if (sendVIMS_append(&c, end, "%03d:", selector_map(sel)) < 0)
        goto error;

    while (argc > 0) {
        switch (argv->a_type) {
        case A_SYMBOL:
            if (sendVIMS_append(&c, end, "%s%s",
                                argv->a_w.w_symbol->s_name,
                                (argc > 1) ? " " : "") < 0)
                goto error;
            break;
        case A_FLOAT:
            if (sendVIMS_append(&c, end, "%d%s",
                                (int)argv->a_w.w_float,
                                (argc > 1) ? " " : "") < 0)
                goto error;
            break;
        default:
            goto error;
        }

        argc--;
        argv++;
    }

    if (sendVIMS_append(&c, end, ";") < 0)
        goto error;

    body_len = (int)(c - body);
    if (body_len < 0 || body_len > 999)
        goto error;

    m->msg[0] = 'V';
    m->msg[1] = (char)('0' + ((body_len / 100) % 10));
    m->msg[2] = (char)('0' + ((body_len / 10) % 10));
    m->msg[3] = (char)('0' + (body_len % 10));
    m->msg[4] = 'D';

    return m;

error:
    post("sendVIMS: parse error");
    msg_free(m);
    return NULL;
}

static void queue_init(queue_t *x)
{
    memset(x, 0, sizeof(*x));
    pthread_mutex_init(&x->lock, NULL);
}

static void queue_destroy(queue_t *x)
{
    pthread_mutex_destroy(&x->lock);
}

static void queue_write(queue_t *x, void *m)
{
    int full = 0;

    pthread_mutex_lock(&x->lock);

    if (((x->write - x->read) & QUEUE_MASK) == QUEUE_MASK) {
        full = 1;
    } else {
        x->queue[x->write] = m;
        x->write = (x->write + 1) & QUEUE_MASK;
    }

    pthread_mutex_unlock(&x->lock);

    if (full) {
        post("sendVIMS: message queue full: ignoring command");
        msg_free(m);
    }
}

static void *queue_read(queue_t *x)
{
    void *msg = NULL;

    pthread_mutex_lock(&x->lock);

    if (((x->write - x->read) & QUEUE_MASK) != 0) {
        msg = x->queue[x->read];
        x->queue[x->read] = NULL;
        x->read = (x->read + 1) & QUEUE_MASK;
    }

    pthread_mutex_unlock(&x->lock);
    return msg;
}

static void queue_clear(queue_t *x)
{
    void *m;

    while ((m = queue_read(x)) != NULL)
        msg_free(m);
}

static void sendVIMS_vq_write(sendVIMS_t *x, vj_msg_t *m)
{
    queue_write(&x->vq, (void *)m);
}

static vj_msg_t *sendVIMS_vq_read(sendVIMS_t *x)
{
    return (vj_msg_t *)queue_read(&x->vq);
}

static void sendVIMS_pq_write(sendVIMS_t *x, pd_msg_t *m)
{
    queue_write(&x->pq, (void *)m);
}

static pd_msg_t *sendVIMS_pq_read(sendVIMS_t *x)
{
    return (pd_msg_t *)queue_read(&x->pq);
}

static void sendVIMS_close_socket(veejay_t *v)
{
    if (v->handle >= 0) {
        close(v->handle);
        v->handle = -1;
    }
}

static void sendVIMS_shutdown_socket(veejay_t *v)
{
    if (v->handle >= 0)
        shutdown(v->handle, SHUT_RDWR);
}

static int vj_connect(veejay_t *v, const char *name, int port_id)
{
    struct hostent he_buf;
    struct hostent *he_result = NULL;
    char he_data[1024];
    int herrno;

    v->handle = -1;

    if (gethostbyname_r(name, &he_buf, he_data, sizeof(he_data),
                        &he_result, &herrno) != 0 || he_result == NULL)
        return -1;

    v->handle = socket(AF_INET, SOCK_STREAM, 0);
    if (v->handle < 0)
        return -1;

    memset(&v->server_addr, 0, sizeof(v->server_addr));
    v->server_addr.sin_family = AF_INET;
    v->server_addr.sin_port = htons(port_id);
    v->server_addr.sin_addr = *(struct in_addr *)he_result->h_addr;

    if (connect(v->handle, (struct sockaddr *)&v->server_addr,
                sizeof(v->server_addr)) == -1) {
        sendVIMS_close_socket(v);
        return -1;
    }

    return 0;
}

static void sendVIMS_disconnect_from_thread(sendVIMS_t *x)
{
    pd_msg_t *m = (pd_msg_t *)malloc(sizeof(*m));

    if (m) {
        m->selector = s_disconnect;
        m->argc = 0;
        sendVIMS_pq_write(x, m);
    }

    longjmp(x->errorhandler, -1);
}

static int sendVIMS_recv_all(int fd, char *buf, int len)
{
    int total = 0;

    while (total < len) {
        int r = (int)recv(fd, buf + total, (size_t)(len - total), 0);

        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return (total > 0) ? total : r;

        total += r;
    }

    return total;
}

static int sendVIMS_send_all(int fd, const char *buf, int len)
{
    int total = 0;

    while (total < len) {
        int r = (int)send(fd, buf + total, (size_t)(len - total), 0);

        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;

        total += r;
    }

    return 0;
}

static int sendVIMS_parse_header(const char *header, int *size)
{
    if (header[0] != 'V' || header[4] != 'D')
        return -1;
    if (!sendVIMS_is_digit(header[1]) ||
        !sendVIMS_is_digit(header[2]) ||
        !sendVIMS_is_digit(header[3]))
        return -1;

    *size = ((header[1] - '0') * 100) +
            ((header[2] - '0') * 10) +
             (header[3] - '0');

    return 0;
}

static pd_msg_t *sendVIMS_status(sendVIMS_t *x)
{
    int gotbytes;
    int wantbytes;
    int size = -1;
    char header[SENDVIMS_HEADER_SIZE + 1];
    char buf[SENDVIMS_HEADER_SIZE + SENDVIMS_STATUS_BODY_MAX + 1];

    wantbytes = SENDVIMS_HEADER_SIZE;
    gotbytes = sendVIMS_recv_all(x->status_socket.handle, header, wantbytes);
    if (wantbytes != gotbytes)
        goto error;

    header[SENDVIMS_HEADER_SIZE] = '\0';

    if (sendVIMS_parse_header(header, &size) != 0)
        goto proto_error;
    if (size < 0 || size > SENDVIMS_STATUS_BODY_MAX)
        goto proto_error;

    memcpy(buf, header, SENDVIMS_HEADER_SIZE);

    wantbytes = size;
    gotbytes = sendVIMS_recv_all(x->status_socket.handle,
                                 buf + SENDVIMS_HEADER_SIZE,
                                 wantbytes);
    if (wantbytes != gotbytes)
        goto error;

    buf[SENDVIMS_HEADER_SIZE + size] = '\0';
    return pd_msg_new(buf);

error:
    if (x->run) {
        if (gotbytes > 0) {
            post("sendVIMS: message truncated: wanted %d bytes, got %d",
                 wantbytes, gotbytes);
        } else if (gotbytes == 0) {
            post("sendVIMS: remote end closed connection");
        } else {
            perror("sendVIMS");
        }
    }
    return NULL;

proto_error:
    post("sendVIMS: protocol error: not a valid veejay status packet");
    return NULL;
}

static void sendVIMS_flush(sendVIMS_t *x, int frames)
{
    while (frames-- > 0 && x->run) {
        pd_msg_t *m = sendVIMS_status(x);

        if (!m) {
            sendVIMS_disconnect_from_thread(x);
            return;
        }

        sendVIMS_pq_write(x, m);
    }
}

static void sendVIMS_send(sendVIMS_t *x, const char *buf)
{
    if (sendVIMS_send_all(x->command_socket.handle, buf, (int)strlen(buf)) < 0) {
        perror("sendVIMS: can't send");
        sendVIMS_disconnect_from_thread(x);
    }
}

static void sendVIMS_init(sendVIMS_t *x)
{
    memset(&x->status_socket, 0, sizeof(x->status_socket));
    memset(&x->command_socket, 0, sizeof(x->command_socket));

    x->status_socket.handle = -1;
    x->command_socket.handle = -1;

    queue_init(&x->vq);
    queue_init(&x->pq);

    x->hostname = gensym("localhost");
    x->port = 3490;
    x->connected = 0;
    x->thread_started = 0;
    x->run = 0;
}

static void sendVIMS_disconnect(sendVIMS_t *x)
{
    if (x->connected || x->run) {
        x->run = 0;
        sendVIMS_shutdown_socket(&x->status_socket);
        sendVIMS_shutdown_socket(&x->command_socket);
    }

    if (x->thread_started) {
        pthread_join(x->thread, NULL);
        x->thread_started = 0;
    }

    if (x->connected) {
        sendVIMS_close_socket(&x->status_socket);
        sendVIMS_close_socket(&x->command_socket);
        x->connected = 0;
        post("sendVIMS: disconnected");
    } else {
        sendVIMS_close_socket(&x->status_socket);
        sendVIMS_close_socket(&x->command_socket);
    }

    queue_clear(&x->vq);
    queue_clear(&x->pq);
}

static void *sendVIMS_thread(void *arg)
{
    sendVIMS_t *x = (sendVIMS_t *)arg;
    vj_msg_t *m;

    if (setjmp(x->errorhandler))
        return NULL;

    while (x->run) {
        while ((m = sendVIMS_vq_read(x)) != NULL) {
            sendVIMS_flush(x, m->delay);
            sendVIMS_send(x, m->msg);
            msg_free(m);
        }

        sendVIMS_flush(x, 1);
    }

    return NULL;
}

static void sendVIMS_connect(sendVIMS_t *x, t_symbol *host, t_float fport)
{
    pthread_attr_t attr;
    int port = (int)fport;

    if (!port)
        port = 3490;

    sendVIMS_disconnect(x);

    if (vj_connect(&x->command_socket, host->s_name, port) == -1) {
        post("sendVIMS: can't connect to veejay at %s:%d (command port)",
             host->s_name, port);
        goto error;
    }

    if (vj_connect(&x->status_socket, host->s_name, port + 1) == -1) {
        post("sendVIMS: can't connect to veejay at %s:%d (status port)",
             host->s_name, port + 1);
        goto error;
    }

    x->connected = 1;
    x->run = 1;

    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_OTHER);

    if (pthread_create(&x->thread, &attr, sendVIMS_thread, x) != 0) {
        pthread_attr_destroy(&attr);
        x->run = 0;
        x->connected = 0;
        post("sendVIMS: failed to start communication thread");
        goto error;
    }

    x->thread_started = 1;
    pthread_attr_destroy(&attr);

    post("sendVIMS: connected to %s:%d", host->s_name, port);
    return;

error:
    sendVIMS_disconnect(x);
}

static void sendVIMS_reconnect(sendVIMS_t *x)
{
    sendVIMS_disconnect(x);
    sendVIMS_connect(x, x->hostname, x->port);
}

static void sendVIMS_anything(sendVIMS_t *x, t_symbol *sel, int argc, t_atom *argv)
{
    vj_msg_t *m;

    if (!x->connected) {
        post("sendVIMS: not connected to veejay");
        return;
    }

    m = vj_msg_new(sel, argc, argv);
    if (m)
        sendVIMS_vq_write(x, m);
}

static t_class *sendVIMS_class = 0;

static void sendVIMS_free(sendVIMS_t *x)
{
    sendVIMS_disconnect(x);
    queue_destroy(&x->vq);
    queue_destroy(&x->pq);
    clock_free(x->clock);
}

static void sendVIMS_tick(sendVIMS_t *x)
{
    pd_msg_t *m;
    float interval = x->connected ? POLL_INTERVAL_CONNECTED : POLL_INTERVAL_IDLE;

    clock_delay(x->clock, interval);

    while ((m = sendVIMS_pq_read(x)) != NULL) {
        if (m->selector == s_veejay) {
            outlet_anything(x->outlet, gensym("list"), m->argc, m->argv);
        } else if (m->selector == s_disconnect) {
            sendVIMS_disconnect(x);
        }

        msg_free(m);
    }
}

static void *sendVIMS_new(t_symbol *moi, int argc, t_atom *argv)
{
    sendVIMS_t *x = (sendVIMS_t *)pd_new(sendVIMS_class);

    (void)moi;

    sendVIMS_init(x);

    x->clock = clock_new(x, (t_method)sendVIMS_tick);
    x->outlet = outlet_new(&x->obj, gensym("anything"));

    if (argc >= 1 && argv[0].a_type == A_SYMBOL)
        x->hostname = argv[0].a_w.w_symbol;

    if (argc >= 2 && argv[1].a_type == A_FLOAT)
        x->port = (int)argv[1].a_w.w_float;

    sendVIMS_connect(x, x->hostname, x->port);
    sendVIMS_tick(x);

    return (void *)x;
}

static void post_selectors(void)
{
    int i;

    for (i = 0; i < 602; i++) {
        if (selector[i])
            post("p%03d = %s", i, selector[i]->s_name);
    }
}

void sendVIMS_setup(void)
{
    post("sendVIMS: version " VERSION);
    post("sendVIMS: (c) 2004-2006 Niels Elburg & Tom Schouten");
    post("sendVIMS: assuming veejay-0.9.8");

    s_disconnect = gensym("disconnect");
    s_veejay = gensym("veejay");
    setup_selectors();

    sendVIMS_class = class_new(gensym("sendVIMS"),
                               (t_newmethod)sendVIMS_new,
                               (t_method)sendVIMS_free,
                               sizeof(sendVIMS_t), 0, A_GIMME, 0);

    class_addanything(sendVIMS_class, (t_method)sendVIMS_anything);
    class_addmethod(sendVIMS_class, (t_method)sendVIMS_reconnect,
                    gensym("reconnect"), 0);
    class_addmethod(sendVIMS_class, (t_method)sendVIMS_disconnect,
                    gensym("disconnect"), 0);
    class_addmethod(sendVIMS_class, (t_method)sendVIMS_connect,
                    gensym("connect"), A_SYMBOL, A_DEFFLOAT, 0);
    class_addmethod(sendVIMS_class, (t_method)post_selectors,
                    gensym("aliases"), 0);
}
