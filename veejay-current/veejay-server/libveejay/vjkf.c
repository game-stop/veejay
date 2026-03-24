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

static	char	*extract_( const char *prefix , int p_id )
{
	char tmp[100];
	snprintf(tmp,sizeof(tmp), "%s_p%d",prefix,p_id);
	return vj_strdup(tmp);
}

unsigned char *keyframe_pack(void *port, int parameter_id, int entry_id, int *rlen)
{
    if(!rlen)
        return NULL;

    int i;
    unsigned char *result = NULL;
    int start = 0, end = 0, type = 0, status = 0, values_len = 0;
    int *values = NULL;

    char *k_s  = extract_("start",  parameter_id);
    char *k_e  = extract_("end",    parameter_id);
    char *k_t  = extract_("type",   parameter_id);
    char *k_x  = extract_("status", parameter_id);
    char *k_d  = extract_("data",   parameter_id);
    char *k_dn = extract_("datalen",parameter_id);

    int err = 0;

    if(vevo_property_get(port,k_s,0,&start) != VEVO_NO_ERROR) err = 1;
    if(vevo_property_get(port,k_e,0,&end) != VEVO_NO_ERROR) err = 1;
    if(vevo_property_get(port,k_t,0,&type) != VEVO_NO_ERROR) err = 1;
    if(vevo_property_get(port,k_x,0,&status) != VEVO_NO_ERROR) err = 1;
    if(vevo_property_get(port,k_d,0,&values) != VEVO_NO_ERROR) err = 1;
    if(vevo_property_get(port,k_dn,0,&values_len) != VEVO_NO_ERROR) err = 1;

    if(err) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "Failed to get KF %d properties: start=%d, end=%d, type=%d, status=%d, values_len=%d%s",
                   parameter_id, start, end, type, status, values_len,
                   values && values_len > 0 ? ", values[0]=%d" : "",
                   values && values_len > 0 ? values[0] : 0);

        free(k_s); free(k_e); free(k_t); free(k_x); free(k_d); free(k_dn);
        return NULL;
    }

    free(k_s); free(k_e); free(k_t); free(k_x); free(k_d); free(k_dn);

    if (values_len <= 0 || values == NULL)
        return NULL;

    int total = 27 + (values_len * 4);
    result = vj_calloc(total);
    if (!result)
        return NULL;

    unsigned char *p = result;
    snprintf((char*)p, 28, "key%02d%02d%08d%08d%02d%02d",
             entry_id, parameter_id, start, end, type, status);
    p[27] = 0;

    unsigned char *out = result + 27;

    for (i = 0; i < values_len; i++) {
        int v = values[i];
        out[i*4+0] = (v) & 0xff;
        out[i*4+1] = (v >> 8) & 0xff;
        out[i*4+2] = (v >> 16) & 0xff;
        out[i*4+3] = (v >> 24) & 0xff;
    }

    *rlen = total;
    return result;
}

