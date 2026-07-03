/*
 * Linux VeeJay
 *
 * Copyright(C)2002-2016 Niels Elburg < nwelburg@gmail.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 *
 *
 */


#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <veejaycore/defs.h>
#include <veejaycore/vjmem.h>
#include <string.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vevo.h>
#include <libveejay/vevo.h>
#include <veejaycore/libvevo.h>
#include <libsample/sampleadm.h>
#include <libstream/vj-tag.h>
#include <assert.h>
#ifdef HAVE_XML2
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#endif
#include <libvjxml/vj-xml.h>
#include <libveejay/vjkf.h>
#include <math.h>
#include <limits.h>

#define KEYFRAME_PACKET_HEADER_LEN 35
#define KEYFRAME_PACKET_HEADER_FMT "key%02d%02d%08d%08d%02d%08d%02d"
#define KEYFRAME_PACKET_HEADER_SCAN_FMT "key%2d%2d%8d%8d%2d%8d%2d"
#define KEYFRAME_PACKET_MAX_VALUES 2000000

static int keyframe_payload_length_for_range(int start, int end,
                                             int *values_len,
                                             int *payload_len)
{
    long long n = 0;

    if (start < 0 || end < start) {
        return 0;
    }

    n = (long long) end - (long long) start + 1LL;

    if (n <= 0 || n > KEYFRAME_PACKET_MAX_VALUES) {
        return 0;
    }

    if (n > (INT_MAX - KEYFRAME_PACKET_HEADER_LEN) / 4) {
        return 0;
    }

    if (values_len) {
        *values_len = (int) n;
    }

    if (payload_len) {
        *payload_len = KEYFRAME_PACKET_HEADER_LEN + ((int) n * 4);
    }

    return 1;
}

static int keyframe_header_fields_valid(int entry_id,
                                        int parameter_id,
                                        int start,
                                        int end,
                                        int type,
                                        int shape,
                                        int status)
{
    if (entry_id < 0 || entry_id > 99) {
        return 0;
    }

    if (parameter_id < 0 || parameter_id > 99) {
        return 0;
    }

    if (start < 0 || start > 99999999) {
        return 0;
    }

    if (end < start || end > 99999999) {
        return 0;
    }

    if (type < 0 || type > 99) {
        return 0;
    }

    if (shape < 0 || shape > 99999999) {
        return 0;
    }

    if (status < 0 || status > 99) {
        return 0;
    }

    return 1;
}

static int keyframe_format_header(char *dst,
                                  size_t dst_size,
                                  int entry_id,
                                  int parameter_id,
                                  int start,
                                  int end,
                                  int type,
                                  int shape,
                                  int status)
{
    int hdr_len = 0;

    if (!dst || dst_size < (KEYFRAME_PACKET_HEADER_LEN + 1)) {
        return 0;
    }

    if (!keyframe_header_fields_valid(entry_id, parameter_id,
                                      start, end, type, shape, status)) {
        return 0;
    }

    hdr_len = snprintf(dst, dst_size,
                       KEYFRAME_PACKET_HEADER_FMT,
                       entry_id, parameter_id,
                       start, end, type, shape, status);

    return (hdr_len == KEYFRAME_PACKET_HEADER_LEN);
}

int keyframe_is_chain_opacity_parameter(int parameter_id)
{
    return parameter_id == VJ_KF_PARAM_CHAIN_OPACITY;
}

static	char	*extract_( const char *prefix , int p_id )
{
	char tmp[100];
	snprintf(tmp,sizeof(tmp), "%s_p%d",prefix,p_id);
	return vj_strdup(tmp);
}

static void keyframe_free_data(void *port, const char *k_d)
{
    int *values = NULL;

    if (vevo_property_get(port, k_d, 0, &values) == VEVO_NO_ERROR)
    {
        if (values)
        {
            free(values);
            values = NULL;
        }

        vevo_property_del(port, k_d);
    }
}

int keyframe_set_data(void *port, int parameter_id, int *values, int value_len)
{
    if (!port)
        return 0;

    char *k_d = extract_("data", parameter_id);
    char *k_len = extract_( "datalen", parameter_id );

    keyframe_free_data(port, k_d);

    vevo_property_set(port, k_d, VEVO_ATOM_TYPE_VOIDPTR, 1, &values);
    vevo_property_set(port, k_len, VEVO_ATOM_TYPE_INT, 1, &value_len );

    free(k_d);
    free(k_len);

    return 1;
}

