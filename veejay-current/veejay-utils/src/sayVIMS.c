/* sendVIMS - very simple client for VeeJay
 *       (C) 2002-2016 Niels Elburg <nwelburg@gmail.com> 
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
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <veejaycore/vims.h>
#include <veejaycore/defs.h>
#include <veejaycore/vj-client.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#ifdef BASE64_AVUTIL
#include <libavutil/base64.h>
#endif

#define SHM_ID_LEN 16
#define STATUS_BUF_SIZE 512
#define STA_LEN_SIZE 6
#define HEADER_READ_SIZE 5
#define MAX_FRAME_READ_RETRIES 5

static int interactive = 0;
static int port_num = 3490;
static char *group_name = NULL;
static char *host_name = NULL;
static vj_client *sayvims = NULL;
static int single_msg = 0;
static char *msg = NULL;
static int dump = 0;
static int verbose = 0;
static int base64_encode = 0;
static int help = 0;
static char *in_file = NULL;
static int is_fifo =0;

/* add 4xx VIMS series, read back data from blocking socket */
static struct
{
    int vims;
    int hdr; // make header lengths consistent one day ...
} 
vims_replies[] =
{
    { VIMS_VIDEO_INFORMATION,3 }, 
    { VIMS_EFFECT_LIST,6 },            
    { VIMS_EDITLIST_LIST,6 },    
    { VIMS_BUNDLE_LIST,6 },          
    { VIMS_STREAM_LIST,5 },
    { VIMS_SAMPLE_LIST, 8},
    { VIMS_STREAM_GET_V4L,3},
    { VIMS_CHAIN_GET_ENTRY,3},
    { VIMS_VIMS_LIST,5},
    { VIMS_SAMPLE_INFO,8},
    { VIMS_SAMPLE_OPTIONS,5},
    { VIMS_DEVICE_LIST,6},
    { VIMS_FONT_LIST,6},
    { VIMS_SRT_LIST,6},
    { VIMS_SRT_INFO,6},
    { VIMS_TRACK_LIST,5},
    { VIMS_SEQUENCE_LIST,6},
    { VIMS_KEYLIST,6},
    { VIMS_WORKINGDIR,8},
    { VIMS_SAMPLE_STACK,3},
    { VIMS_GET_IMAGE,8},
    { VIMS_CHAIN_GET_PARAMETERS,4},
    { VIMS_GET_SHM,SHM_ID_LEN },
    { VIMS_GET_STREAM_ARGS, 3 },
    { VIMS_GET_GENERATORS,5 },
    { 0,0 },
};

static int read_full(vj_client *client, int channel, unsigned char *buf, int len)
{
    int total = 0;

    while (total < len) {
        int n = vj_client_read(client, channel, buf + total, len - total);
        if (n < 0) {
            return -1;
        } else if (n == 0) {
            return -1;
        }
        total += n;
    }

    return total;
}
static int send_full(vj_client *client, int channel, const unsigned char *buf, int len) {
    int total = 0;
    while (total < len) {
        int n = vj_client_send(client, channel, (unsigned char*) buf + total);
        if (n <= 0) return -1;
        total += n;
    }
    return total;
}

static vj_client *sayvims_connect(void)
{
    vj_client *client = vj_client_alloc(0, 0, 0);
    if (!client) return NULL;

    if (!host_name) {
        host_name = strdup("localhost");
        if (!host_name) {
            vj_client_free(client);
            return NULL;
        }
    }

    if (!vj_client_connect(client, host_name, group_name, port_num)) {
        vj_client_free(client);
        return NULL;
    }

    return client;
}

static void reconnect(void)
{
    if (sayvims) {
        vj_client_close(sayvims);
        vj_client_free(sayvims);
        sayvims = NULL;
    }

    sayvims = sayvims_connect();
    if (!sayvims) {
        fprintf(stderr, "Unable to make a connection with %s:%d\n",
                host_name ? host_name : "(null)", port_num);
        exit(EXIT_FAILURE);
    }
}