int	keyframe_get_param_status( int lookup, int fx_entry, int parameter_id, int is_sample )
{
	void *port = NULL;

	if( is_sample ) {
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

	if( is_sample ) {
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

void	keyframe_clear_entry( int lookup, int fx_entry, int parameter_id, int is_sample )
{
	int start = 0;
	int end = 0;
	int type = 0;
	int status = 0;
	int values_len = 0;
	int *values = NULL;
	void *port = NULL;

	if( is_sample ) {
		port = sample_get_kf_port( lookup, fx_entry );
	} else {
		port = vj_tag_get_kf_port( lookup, fx_entry );
	}

	if( port == NULL ) {
		veejay_msg(0, "FX Entry %d does not have animated parameters", fx_entry );
		return;
	}

	char *k_s = extract_ ( "start", parameter_id );
	char *k_e = extract_ ( "end", parameter_id );
	char *k_t = extract_ ( "type", parameter_id );
	char *k_x = extract_ ( "status", parameter_id );
	char *k_d = extract_ ( "data", parameter_id );
	char *k_dn= extract_ ( "datalen", parameter_id );

    if (vevo_property_get(port, k_s, 0, &start) != VEVO_NO_ERROR) start = 0;
    if (vevo_property_get(port, k_e, 0, &end) != VEVO_NO_ERROR) end = 0;
    if (vevo_property_get(port, k_t, 0, &type) != VEVO_NO_ERROR) type = 0;
    if (vevo_property_get(port, k_x, 0, &status) != VEVO_NO_ERROR) status = 0;
	
    if (vevo_property_get(port, k_d, 0, &values) == VEVO_NO_ERROR && values != NULL) {
        free(values);
        values = NULL;
    }

    if (vevo_property_get(port, k_dn, 0, &values_len) != VEVO_NO_ERROR) values_len = 0;

	if( values != NULL ) {
		free(values);
	}

	vevo_property_del( port, k_s );
	vevo_property_del( port, k_e );
	vevo_property_del( port, k_t );
	vevo_property_del( port, k_x );
	vevo_property_del( port, k_d );
	vevo_property_del( port, k_dn);

	free(k_s);
	free(k_e);
	free(k_t);
	free(k_x);
	free(k_d);
	free(k_dn);
}

int keyframe_unpack(unsigned char *in, int len,
                    int *entry, int lookup, int is_sample)
{
    int parameter_id = 0;
    int start = 0, end = 0, type = 0;
    int fx_entry = 0;
    int status = 0;

    if (len < 27)
        return 0;

    int n = sscanf((char*)in,
                   "key%2d%2d%8d%8d%2d%2d",
                   &fx_entry,&parameter_id,&start,&end,&type,&status);

    if (n != 6)
        return 0;

    if (start < 0 || end < start)
        return 0;

    int values_len = end - start + 1;
    int expected = 27 + (values_len * 4);

    if (len < expected)
        return 0;

    int *values = vj_calloc(sizeof(int) * values_len);
    if (!values)
        return 0;

    unsigned char *ptr = in + 27;
    for (int i = 0; i < values_len; i++) {
        values[i] =
            ((int)ptr[0]) |
            ((int)ptr[1] << 8) |
            ((int)ptr[2] << 16) |
            ((int)ptr[3] << 24);
        ptr += 4;
    }

    void *port = is_sample ?
        sample_get_kf_port(lookup, fx_entry) :
        vj_tag_get_kf_port(lookup, fx_entry);

    if (!port && is_sample) {
        sample_chain_alloc_kf(lookup, fx_entry);
        port = sample_get_kf_port(lookup, fx_entry);
    }

    if (!port) {
        free(values);
        return 0;
    }

    char *k_s  = extract_("start",parameter_id);
    char *k_e  = extract_("end",parameter_id);
    char *k_t  = extract_("type",parameter_id);
    char *k_x  = extract_("status",parameter_id);
    char *k_d  = extract_("data",parameter_id);
    char *k_dn = extract_("datalen",parameter_id);

    vevo_property_set(port,k_s,VEVO_ATOM_TYPE_INT,1,&start);
    vevo_property_set(port,k_e,VEVO_ATOM_TYPE_INT,1,&end);
    vevo_property_set(port,k_t,VEVO_ATOM_TYPE_INT,1,&type);
    vevo_property_set(port,k_x,VEVO_ATOM_TYPE_INT,1,&status);
    vevo_property_set(port,k_d,VEVO_ATOM_TYPE_VOIDPTR,1,&values);
    vevo_property_set(port,k_dn,VEVO_ATOM_TYPE_INT,1,&values_len);

    free(k_s); free(k_e); free(k_t);
    free(k_x); free(k_d); free(k_dn);

    *entry = fx_entry;
    return 1;
}

int keyframe_get_tokens(void *port, int parameter_id,
                        int *start, int *end,
                        int *type, int *status)
{
    char *k_s = extract_("start",parameter_id);
    char *k_e = extract_("end",parameter_id);
    char *k_t = extract_("type",parameter_id);
    char *k_x = extract_("status",parameter_id);

    int ok =
        vevo_property_get(port,k_s,0,start)==VEVO_NO_ERROR &&
        vevo_property_get(port,k_e,0,end)==VEVO_NO_ERROR &&
        vevo_property_get(port,k_t,0,type)==VEVO_NO_ERROR &&
        vevo_property_get(port,k_x,0,status)==VEVO_NO_ERROR;

    free(k_s); free(k_e); free(k_t); free(k_x);
    return ok;
}

int keyframe_xml_pack( xmlNodePtr node, void *port, int parameter_id  )
{
	int i;
	int start = 0, end = 0, type = 0, status = 0, values_len = 0;
	int *values = NULL;
	
	char *k_s = extract_ ( "start", parameter_id );
	char *k_e = extract_ ( "end", parameter_id );
	char *k_t = extract_ ( "type", parameter_id );
	char *k_x = extract_ ( "status", parameter_id );
	char *k_d = extract_ ( "data", parameter_id );
	char *k_dn = extract_ ( "datalen", parameter_id );

	if( vevo_property_get( port, k_s, 0, &start ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e); free(k_t); free(k_x); free(k_d); free(k_dn);
			return 0;
	}
	if( vevo_property_get( port, k_e,   0, &end ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e); free(k_t); free(k_x); free(k_d); free(k_dn);
			return 0;
	}
	if( vevo_property_get( port, k_t,   0, &type ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e);free(k_t); free(k_x); free(k_d); free(k_dn);
			return 0;
	}
	if( vevo_property_get( port, k_x,   0, &status ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e);free(k_t); free(k_x); free(k_d); free(k_dn);
			return 0;
	}
	if( vevo_property_get( port, k_d,   0, &values ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e);free(k_t); free(k_x); free(k_d); free(k_dn);
			return 0;
	}
	if( vevo_property_get( port, k_dn,   0, &values_len ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e);free(k_t); free(k_x); free(k_d); free(k_dn);
			return 0;
	}

	put_xml_int( node, k_s, start );
	put_xml_int( node, k_e, end );
	put_xml_int( node, k_t, type );
	put_xml_int( node, k_x, status );
	put_xml_int( node, k_dn, values_len );

	for( i = 0; i < values_len ; i ++ ) 
	{
		char xmlbuf[128];
		snprintf((char*)xmlbuf,sizeof(xmlbuf), "%d %d", parameter_id,values[i] );
		put_xml_str( node, "value", xmlbuf );
	}

	free(k_s);
	free(k_e);
	free(k_t);
	free(k_x);
	free(k_d);
	free(k_dn);

	return 1;
}

int keyframe_xml_unpack(xmlDocPtr doc, xmlNodePtr node, void *port)
{
    if (!node || !port)
        return 0;

    int start=0,end=0,type=0,status=0;
    int values_len=0, frame=0;
    int *values=NULL;
    int parameter_id=-1;

    /* pass 1: find datalen */
    for (xmlNodePtr n=node; n; n=n->next) {
        if (!xmlStrncmp(n->name, (xmlChar*)"datalen_p", 9)) {
            values_len = get_xml_int(doc,n);
            if (values_len <= 0)
                return 0;
            values = vj_calloc(sizeof(int)*values_len);
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
    for (; node; node=node->next) {

        if (!xmlStrncmp(node->name,(xmlChar*)"start",5))
            start = get_xml_int(doc,node);

        else if (!xmlStrncmp(node->name,(xmlChar*)"end",3))
            end = get_xml_int(doc,node);

        else if (!xmlStrncmp(node->name,(xmlChar*)"type",4))
            type = get_xml_int(doc,node);

        else if (!xmlStrncmp(node->name,(xmlChar*)"status",6))
            status = get_xml_int(doc,node);

        else if (!xmlStrncmp(node->name,(xmlChar*)"value",5)) {
            int val=0;
            int pid = get_xml_2int(doc,node,&val);

            if (frame >= values_len)
                break;

            values[frame++] = val; //every frame has a parameter value
        }
    }

    if (parameter_id < 0) {
        free(values);
        return 0;
    }

    char *k_s  = extract_("start",parameter_id);
    char *k_e  = extract_("end",parameter_id);
    char *k_t  = extract_("type",parameter_id);
    char *k_x  = extract_("status",parameter_id);
    char *k_d  = extract_("data",parameter_id);
    char *k_dn = extract_("datalen",parameter_id);

    vevo_property_set(port,k_s,VEVO_ATOM_TYPE_INT,1,&start);
    vevo_property_set(port,k_e,VEVO_ATOM_TYPE_INT,1,&end);
    vevo_property_set(port,k_t,VEVO_ATOM_TYPE_INT,1,&type);
    vevo_property_set(port,k_x,VEVO_ATOM_TYPE_INT,1,&status);
    vevo_property_set(port,k_d,VEVO_ATOM_TYPE_VOIDPTR,1,&values);
    vevo_property_set(port,k_dn,VEVO_ATOM_TYPE_INT,1,&values_len);

    free(k_s); free(k_e); free(k_t);
    free(k_x); free(k_d); free(k_dn);

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
    if (!values || idx < 0 || idx > max_idx) {
        free(k_x); free(k_s); free(k_e); free(k_d);
        return 0;
    }

    *result = values[idx];

    free(k_x); free(k_s); free(k_e); free(k_d);
    return 1;
}