unsigned char *keyframe_pack(void *port, int parameter_id, int entry_id, int *rlen)
{
    if (!port || !rlen) {
        return NULL;
    }

    *rlen = 0;

    int start = 0, end = 0, type = 0, shape = 0, status = 0, values_len = 0;
    int range_len = 0;
    int total = 0;
    int *values = NULL;

    /* early exit for fast path */
    char *k_s = extract_("start", parameter_id);
    if (!k_s || vevo_property_get(port, k_s, 0, &start) != VEVO_NO_ERROR) {
        free(k_s); return NULL;
    }

    char *k_e = extract_("end", parameter_id);
    if (!k_e || vevo_property_get(port, k_e, 0, &end) != VEVO_NO_ERROR) {
        free(k_s); free(k_e); return NULL;
    }

    char *k_t = extract_("type", parameter_id);
    if (!k_t || vevo_property_get(port, k_t, 0, &type) != VEVO_NO_ERROR) {
        free(k_s); free(k_e); free(k_t); return NULL;
    }

    char *k_h = extract_("shape", parameter_id);
    if (!k_h || vevo_property_get(port, k_h, 0, &shape) != VEVO_NO_ERROR) {
        free(k_s); free(k_e); free(k_t); free(k_h); return NULL;
    }

    char *k_x = extract_("status", parameter_id);
    if (!k_x || vevo_property_get(port, k_x, 0, &status) != VEVO_NO_ERROR) {
        free(k_s); free(k_e); free(k_t); free(k_h); free(k_x); return NULL;
    }

    char *k_dn = extract_("datalen", parameter_id);
    if (!k_dn || vevo_property_get(port, k_dn, 0, &values_len) != VEVO_NO_ERROR) {
        free(k_s); free(k_e); free(k_t); free(k_h); free(k_x); free(k_dn); return NULL;
    }

    char *k_d = extract_("data", parameter_id);
    if(!k_d || vevo_property_get(port, k_d, 0, &values) != VEVO_NO_ERROR) {
        free(k_s); free(k_e); free(k_t); free(k_h); free(k_x); free(k_dn); free(k_d); return NULL;
    }

    free(k_s); free(k_e); free(k_t); free(k_h); free(k_x); free(k_dn); free(k_d);

    if (values_len <= 0 || values == NULL) {
        return NULL;
    }

    if (!keyframe_payload_length_for_range(start, end, &range_len, &total)) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[FX Anim] invalid keyframe range/header fields entry=%d param=%d start=%d end=%d type=%d shape=%d status=%d",
                   entry_id, parameter_id, start, end, type, shape, status);
        return NULL;
    }

    if (values_len != range_len) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[FX Anim] datalen mismatch for parameter %d: datalen=%d range=%d (%d-%d)",
                   parameter_id, values_len, range_len, start, end);
        return NULL;
    }

    unsigned char *result = vj_calloc(total);
    if (!result) {
        return NULL;
    }

    if (!keyframe_format_header((char *) result,
                                KEYFRAME_PACKET_HEADER_LEN + 1,
                                entry_id,
                                parameter_id,
                                start,
                                end,
                                type,
                                shape,
                                status)) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[FX Anim] unable to format fixed-width keyframe header");
        free(result);
        return NULL;
    }

    unsigned char *out = result + KEYFRAME_PACKET_HEADER_LEN;
    for (int i = 0; i < values_len; i++) {
        uint32_t v = (uint32_t) values[i];
        out[i*4+0] = (unsigned char) (v & 0xffu);
        out[i*4+1] = (unsigned char) ((v >> 8) & 0xffu);
        out[i*4+2] = (unsigned char) ((v >> 16) & 0xffu);
        out[i*4+3] = (unsigned char) ((v >> 24) & 0xffu);
    }

    *rlen = total;
    return result;
}


int	keyframe_get_param_status( int lookup, int fx_entry, int parameter_id, int is_sample )
{
	void *port = NULL;

    if(keyframe_is_chain_opacity_parameter(parameter_id)) {
        port = is_sample ? sample_get_chain_fade_kf_port(lookup)
                         : vj_tag_get_chain_fade_kf_port(lookup);
    } else if( is_sample ) {
		port = sample_get_kf_port( lookup, fx_entry );
	} else {
		port = vj_tag_get_kf_port( lookup, fx_entry );
	}

	if( port == NULL ) {
		veejay_msg(0, "FX Entry %d does not have animated parameters", fx_entry );
		return 0;
	}

	int status = 0;
	char *k_x = extract_ ( "status", parameter_id );
	
	vevo_property_get( port, k_x,0, &status ); 

	free(k_x);

	return status;
}