static int vjsend(int cmd, unsigned char *buf, size_t buflen)
{
    if (!sayvims || !buf || buflen == 0)
        return 0;

    if (!sayvims->mcast &&
        vj_client_poll(sayvims, V_CMD) &&
        vj_client_link_can_read(sayvims, V_CMD)) {

        unsigned char dummy[1];
        if (vj_client_read(sayvims, V_CMD, dummy, 1) <= 0)
            reconnect();
    }

    /* find end-of-line safely */
    size_t index = strcspn((char *)buf, "\n\r");
    if (index >= buflen)
        return 0;

    buf[index] = '\0';
    if (index == 0)
        return 0;

    if (send_full(sayvims, cmd, buf, (int)index) <= 0) {
        fprintf(stderr, "Unable to send message '%s'\n", buf);
        return 0;
    }

    if (verbose)
        fprintf(stdout, "%s\n", buf);

    return 1;
}


static int vimsReplyLength(int vims_id)
{
    int i;
    for( i = 0; vims_replies[i].vims != 0; i ++ ) {
        if( vims_replies[i].vims == vims_id ) 
            return vims_replies[i].hdr;
    }
    if (verbose) fprintf(stderr, "Unknown VIMS ID: %d\n", vims_id);
    return 0;
}


static unsigned char *vimsReply(int expectedLen, int *actualWritten) {
    if (expectedLen <= 0) return NULL;

    char header[expectedLen + 1]; 
    memset(header, 0, expectedLen + 1);

    if (read_full(sayvims, V_CMD, (unsigned char*)header, expectedLen) <= 0)
        return NULL;

    int dataLen = 0;
    if (sscanf(header, "%d", &dataLen) != 1 || dataLen <= 0)
        return NULL;

    unsigned char *data = vj_calloc(dataLen + 1);
    if (!data) return NULL;

    int readBytes = read_full(sayvims, V_CMD, data, dataLen);
    if (readBytes <= 0) {
        free(data);
        return NULL;
    }

    *actualWritten = readBytes;
    return data;
}


static unsigned char *vimsAnswer(int len) {
    if (len <= 0) return NULL;

    unsigned char *data = vj_calloc(len + 1);
    if (!data) return NULL;

    int readBytes = read_full(sayvims, V_CMD, data, len);
    if (readBytes <= 0) {
        free(data);
        return NULL;
    }

    return data;
}

static int vimsMustReadReply(char *msg, int *vims_event_id) {
    if (!msg || !vims_event_id) return 0;

    int vims_id = -1;
    int mustRead = 0;

    if (sscanf(msg, "%d:", &vims_id) == 1) {
        *vims_event_id = vims_id;
        if (vims_id >= 400 && vims_id <= 499) mustRead = 1;
    } else {
        *vims_event_id = -1;
    }

    return mustRead;
}

static int vj_flush(int frames) { 
    char status[STATUS_BUF_SIZE];

    while (frames > 0) {
        if (!vj_client_poll(sayvims, V_STATUS))
            continue;

        char sta_len[STA_LEN_SIZE] = {0};

        if (read_full(sayvims, V_STATUS, (unsigned char*)sta_len, HEADER_READ_SIZE) <= 0) {
            fprintf(stderr, "Connection closed or error reading header\n");
            return 0;
        }

        if (sta_len[0] != 'V') {
            fprintf(stderr, "Invalid status header: '%c'\n", sta_len[0]);
            continue;
        }

        int bytes = 0;
        if (sscanf(sta_len + 1, "%03d", &bytes) != 1 || bytes <= 0) {
            fprintf(stderr, "Invalid status length: '%s'\n", sta_len + 1);
            continue;
        }

        if (bytes > STATUS_BUF_SIZE) {
            fprintf(stderr, "Status too large (%d > %d), skipping\n", bytes, STATUS_BUF_SIZE);
            unsigned char discard[STATUS_BUF_SIZE];
            int to_read = bytes;
            while (to_read > 0) {
                int chunk = to_read > STATUS_BUF_SIZE ? STATUS_BUF_SIZE : to_read;
                if (read_full(sayvims, V_STATUS, discard, chunk) <= 0)
                    break;
                to_read -= chunk;
            }
            continue;
        }

        if (read_full(sayvims, V_STATUS, (unsigned char*)status, bytes) <= 0) {
            fprintf(stderr, "Error reading status data\n");
            return 0;
        }

        if (dump) {
            status[bytes] = '\0';
            fprintf(stdout, "%s\n", status);
        }

        frames--;
    }

    return 1;
}



