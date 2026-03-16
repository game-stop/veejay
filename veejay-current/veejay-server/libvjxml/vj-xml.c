/* 
 * Linux VeeJay
 *
 * Copyright(C)2016 Niels Elburg <nwelburg@gmail.com>
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
 */
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libvjxml/vj-xml.h>
#include <veejaycore/vjmem.h>
static char *UTF8toLAT1(unsigned char *in)
{
    if (!in)
        return NULL;

    int in_size = strlen((char*)in);
    unsigned char *out = vj_calloc((size_t)in_size + 1);
    if (!out)
        return NULL;

    int out_size = in_size;
    int tmp_in   = in_size;

    if (UTF8Toisolat1(out, &out_size, in, &tmp_in) != 0) {
        veejay_memcpy(out, in, in_size);
        out_size = in_size;
    }

    out[out_size] = '\0';
    return (char*)out;
}

char *get_xml_str(xmlDocPtr doc, xmlNodePtr node)
{
    if (!node)
        return NULL;

    xmlChar *tmp = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
    if (!tmp)
        return NULL;

    char *out = UTF8toLAT1(tmp);
    xmlFree(tmp);

    return out;
}

int get_xml_int(xmlDocPtr doc, xmlNodePtr node)
{
    char *ch = get_xml_str(doc, node);
    int res = 0;

    if (ch) {
        res = atoi(ch);
        free(ch);
    }

    return res;
}


void get_xml_str_n(xmlDocPtr doc, xmlNodePtr node, char *val, size_t len)
{
    if (!val || len == 0 || !node)
        return;

    val[0] = '\0';

    xmlChar *xmlTemp = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
    if (!xmlTemp)
        return;

    char *chTemp = UTF8toLAT1(xmlTemp);
    xmlFree(xmlTemp);

    if (!chTemp)
        return;

    snprintf(val, len, "%s", chTemp);

    free(chTemp);
}

float get_xml_float(xmlDocPtr doc, xmlNodePtr node)
{
    char *ch = get_xml_str(doc, node);
    float val = 0.0f;

    if (ch) {
        sscanf(ch, "%f", &val);
        free(ch);
    }

    return val;
}

void get_xml_3int(xmlDocPtr doc, xmlNodePtr node, int *first, int *second, int *third)
{
    if (first)  *first  = 0;
    if (second) *second = 0;
    if (third)  *third  = 0;

    char *ch = get_xml_str(doc, node);
    if (!ch)
        return;

    sscanf(ch, "%d %d %d", first ? first : &(int){0}, second ? second : &(int){0}, third ? third : &(int){0});
    
    free(ch);
}

int get_xml_2int(xmlDocPtr doc, xmlNodePtr node, int *second)
{
    int first = 0;
    if (second)
        *second = 0;

    char *ch = get_xml_str(doc, node);
    if (!ch)
        return 0;

    sscanf(ch, "%d %d", &first, second ? second : &(int){0});
    free(ch);

    return first;
}

void put_xml_int(xmlNodePtr node, const char *key, int value)
{
    if (!node || !key)
        return;

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%d", value);

    xmlNewChild(node, NULL, (const xmlChar*)key, (const xmlChar*)buffer);
}

void put_xml_str(xmlNodePtr node, const char *key, char *value)
{
    if (!node || !key || !value)
        return;

    xmlNewChild(node, NULL, (const xmlChar*)key, (const xmlChar*)value);
}


void put_xml_float(xmlNodePtr node, const char *key, float value)
{
    if (!node || !key)
        return;

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%f", value);

    xmlNewChild(node, NULL, (const xmlChar*)key, (const xmlChar*)buffer);
}