void	keyframe_set_param_status( int lookup, int fx_entry, int parameter_id, int status, int is_sample )
{
	void *port = NULL;
	int kf_status = status;

    if(keyframe_is_chain_opacity_parameter(parameter_id)) {
        port = is_sample ? sample_get_chain_fade_kf_port(lookup)
                         : vj_tag_get_chain_fade_kf_port(lookup);
    } else if( is_sample ) {
		port = sample_get_kf_port( lookup, fx_entry );
	} else {
		port = vj_tag_get_kf_port( lookup, fx_entry );
	}

	if( port == NULL ) {
		veejay_msg(0, "FX Entry %d does not have animated parameters", fx_entry );
		return;
	}

	char *k_x = extract_( "status", parameter_id );
	
	vevo_property_set( port, k_x, VEVO_ATOM_TYPE_INT,1, &kf_status );

	free(k_x);
}

void    keyframe_clear_entry( int lookup, int fx_entry, int parameter_id, int is_sample )
{
    void *port = NULL;

    if(keyframe_is_chain_opacity_parameter(parameter_id)) {
        port = is_sample ? sample_get_chain_fade_kf_port(lookup)
                         : vj_tag_get_chain_fade_kf_port(lookup);
    } else if( is_sample ) {
        port = sample_get_kf_port( lookup, fx_entry );
    } else {
        port = vj_tag_get_kf_port( lookup, fx_entry );
    }

    if( port == NULL ) {
        veejay_msg(0, "FX Entry %d does not have animated parameters", fx_entry );
        return;
    }

    char *k_s  = extract_ ( "start", parameter_id );
    char *k_e  = extract_ ( "end", parameter_id );
    char *k_t  = extract_ ( "type", parameter_id );
    char *k_h  = extract_ ( "shape", parameter_id );
    char *k_x  = extract_ ( "status", parameter_id );
    char *k_d  = extract_ ( "data" , parameter_id );
    char *k_dn = extract_ ( "datalen", parameter_id );

    keyframe_free_data(port, k_d);

    vevo_property_del( port, k_s );
    vevo_property_del( port, k_e );
    vevo_property_del( port, k_t );
    vevo_property_del( port, k_h );
    vevo_property_del( port, k_x );
    vevo_property_del( port, k_dn );

    free(k_s);
    free(k_e);
    free(k_t);
    free(k_h);
    free(k_x);
    free(k_d);
    free(k_dn);
}