static void Usage(char *progname)
{
    fprintf(stderr, "Usage: %s [options] [messages]\n",progname);
    fprintf(stderr, "where options are:\n");
    fprintf(stderr, " -p\t\tVeejay port (3490)\n"); 
    fprintf(stderr, " -g\t\tVeejay groupname (224.0.0.31)\n");
    fprintf(stderr, " -h\t\tVeejay hostname (localhost)\n");
    fprintf(stderr, " -m\t\tSend single message\n");
    fprintf(stderr, " -i\t\tInteractive mode\n");
    fprintf(stderr, " -f file\tRead from (special) file\n");
    fprintf(stderr, " -d\t\tDump status to stdout\n");
    fprintf(stderr, " -b\t\tBase64 encode binary data\n");
    fprintf(stderr, " -v\t\tVerbose\n");
    fprintf(stderr, " -?\t\tPrint this help\n");
    fprintf(stderr, "\nExit interactive mode by typing 'quit'\n");
    fprintf(stderr, "Messages to send to veejay must be wrapped in quotes\n");
    fprintf(stderr, "VIMS reply messages are only displayed in interactive mode.\n");
    fprintf(stderr, "You can send multiple messages by seperating them with a whitespace\n");
    fprintf(stderr, "Example: %s \"600:;\"\n",progname);
    fprintf(stderr, "         (quit veejay)\n");
    fprintf(stderr, "Example: echo \"%03d:;\" | %s \n", VIMS_QUIT, progname);
    fprintf(stderr, "\n");
    fprintf(stderr, "Example: sayVIMS -h 192.168.100.12 -m \"600:;\"\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Example: sayVIMS -m \"360:0 0 101 1;\"\n");
    fprintf(stderr, "         (Add effect 'Mirror' to chain entry)\n");
}

static int set_option(const char *name, char *value)
{
    int err = 0;
    if(strcmp(name, "h") == 0 )
    {
        host_name = strdup(optarg);
        if(group_name) err ++;
    }
    else if(strcmp(name, "g") == 0)
    {
        if(host_name) err ++; 
        group_name = strdup( optarg );  
    }
    else if (strcmp(name, "p") == 0)
    {
        port_num = atoi(optarg);
    }
    else if (strcmp(name, "i") == 0)
    {
        interactive = 1;
    }
    else if (strcmp(name, "m") == 0 )
    {
        single_msg = 1;
        msg = strdup( optarg );
    }
    else if(strcmp(name, "d") == 0)
    {
        dump = 1;
    }
    else if(strcmp(name,"v") == 0 )
    {
        verbose = 1;
    }
    else if(strcmp(name,"b") == 0 )
    {
#ifdef BASE64_AVUTIL
        base64_encode = 1;
#else
        fprintf(stderr, "compiled without base64 support\n");
        err++;
#endif
    }
    else if(strcmp(name,"?") == 0)
    {
        help = 1;
    }
    else if(strcmp(name,"f") == 0 )
    {
        in_file = strdup( optarg );
    }
    else err++;

    return err;
}