int keyframe_unpack(unsigned char *in, int len,
                    int *entry, int lookup, int is_sample)
{
    int parameter_id = 0;
    int start = 0, end = 0, type = 0, shape = 0;
    int fx_entry = 0;
    int status = 0;
    int values_len = 0;
    int expected = 0;

    if (!in || !entry) {
        return -1;
    }

    if (len < KEYFRAME_PACKET_HEADER_LEN) {
        veejay_msg(VEEJAY_MSG_ERROR, "[FX Anim] packet too small (%d/%d)", len, KEYFRAME_PACKET_HEADER_LEN );
        return -1;
    }

    char header[KEYFRAME_PACKET_HEADER_LEN + 1];
    memcpy(header, in, KEYFRAME_PACKET_HEADER_LEN);
    header[KEYFRAME_PACKET_HEADER_LEN] = '\0';

    int n = sscanf(header,
                   KEYFRAME_PACKET_HEADER_SCAN_FMT,
                   &fx_entry, &parameter_id, &start, &end, &type, &shape, &status);

    if (n != 7) {
        veejay_msg(VEEJAY_MSG_ERROR, "[FX Anim] invalid packet header");
        return -1;
    }

    if (!keyframe_header_fields_valid(fx_entry, parameter_id,
                                      start, end, type, shape, status)) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[FX Anim] invalid packet header fields entry=%d param=%d start=%d end=%d type=%d shape=%d status=%d",
                   fx_entry, parameter_id, start, end, type, shape, status);
        return -1;
    }

    if (!keyframe_payload_length_for_range(start, end, &values_len, &expected)) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[FX Anim] invalid keyframe range %d - %d", start, end);
        return -1;
    }

    if (len < expected) {
        veejay_msg(VEEJAY_MSG_ERROR, "[FX Anim] packet data is truncated (expected %d, have %d)", expected, len );
        return -1;
    }

    int *values = vj_malloc(sizeof(int) * values_len);
    if (!values) {
        veejay_msg(VEEJAY_MSG_ERROR, "[FX Anim] memory allocation error packet of %d size", values_len);
        return -1;
    }

    const unsigned char *ptr = in + KEYFRAME_PACKET_HEADER_LEN;
    for (int i = 0; i < values_len; i++) {
        uint32_t v =
            ((uint32_t) ptr[0]) |
            ((uint32_t) ptr[1] << 8) |
            ((uint32_t) ptr[2] << 16) |
            ((uint32_t) ptr[3] << 24);

        values[i] = (int) v;
        ptr += 4;
    }

    void *port = NULL;

    if(keyframe_is_chain_opacity_parameter(parameter_id)) {
        port = is_sample ? sample_chain_fade_alloc_kf(lookup)
                         : vj_tag_chain_fade_alloc_kf(lookup);
        fx_entry = VJ_KF_ENTRY_CHAIN_FADE;
    } else {
        port = is_sample ? sample_get_kf_port(lookup, fx_entry) : vj_tag_get_kf_port(lookup, fx_entry);

        if (!port && is_sample) {
            sample_chain_alloc_kf(lookup, fx_entry);
            port = sample_get_kf_port(lookup, fx_entry);
        }
    }

    if (!port) {
        free(values);
        return 0;
    }

    char *k_s  = extract_("start", parameter_id);
    char *k_e  = extract_("end", parameter_id);
    char *k_t  = extract_("type", parameter_id);
    char *k_h  = extract_("shape", parameter_id);
    char *k_x  = extract_("status", parameter_id);

    vevo_property_set(port, k_s, VEVO_ATOM_TYPE_INT, 1, &start);
    vevo_property_set(port, k_e, VEVO_ATOM_TYPE_INT, 1, &end);
    vevo_property_set(port, k_t, VEVO_ATOM_TYPE_INT, 1, &type);
    vevo_property_set(port, k_h, VEVO_ATOM_TYPE_INT, 1, &shape);
    vevo_property_set(port, k_x, VEVO_ATOM_TYPE_INT, 1, &status);

    int data_ok = keyframe_set_data(port, parameter_id, values, values_len );

    free(k_s); free(k_e); free(k_t);
    free(k_h); free(k_x);

    if(data_ok && keyframe_is_chain_opacity_parameter(parameter_id)) {
        if(is_sample)
            sample_chain_fade_set_kf_status(lookup, status, type);
        else
            vj_tag_chain_fade_set_kf_status(lookup, status, type);
    }

    *entry = fx_entry;
    return data_ok;
}

int keyframe_get_param_tokens(void *port, int parameter_id,
                        int *start, int *end,
                        int *type, int *shape, int *status)
{
    if (!port || !start || !end || !type || !shape || !status) {
        return 0;
    }

    char *k_s = extract_("start", parameter_id);
    char *k_e = extract_("end", parameter_id);
    char *k_t = extract_("type", parameter_id);
    char *k_h = extract_("shape", parameter_id);
    char *k_x = extract_("status", parameter_id);

    int ok =
        vevo_property_get(port, k_s, 0, start)  == VEVO_NO_ERROR &&
        vevo_property_get(port, k_e, 0, end)    == VEVO_NO_ERROR &&
        vevo_property_get(port, k_t, 0, type)   == VEVO_NO_ERROR &&
        vevo_property_get(port, k_h, 0, shape)  == VEVO_NO_ERROR &&
        vevo_property_get(port, k_x, 0, status) == VEVO_NO_ERROR;

    free(k_s);
    free(k_e);
    free(k_t);
    free(k_h);
    free(k_x);

    return ok;
}



int keyframe_xml_pack( xmlNodePtr node, void *port, int parameter_id  )
{
    int i;
    int start = 0, end = 0, type = 0, shape = 0, status = 0, values_len = 0;
    int *values = NULL;

    char *k_s  = extract_ ( "start", parameter_id );
    char *k_e  = extract_ ( "end", parameter_id );
    char *k_t  = extract_ ( "type", parameter_id );
    char *k_h  = extract_ ( "shape", parameter_id );
    char *k_x  = extract_ ( "status", parameter_id );
    char *k_d  = extract_ ( "data", parameter_id );
    char *k_dn = extract_ ( "datalen", parameter_id );

    if( vevo_property_get( port, k_s, 0, &start ) != VEVO_NO_ERROR )
    {
            free(k_s); free(k_e); free(k_t); free(k_h); free(k_x); free(k_d); free(k_dn);
            return 0;
    }
    if( vevo_property_get( port, k_e,   0, &end ) != VEVO_NO_ERROR )
    {
            free(k_s); free(k_e); free(k_t); free(k_h); free(k_x); free(k_d); free(k_dn);
            return 0;
    }
    if( vevo_property_get( port, k_t,   0, &type ) != VEVO_NO_ERROR )
    {
            free(k_s); free(k_e); free(k_t); free(k_h); free(k_x); free(k_d); free(k_dn);
            return 0;
    }
    if( vevo_property_get( port, k_h,   0, &shape ) != VEVO_NO_ERROR )
    {
            free(k_s); free(k_e); free(k_t); free(k_h); free(k_x); free(k_d); free(k_dn);
            return 0;
    }
    if( vevo_property_get( port, k_x,   0, &status ) != VEVO_NO_ERROR )
    {
            free(k_s); free(k_e); free(k_t); free(k_h); free(k_x); free(k_d); free(k_dn);
            return 0;
    }
    if( vevo_property_get( port, k_d,   0, &values ) != VEVO_NO_ERROR )
    {
            free(k_s); free(k_e); free(k_t); free(k_h); free(k_x); free(k_d); free(k_dn);
            return 0;
    }
    if( vevo_property_get( port, k_dn,   0, &values_len ) != VEVO_NO_ERROR )
    {
            free(k_s); free(k_e); free(k_t); free(k_h); free(k_x); free(k_d); free(k_dn);
            return 0;
    }

    put_xml_int( node, k_s, start );
    put_xml_int( node, k_e, end );
    put_xml_int( node, k_t, type );
    put_xml_int( node, k_h, shape );
    put_xml_int( node, k_x, status );
    put_xml_int( node, k_dn, values_len );

    for( i = 0; i < values_len ; i ++ )
    {
        char xmlbuf[128];
        snprintf((char*)xmlbuf, sizeof(xmlbuf), "%d %d", parameter_id, values[i] );
        put_xml_str( node, "value", xmlbuf );
    }

    free(k_s);
    free(k_e);
    free(k_t);
    free(k_h);
    free(k_x);
    free(k_d);
    free(k_dn);

    return 1;
}

int keyframe_xml_unpack(xmlDocPtr doc, xmlNodePtr node, void *port)
{
    if (!node || !port)
        return 0;

    int start = 0, end = 0, type = 0, shape = 0, status = 0;
    int have_shape = 0;
    int values_len = 0, frame = 0;
    int *values = NULL;
    int parameter_id = -1;

    /* pass 1: find datalen */
    for (xmlNodePtr n = node; n; n = n->next) {
        if (!xmlStrncmp(n->name, (xmlChar*)"datalen_p", 9)) {
            values_len = get_xml_int(doc, n);
            if (values_len <= 0)
                return 0;
            values = vj_calloc(sizeof(int) * values_len);
            if(!values) {
                return 0;
            }
            parameter_id = atoi((char*)(n->name + 9));
        }
    }

    if (!values) {
        return 0;
    }

    /* pass 2: parse */
    for (; node; node = node->next) {

        if (!xmlStrncmp(node->name, (xmlChar*)"start", 5))
            start = get_xml_int(doc, node);

        else if (!xmlStrncmp(node->name, (xmlChar*)"end", 3))
            end = get_xml_int(doc, node);

        else if (!xmlStrncmp(node->name, (xmlChar*)"type", 4))
            type = get_xml_int(doc, node);

        else if (!xmlStrncmp(node->name, (xmlChar*)"shape", 5)) {
            shape = get_xml_int(doc, node);
            have_shape = 1;
        }

        else if (!xmlStrncmp(node->name, (xmlChar*)"status", 6))
            status = get_xml_int(doc, node);

        else if (!xmlStrncmp(node->name, (xmlChar*)"value", 5)) {
            int val = 0;
            get_xml_2int(doc, node, &val);

            if (frame >= values_len)
                break;

            values[frame++] = val; /* every frame has a parameter value */
        }
    }

    if (parameter_id < 0 || !have_shape || frame != values_len) {
        free(values);
        return 0;
    }

    char *k_s  = extract_("start", parameter_id);
    char *k_e  = extract_("end", parameter_id);
    char *k_t  = extract_("type", parameter_id);
    char *k_h  = extract_("shape", parameter_id);
    char *k_x  = extract_("status", parameter_id);
    char *k_d  = extract_("data", parameter_id);
    char *k_dn = extract_("datalen", parameter_id);

    vevo_property_set(port, k_s,  VEVO_ATOM_TYPE_INT,     1, &start);
    vevo_property_set(port, k_e,  VEVO_ATOM_TYPE_INT,     1, &end);
    vevo_property_set(port, k_t,  VEVO_ATOM_TYPE_INT,     1, &type);
    vevo_property_set(port, k_h,  VEVO_ATOM_TYPE_INT,     1, &shape);
    vevo_property_set(port, k_x,  VEVO_ATOM_TYPE_INT,     1, &status);
    vevo_property_set(port, k_d,  VEVO_ATOM_TYPE_VOIDPTR, 1, &values);
    vevo_property_set(port, k_dn, VEVO_ATOM_TYPE_INT,     1, &values_len);

    free(k_s); free(k_e); free(k_t);
    free(k_h); free(k_x); free(k_d); free(k_dn);

    return 1;
}