static int processLine(FILE *infile, FILE *outfile, char *tmp, size_t len)
{
    if (fgets(tmp, len, infile) == NULL) {
        if (is_fifo)
            return 0;

        if (feof(infile))
            fprintf(stderr, "EOF reached\n");

        return -1;
    }

    size_t line_len = strlen(tmp);
    if (line_len > 0 && tmp[line_len - 1] == '\n') {
        tmp[line_len - 1] = '\0';
        line_len--;
    }

    if (strncmp("quit", tmp, 4) == 0)
        return -1;

    if (tmp[0] == '+') {
        int wait_frames_ = 1;
        if (sscanf(tmp + 1, "%d", &wait_frames_) == 1) {
            if (verbose)
                fprintf(stdout, "wait %d frames\n", wait_frames_);
            if (vj_flush(wait_frames_) == 0)
                return 0;
        }
    } else {
        int vims_id = 0;
        int mustRead = vimsMustReadReply(tmp, &vims_id);

        if (vjsend(V_CMD, (unsigned char *)tmp, len) == 0)
            return 0;


        if (mustRead) {
            if (vims_id == VIMS_GET_SHM) {
                unsigned char *data = vimsAnswer(SHM_ID_LEN);
                if (data != NULL) {
                    if (outfile != NULL)
                        fwrite(data, sizeof(unsigned char), SHM_ID_LEN, outfile);
                    free(data);
                }
            } else {
                int headerLength = vimsReplyLength(vims_id);
                int dataLength = 0;
                unsigned char *data = vimsReply(headerLength, &dataLength);

                if (data != NULL) {
                    if (outfile != NULL) {
                        if (base64_encode) {
#ifdef BASE64_AVUTIL
                            int b64len = AV_BASE64_SIZE(dataLength);
                            char *out = (char *) vj_calloc(b64len);
                            char *b64str = av_base64_encode(out, b64len, data, dataLength);
                            if (b64str != NULL)
                                fwrite(b64str, sizeof(char), strlen(b64str), outfile);
                            free(out);
#else
                            fwrite(data, sizeof(char), dataLength, outfile);
#endif
                        } else {
                            fwrite(data, sizeof(char), dataLength, outfile);
                        }
                        fflush(outfile);
                    }
                    free(data);
                }
            }
        }
    }

    return 1;
}

static void do_work(int stdin_fd, FILE *std_out)
{
    FILE *instd = stdin;
    FILE *to_close = NULL;
    char *tmp = NULL;
    const size_t len = 1024;

    if (in_file) {
        to_close = fdopen(stdin_fd, "r");
        if (!to_close) {
            fprintf(stderr, "Failed to open stdin_fd\n");
            return; 
        }
        instd = to_close;
    }

    tmp = (char *) vj_calloc(len);
    if (!tmp) {
        fprintf(stderr, "Memory allocation failed\n");
        goto cleanup;
    }

    for (;;) {
        int result = processLine(instd, std_out, tmp, len);
        if (result == -1) {
            fprintf(stderr, "Session ends, bye!\n");
            break;
        }
        memset(tmp, 0, len);
    }

cleanup:
    if (to_close) {
        fclose(to_close);
    }
    if (tmp) {
        free(tmp);
    }
}


int main(int argc, char *argv[])
{
    int n = 0;
    char option[2];
    int err = 0;
    int std_fd = 0;
    int ret = 0;
    struct stat std_stat;
    memset( &std_stat, 0, sizeof(std_stat));

    veejay_set_debug_level(verbose);

    // parse commandline parameters
    while( ( n = getopt(argc,argv, "h:g:p:f:m:idbv?")) != EOF)
    {
        sprintf(option,"%c",n);
        err += set_option( option,optarg);
    }

    if(help || err  || optind > argc)
    {
        fprintf(stdout, "veejay sayVIMS %s\n", VERSION );
        Usage( argv[0] );
        return -1;
    }

    vj_mem_init(0,0);

    reconnect();

    if(!sayvims) {
        return -1;
    }

    if( interactive )
    {
        fprintf(stdout, "veejay sayVIMS %s\n",VERSION);
        fprintf(stdout, "\ttype 'quit' or press CTRL-c to exit\n");
        fprintf(stdout, "\tsee 'veejay -u' for a list of commands\n");
    }

    if(in_file)
    {
        std_fd = open( in_file, O_RDONLY );
        if(std_fd == -1) {
                fprintf(stderr, "unable to open file\n");
                return -1;
        }
        if( fstat( std_fd, &std_stat ) != 0 ) {
                fprintf(stderr, "unable to stat file: %s\n", strerror(errno));
                close(std_fd);
                return -1;
        }
        is_fifo = S_ISFIFO( std_stat.st_mode );
    }

    if(single_msg)  /* single message send */
    {
        ret = vjsend( V_CMD, (unsigned char*)msg, strlen(msg) + 1 );

        free(msg);
    }
    else
    {
        do_work( std_fd, ( interactive ? stdout : NULL ) );
    }

    vj_client_close(sayvims);
    vj_client_free(sayvims);
    if(host_name) free(host_name);
    if(group_name) free(group_name);

    if(in_file)
        close(std_fd);

    return ret;
} 