int get_keyframe_value(void *port, long long n_frame,
                       int parameter_id, int *result)
{
    char *k_x = extract_("status",parameter_id);
    char *k_s = extract_("start",parameter_id);
    char *k_e = extract_("end",parameter_id);
    char *k_d = extract_("data",parameter_id);

    int status=0,start=0,end=0;

    if (vevo_property_get(port,k_x,0,&status)!=VEVO_NO_ERROR ||
        vevo_property_get(port,k_s,0,&start)!=VEVO_NO_ERROR ||
        vevo_property_get(port,k_e,0,&end)!=VEVO_NO_ERROR)
    {
        free(k_x); free(k_s); free(k_e); free(k_d);
        return 0;
    }

    if (!status || n_frame < start || n_frame > end) {
        free(k_x); free(k_s); free(k_e); free(k_d);
        return 0;
    }

    int *values=NULL;
    if (vevo_property_get(port,k_d,0,&values)!=VEVO_NO_ERROR) {
        free(k_x); free(k_s); free(k_e); free(k_d);
        return 0;
    }

    int idx = (int)(n_frame - start);
    int max_idx = end - start + 1;
    if (!values || idx < 0 || idx >= max_idx) {
        free(k_x); free(k_s); free(k_e); free(k_d);
        return 0;
    }

    *result = values[idx];

    free(k_x); free(k_s); free(k_e); free(k_d);
    return 1;
}


static int *keyframe_resample_data(int *old_values, int old_len, int new_len) 
{
    if (!old_values || old_len <= 0 || new_len <= 0) {
        return NULL;
    }

    int *new_values = vj_malloc(sizeof(int) * new_len);
    if (!new_values) {
        return NULL;
    }

    if (old_len == 1) {
        for (int i = 0; i < new_len; i++) {
            new_values[i] = old_values[0];
        }
        return new_values;
    }
    
    if (new_len == 1) {
        new_values[0] = old_values[0];
        return new_values;
    }

    for (int i = 0; i < new_len; i++) {
        double position = (double)i * (old_len - 1) / (new_len - 1);
        
        int index_left = (int)position;
        int index_right = index_left + 1;
        
        if (index_right >= old_len) {
            index_right = old_len - 1;
        }

        double fraction = position - index_left;
        double interpolated_val = (old_values[index_left] * (1.0 - fraction)) + 
                                  (old_values[index_right] * fraction);
                     
        new_values[i] = (int)round(interpolated_val); 
    }

    return new_values;
}

int keyframe_resize_entry(void *port, int parameter_id, int new_start, int new_end)
{
    if (!port || new_start > new_end) return 0;

    int old_start = 0, old_end = 0, old_len = 0;
    int *old_values = NULL;

    char *k_s  = extract_("start", parameter_id);
    char *k_e  = extract_("end", parameter_id);
    char *k_d  = extract_("data", parameter_id);
    char *k_dn = extract_("datalen", parameter_id);

    if (vevo_property_get(port, k_s, 0, &old_start) != VEVO_NO_ERROR ||
        vevo_property_get(port, k_e, 0, &old_end) != VEVO_NO_ERROR ||
        vevo_property_get(port, k_d, 0, &old_values) != VEVO_NO_ERROR ||
        vevo_property_get(port, k_dn, 0, &old_len) != VEVO_NO_ERROR) 
    {
        free(k_s); free(k_e); free(k_d); free(k_dn);
        return 0;
    }

    int new_len = 0;

    if (!keyframe_payload_length_for_range(new_start, new_end, &new_len, NULL)) {
        free(k_s); free(k_e); free(k_d); free(k_dn);
        return 0;
    }

    if (new_len == old_len) {
        // note: must update start/end if shifted in time
        vevo_property_set(port, k_s, VEVO_ATOM_TYPE_INT, 1, &new_start);
        vevo_property_set(port, k_e, VEVO_ATOM_TYPE_INT, 1, &new_end);
        free(k_s); free(k_e); free(k_d); free(k_dn);
        return 1;
    }

    int *new_values = keyframe_resample_data(old_values, old_len, new_len);
    
    if (!new_values) {
        free(k_s); free(k_e); free(k_d); free(k_dn);
        return 0;
    }

    vevo_property_set(port, k_s,  VEVO_ATOM_TYPE_INT, 1, &new_start);
    vevo_property_set(port, k_e,  VEVO_ATOM_TYPE_INT, 1, &new_end);
    vevo_property_set(port, k_d,  VEVO_ATOM_TYPE_VOIDPTR, 1, &new_values);
    vevo_property_set(port, k_dn, VEVO_ATOM_TYPE_INT, 1, &new_len);

    if (old_values) {
        free(old_values); 
    }

    free(k_s); free(k_e); free(k_d); free(k_dn);
    return 1;
}


void *keyframe_port_clone_and_resize(void *src_port, int new_len)
{
    if (!src_port || new_len <= 0) return NULL;

    void *dst_port = vpn(VEVO_ANONYMOUS_PORT);
    if (!dst_port) return NULL;

    for (int p_id = 0; p_id < SAMPLE_MAX_PARAMETERS; p_id++) {
        int status = 0, type = 0, shape = 0;
        int old_start = 0, old_end = 0, old_datalen = 0;
        int *old_values = NULL;
        char *k_dn = extract_("datalen", p_id);
        if( vevo_property_get(src_port, k_dn, 0, &old_datalen) != VEVO_NO_ERROR ) {
            free(k_dn);
            continue;
        }

        char *k_x = extract_("status", p_id);
        char *k_s  = extract_("start", p_id);
        char *k_e  = extract_("end", p_id);
        char *k_t  = extract_("type", p_id);
        char *k_h  = extract_("shape", p_id);
        char *k_d  = extract_("data", p_id);

        int have_all =
            vevo_property_get(src_port, k_s, 0, &old_start)  == VEVO_NO_ERROR &&
            vevo_property_get(src_port, k_e, 0, &old_end)    == VEVO_NO_ERROR &&
            vevo_property_get(src_port, k_t, 0, &type)       == VEVO_NO_ERROR &&
            vevo_property_get(src_port, k_h, 0, &shape)      == VEVO_NO_ERROR &&
            vevo_property_get(src_port, k_d, 0, &old_values) == VEVO_NO_ERROR &&
            vevo_property_get(src_port, k_x, 0, &status)     == VEVO_NO_ERROR;

        if (have_all && old_values && old_datalen > 0) {
            int *new_values = keyframe_resample_data(old_values, old_datalen, new_len);

            if (new_values) {
                int new_start = 0;
                int new_end   = new_len - 1;

                vevo_property_set(dst_port, k_s,  VEVO_ATOM_TYPE_INT, 1, &new_start);
                vevo_property_set(dst_port, k_e,  VEVO_ATOM_TYPE_INT, 1, &new_end);
                vevo_property_set(dst_port, k_t,  VEVO_ATOM_TYPE_INT, 1, &type);
                vevo_property_set(dst_port, k_h,  VEVO_ATOM_TYPE_INT, 1, &shape);
                vevo_property_set(dst_port, k_x,  VEVO_ATOM_TYPE_INT, 1, &status);
                vevo_property_set(dst_port, k_dn, VEVO_ATOM_TYPE_INT, 1, &new_len);

                keyframe_set_data(dst_port, p_id, new_values, new_len );
            }
        }

        free(k_x); free(k_s); free(k_e);
        free(k_t); free(k_h); free(k_d); free(k_dn);
    }

    return dst_port;
}

