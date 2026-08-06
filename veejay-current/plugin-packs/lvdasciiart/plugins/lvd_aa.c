/*
 * Copyright (c) 2019 Niels Elburg <nwelburg@gmail.com>
 *
 * This file is part of lvdasciiart
 *
 * LVDAsciiArt is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * LVDAsciiArt is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *  
 * You should have received a copy of the GNU Lesser General Public
 * License along with LVDAsciiAart; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/* There was an ASCII filter posted to the ffmpeg-devel mailing list.
 * This ASCII filter was slurped and modified to run as a livido plugin in veejay
 *
 * https://ffmpeg.org/pipermail/ffmpeg-devel/2014-June/159234.html
 */

/*
 * Copyright (c) 2014 Alexander Tumin <iamtakingiteasy@eientei.org>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *  
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * aa (ascii-art) video filter using aalib
 *
 * original filter by Alexander Tumin
 */
#ifndef IS_LIVIDO_PLUGIN
#define IS_LIVIDO_PLUGIN
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <aalib.h>
#include <libswscale/swscale.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_STROKER_H

#include "livido.h"
#include "utils.h"

LIVIDO_PLUGIN
#include "livido-utils.c" // NOSONAR: LiViDO plugin imports are translation-unit local

#define NUM_GLYPHS 256

typedef struct Glyph {
    FT_Glyph glyph;
    uint32_t code;
    FT_Bitmap bitmap;
    FT_BBox bbox;
    int bitmap_left;
    int bitmap_top;
} Glyph;

typedef struct {
    struct SwsContext *sws;
    uint8_t *buf[3];
    int w;
    int h;
    int flags;
} lvd_scale_t;

typedef struct {
    int x;
    int y;
    int w;
    int h;
    int initialized;
    int rollover;
    int density;
    int mode;
    int aa_flags;
} lvd_aa_runtime_t;

typedef struct {
    char *fontfile;
    int fontsize;
    int font;
    FT_Library library;
    FT_Face face;
    int xadvance;
    int yadvance;
    aa_context *aa;
    struct aa_hardware_params aa_params;
    struct aa_renderparams renderparams;
    lvd_aa_runtime_t runtime;
    uint8_t *buf[3];
    uint8_t *curframe_data[3];
    int curframe_width;
    int curframe_height;
    Glyph **glyphs;
    lvd_scale_t scale;
} lvd_aa_t;

typedef struct {
    int fontidx;
    char **fontlist;
    int maxfonts;
} font_param_t;

typedef struct {
    int width;
    int height;
    int x;
    int y;
    const uint8_t *bitbuffer;
    uint32_t bitmap_rows;
    uint32_t bitmap_width;
    uint32_t bitmap_pitch;
} lvd_glyph_region_t;

typedef struct {
    uint8_t *y;
    uint8_t *u;
    uint8_t *v;
    const uint8_t *opacity;
    const uint8_t *source_u;
    const uint8_t *source_v;
} lvd_glyph_planes_t;

static int is_ttf(const char *file)
{
    if(strstr( file, ".ttf" ) || strstr(file, ".TTF" ) )
        return 1;
    if(strstr( file, ".otf" ) || strstr(file, ".OTF" ) )
        return 1;

    return 0;
}

static int find_font_file(FT_Library lib, const char *path, char **fontlist, int *fontidx, int maxfonts)
{
    if(!path)
        return 0;

    struct stat st;
    livido_memset(&st, 0, sizeof(st));
    if(lstat(path, &st) < 0)
        return 0;

    if(S_ISLNK(st.st_mode)) {
        livido_memset(&st, 0, sizeof(st));
        if(stat(path, &st) < 0)
            return 0;
    }

    if(S_ISDIR(st.st_mode))
        return 1;
    if(!S_ISREG(st.st_mode) || !is_ttf(path) || *fontidx >= maxfonts)
        return 0;

    FT_Face face;
    if(FT_New_Face(lib, path, 0, &face) != 0)
        return 0;

    if(FT_Set_Pixel_Sizes(face, 0, 8) == 0) {
        fontlist[*fontidx] = strdup(path);
        if(fontlist[*fontidx])
            (*fontidx)++;
    }
    FT_Done_Face(face);
    return 0;
}

static int find_fonts(FT_Library lib, const char *path, char **fontlist, int *fontidx, int maxfonts)
{
    struct dirent **files = NULL;
    int n = scandir(path, &files, NULL, alphasort);
    if(n < 0)
        return 0;

    char tmp[2048];
    while(n-- > 0) {
        const char *name = files[n]->d_name;
        if(strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
            snprintf(tmp, sizeof(tmp), "%s/%s", path, name);
            if(find_font_file(lib, tmp, fontlist, fontidx, maxfonts))
                find_fonts(lib, tmp, fontlist, fontidx, maxfonts);
        }
        free(files[n]);
    }
    free(files);
    return 1;
}

static void load_fonts(FT_Library lib, char **fontlist, int *fontidx, int maxfonts)
{
    const char *home = getenv("HOME");
    char path[2048];

    if(home && *home) {
        snprintf(path, sizeof(path), "%s/.veejay/fonts", home);
        find_fonts(lib, path, fontlist, fontidx, maxfonts);

        snprintf(path, sizeof(path), "%s/.fonts", home);
        find_fonts(lib, path, fontlist, fontidx, maxfonts);
    }

    find_fonts(lib, "/usr/share/fonts/truetype", fontlist, fontidx, maxfonts);
    find_fonts(lib, "/usr/share/fonts/opentype", fontlist, fontidx, maxfonts);
    find_fonts(lib, "/usr/local/share/fonts", fontlist, fontidx, maxfonts);
}

static int load_font(lvd_aa_t *s)
{
    if( FT_New_Face(s->library, s->fontfile, 0, &s->face ) )
        return 1;
    return 0;
}

static void deinit(lvd_aa_t *s);

static int load_glyph(lvd_aa_t *s, uint32_t code, Glyph *glyph)
{
    if(FT_Load_Char(s->face, code, FT_LOAD_DEFAULT))
        return 1;

    glyph->code = code;
    glyph->glyph = NULL;
    if(FT_Get_Glyph(s->face->glyph, &glyph->glyph))
        return 1;

    if(FT_Glyph_To_Bitmap(&glyph->glyph, FT_RENDER_MODE_NORMAL, 0, 1)) {
        FT_Done_Glyph(glyph->glyph);
        glyph->glyph = NULL;
        return 1;
    }

    const FT_BitmapGlyphRec *bitmapglyph =
        (const FT_BitmapGlyphRec*)glyph->glyph;
    glyph->bitmap = bitmapglyph->bitmap;
    glyph->bitmap_left = bitmapglyph->left;
    glyph->bitmap_top = bitmapglyph->top;
    FT_Glyph_Get_CBox(glyph->glyph, ft_glyph_bbox_pixels, &glyph->bbox);
    return 0;
}

static int init(lvd_aa_t *s, const font_param_t *fp)
{
    int err = 1;

    s->fontfile = fp->fontlist[s->font];
    if(!s->fontfile)
        return 1;

    if(FT_Init_FreeType(&s->library))
        goto init_cleanup;
    if(load_font(s))
        goto init_cleanup;
    if(FT_Set_Pixel_Sizes(s->face, 0, s->fontsize))
        goto init_cleanup;

    s->glyphs = (Glyph**)livido_malloc(sizeof(Glyph*) * NUM_GLYPHS);
    if(!s->glyphs)
        goto init_cleanup;
    livido_memset(s->glyphs, 0, sizeof(Glyph*) * NUM_GLYPHS);

    for(int i = 0; i < NUM_GLYPHS; i++) {
        s->glyphs[i] = (Glyph*)livido_malloc(sizeof(Glyph));
        if(!s->glyphs[i])
            continue;
        livido_memset(s->glyphs[i], 0, sizeof(Glyph));
        if(load_glyph(s, (uint32_t)i, s->glyphs[i]) == 0)
            continue;
        livido_free(s->glyphs[i]);
        s->glyphs[i] = NULL;
    }

    s->runtime.initialized = 1;
    return 0;

init_cleanup:
    deinit(s);
    return err;
}

static void deinit(lvd_aa_t *s)
{
    if(!s)
        return;

    if(s->aa) {
        s->aa->driverdata = NULL;
        aa_close(s->aa);
        s->aa = NULL;
    }

    if(s->glyphs) {
        for(int i = 0; i < NUM_GLYPHS; i++) {
            if(!s->glyphs[i])
                continue;
            if(s->glyphs[i]->glyph)
                FT_Done_Glyph(s->glyphs[i]->glyph);
            livido_free(s->glyphs[i]);
        }
        livido_free(s->glyphs);
        s->glyphs = NULL;
    }

    if(s->face) {
        FT_Done_Face(s->face);
        s->face = NULL;
    }
    if(s->library) {
        FT_Done_FreeType(s->library);
        s->library = NULL;
    }

    s->runtime.initialized = 0;
}

static int vf_driver_init(const struct aa_hardware_params *source, const void *data, struct aa_hardware_params *dest, void **params)
{
    (void)data;
    (void)params;
    *dest = *source;
    return 1;
}

static void vf_driver_uninit(struct aa_context *context)
{
    (void)context;
}

static void vf_driver_setattr(aa_context *context, int attr)
{
    (void)context;
    (void)attr;
}

static void vf_driver_getsize(aa_context *context, int *width, int *height)
{
    lvd_aa_t *s = context->driverdata;
    if (s) {
        *width = s->runtime.w;
        *height = s->runtime.h;
    }
}

static void vf_driver_gotoxy(aa_context *context, int x, int y)
{
    lvd_aa_t *s = context->driverdata;
    if (s) {
        s->runtime.x = x;
        s->runtime.y = y;
    }
}

// Average
static void draw_glyph_Y(const lvd_glyph_region_t *region, lvd_glyph_planes_t *planes)
{
    for(uint32_t r = 0; r < region->bitmap_rows && (int)r + region->y < region->height; r++) {
        for(uint32_t c = 0; c < region->bitmap_width && (int)c + region->x < region->width; c++) {
            const uint32_t pos = r * region->bitmap_pitch + c;
            const int pixel = (int)c + region->x + ((region->y + (int)r) * region->width);
            if(planes->y[pixel] < 16)
                planes->y[pixel] = region->bitbuffer[pos];
            else
                planes->y[pixel] = (region->bitbuffer[pos] + planes->y[pixel]) >> 1;
        }
    }
}

static void draw_glyph_YasAlpha(const lvd_glyph_region_t *region, lvd_glyph_planes_t *planes)
{
    for(uint32_t r = 0; r < region->bitmap_rows && (int)r + region->y < region->height; r++) {
        for(uint32_t c = 0; c < region->bitmap_width && (int)c + region->x < region->width; c++) {
            const uint32_t pos = r * region->bitmap_pitch + c;
            const int pixel = (int)c + region->x + ((region->y + (int)r) * region->width);
            const uint8_t op1 = planes->opacity[pixel];
            const uint8_t op0 = 0xff - op1;
            planes->y[pixel] = (op0 * planes->y[pixel] + op1 * region->bitbuffer[pos]) >> 8;
        }
    }
}

static void draw_glyph_op(const lvd_glyph_region_t *region, lvd_glyph_planes_t *planes)
{
    for(uint32_t r = 0; r < region->bitmap_rows && (int)r + region->y < region->height; r++) {
        for(uint32_t c = 0; c < region->bitmap_width && (int)c + region->x < region->width; c++) {
            const uint32_t pos = r * region->bitmap_pitch + c;
            const int pixel = (int)c + region->x + ((region->y + (int)r) * region->width);
            const uint8_t op1 = planes->opacity[pixel];
            const uint8_t op0 = 0xff - op1;
            planes->y[pixel] = (op0 * planes->y[pixel] + op1 * region->bitbuffer[pos]) >> 8;
            planes->u[pixel] = planes->source_u[pixel];
            planes->v[pixel] = planes->source_v[pixel];
        }
    }
}

static void draw_glyph_op_nb(const lvd_glyph_region_t *region, lvd_glyph_planes_t *planes)
{
    for(uint32_t r = 0; r < region->bitmap_rows && (int)r + region->y < region->height; r++) {
        for(uint32_t c = 0; c < region->bitmap_width && (int)c + region->x < region->width; c++) {
            const uint32_t pos = r * region->bitmap_pitch + c;
            const int pixel = (int)c + region->x + ((region->y + (int)r) * region->width);
            const uint8_t op1 = planes->opacity[pixel];
            const uint8_t op0 = 0xff - op1;
            planes->y[pixel] = (op0 * planes->y[pixel] + op1 * region->bitbuffer[pos]) >> 8;
            if(region->bitbuffer[pos] > 0) {
                planes->u[pixel] = planes->source_u[pixel];
                planes->v[pixel] = planes->source_v[pixel];
            }
        }
    }
}

static void draw_glyph(lvd_aa_t *s, const FT_Bitmap *bitmap, int x, int y)
{
    const int width = s->curframe_width;
    const int height = s->curframe_height;

    if(x < 0 || x > width) {
        if(!s->runtime.rollover)
            return;
        if(x > width)
            x %= width;
        if(x < 0)
            x += width;
    }
    if(y < 0 || y > height) {
        if(!s->runtime.rollover)
            return;
        if(y > height)
            y %= height;
        if(y < 0)
            y += height;
    }

    const lvd_glyph_region_t region = {
        .width = width,
        .height = height,
        .x = x,
        .y = y,
        .bitbuffer = bitmap->buffer,
        .bitmap_rows = bitmap->rows,
        .bitmap_width = bitmap->width,
        .bitmap_pitch = bitmap->pitch
    };
    lvd_glyph_planes_t planes = {
        .y = s->curframe_data[0],
        .u = s->curframe_data[1],
        .v = s->curframe_data[2],
        .opacity = s->buf[0],
        .source_u = s->buf[1],
        .source_v = s->buf[2]
    };

    switch(s->runtime.mode) {
        case 0:
            draw_glyph_Y(&region, &planes);
            break;
        case 1:
            draw_glyph_YasAlpha(&region, &planes);
            break;
        case 2:
            draw_glyph_op(&region, &planes);
            break;
        case 3:
            draw_glyph_op_nb(&region, &planes);
            break;
        default:
            break;
    }
}

static void vf_driver_print(aa_context *context, const char *text)
{
    lvd_aa_t *s = context->driverdata;
    const char *c = text;
    Glyph *glyph = NULL;
    int cx;
    int cy;
    if(!s)
        return;

    while(*c) {
            Glyph dummy = { 0 };
            dummy.code = (uint32_t) *c;

            glyph = s->glyphs[ (dummy.code % 0xff) ];

            if (!glyph) {
                s->runtime.x += 1;
                c++;
                continue;
            }

            cx = (s->runtime.x * s->xadvance) + glyph->bitmap_left;
            cy = (s->runtime.y * s->yadvance) - glyph->bitmap_top;

            draw_glyph(s, &glyph->bitmap, cx, cy);

        s->runtime.x += 1;
        c++;
    }
}

static struct aa_driver vf_driver = {
    .shortname = "vf",
    .name = "video filter driver",
    .init = vf_driver_init,
    .uninit = vf_driver_uninit,
    .setattr = vf_driver_setattr,
    .getsize = vf_driver_getsize,
    .print = vf_driver_print,
    .gotoxy = vf_driver_gotoxy
};

static int config_props_in(lvd_aa_t *s, int w, int h, int density)
{
    if (!s->aa) {
        s->runtime.w = w/density;
        s->runtime.h = h/density;

        s->aa_params.supported = s->runtime.aa_flags;
        s->aa_params.width = s->runtime.w;
        s->aa_params.height = s->runtime.h;

        s->aa = aa_init(&vf_driver, &s->aa_params, 0);

        if (!s->aa) {
            return 1;
        }
        s->aa->driverdata = s;
    }
    return 0;
}


int init_instance( livido_port_t *my_instance )
{
    int w = 0;
    int h = 0;
    
    lvd_extract_dimensions( my_instance, "out_channels", &w, &h );

    lvd_aa_t *aa = (lvd_aa_t*) livido_malloc( sizeof(lvd_aa_t));
    if(!aa) {
        return LIVIDO_ERROR_MEMORY_ALLOCATION;
    }

    livido_memset(aa,0,sizeof(lvd_aa_t));
    
    aa->scale.buf[0] = (uint8_t*) livido_malloc( sizeof(uint8_t) * ( w * h) );
    if(!aa->scale.buf[0]) {
        livido_free(aa);
        return LIVIDO_ERROR_MEMORY_ALLOCATION;
    }

    aa->scale.flags = SWS_FAST_BILINEAR;
    aa->scale.w = -1;
    aa->scale.h = -1;

    aa->fontsize = -1;
    aa->runtime.density = -1;
    aa->font = -1;
    aa->runtime.aa_flags = 0;

    aa->buf[0] = (uint8_t*)livido_malloc(sizeof(uint8_t) * (w * h * 3));
    if(!aa->buf[0]) {
        livido_free(aa->scale.buf[0]);
        livido_free(aa);
        return LIVIDO_ERROR_MEMORY_ALLOCATION;
    }
    aa->buf[1] = aa->buf[0] + w * h;
    aa->buf[2] = aa->buf[1] + w * h;

    livido_property_set( my_instance, "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR,1, &aa);

    return LIVIDO_NO_ERROR;
}


int deinit_instance( livido_port_t *my_instance )
{
    lvd_aa_t *aa = NULL;
    if( livido_property_get( my_instance, "PLUGIN_private", 0, &aa ) == LIVIDO_NO_ERROR ) {
        if( aa ) {

            if(aa->runtime.initialized || aa->aa || aa->glyphs || aa->face || aa->library)
                deinit(aa);
            
            if(aa->scale.buf[0]) {
                livido_free(aa->scale.buf[0]);
            }

            if(aa->buf[0]) {
                livido_free(aa->buf[0]);
            }

            if(aa->scale.sws) {
                sws_freeContext( aa->scale.sws );
            }

            livido_free(aa);
            aa = NULL;
        }       
        livido_property_set( my_instance, "PLUGIN_private", LIVIDO_ATOM_TYPE_VOIDPTR, 0, NULL );
    }
    return LIVIDO_NO_ERROR;
}

int     process_instance( livido_port_t *my_instance, double timecode )
{
    (void)timecode;
    uint8_t *A[4] = {NULL,NULL,NULL,NULL};
    uint8_t *O[4]= {NULL,NULL,NULL,NULL};

    int palette;
    int w;
    int h;
    
    lvd_aa_t *aa = NULL;
    livido_property_get( my_instance, "PLUGIN_private", 0, &aa );
    
    if( aa == NULL )
        return LIVIDO_ERROR_INTERNAL;

    int error  = lvd_extract_channel_values( my_instance, "out_channels", 0, &w,&h, O,&palette );
    if( error != LIVIDO_NO_ERROR )
        return LIVIDO_ERROR_NO_OUTPUT_CHANNELS;

    error = lvd_extract_channel_values( my_instance, "in_channels" , 0, &w, &h, A, &palette );
    if( error != LIVIDO_NO_ERROR )
        return LIVIDO_ERROR_NO_INPUT_CHANNELS;

    int font = lvd_extract_param_index( my_instance,"in_parameters", 0 );
    int font_size = lvd_extract_param_index( my_instance,"in_parameters", 1 );
    int brightness = lvd_extract_param_index( my_instance, "in_parameters", 2 );
    int contrast = lvd_extract_param_index( my_instance, "in_parameters", 3);
    int gamma = lvd_extract_param_index( my_instance, "in_parameters", 4);
    int inversion = lvd_extract_param_index( my_instance, "in_parameters", 5 );
    int dither = lvd_extract_param_index( my_instance, "in_parameters", 6 );
    int rollover = lvd_extract_param_index( my_instance, "in_parameters", 7 );
    int density = lvd_extract_param_index( my_instance, "in_parameters", 8 );
    int mode = lvd_extract_param_index( my_instance, "in_parameters", 9 );
    int aaflagmode = lvd_extract_param_index( my_instance, "in_parameters", 10 );

    font_param_t *fp = NULL;
    livido_port_t *font_port = NULL;
    if( livido_property_get( my_instance, "in_parameters", 0, &font_port ) != LIVIDO_NO_ERROR )
        return LIVIDO_ERROR_INTERNAL;
    if( livido_property_get( font_port, "PLUGIN_param_private", 0, &fp ) != LIVIDO_NO_ERROR )
        return LIVIDO_ERROR_INTERNAL;

    int re_init = !aa->runtime.initialized;

    int aa_flag = AA_NORMAL_MASK | AA_REVERSE_MASK;
    if( aaflagmode == 1 )
        aa_flag = AA_NORMAL_MASK | AA_REVERSE_MASK | AA_EXTENDED;

    if( aa_flag != aa->runtime.aa_flags ) {
        aa->runtime.aa_flags = aa_flag;
        re_init = 1;
    }

    if( density != aa->runtime.density ) {
        aa->runtime.density = density;
        re_init = 1;
    }

    if( font_size != aa->fontsize ) {
        aa->fontsize = font_size;
        re_init = 1;
    }
    
    if( font != aa->font ) {
        aa->font = font;
        re_init = 1;
    }
 
    aa->xadvance = density * 2;
    aa->yadvance = density * 4;
    aa->runtime.rollover = rollover;
    aa->runtime.mode = mode;

    if(re_init) {
        if( aa->runtime.initialized )
            deinit(aa);

        if(aa->scale.sws) {
            sws_freeContext( aa->scale.sws );
            aa->scale.sws = NULL;
        }

        if( init(aa, fp) ) {
            return LIVIDO_ERROR_INTERNAL;
        }

        if( config_props_in(aa,w,h,density) ) {
            deinit(aa);
            return LIVIDO_ERROR_INTERNAL;
        }
    }

    uint8_t *framebuffer = aa_image(aa->aa);
    uint8_t *fb[3] = { framebuffer, NULL, NULL };

    if( aa->scale.sws == NULL ) {
        aa->scale.w = (w/density);
        aa->scale.h = (h/density);
        aa->scale.sws = sws_getContext( w, h, AV_PIX_FMT_YUV444P, aa->scale.w, aa->scale.h, AV_PIX_FMT_GRAY8, aa->scale.flags,NULL,NULL,NULL);
        if(aa->scale.sws == NULL)
            return LIVIDO_ERROR_INTERNAL;
    }

    int src_strides[3] = { w, w, w };
    int dst_strides[3] = { aa->scale.w, 0, 0 };

    sws_scale( aa->scale.sws,
             (const uint8_t * const *)A,
             src_strides,
             0,
             h,
             fb,
             dst_strides );

    aa->renderparams.bright = brightness;
    aa->renderparams.contrast = contrast;
    aa->renderparams.gamma = (double) gamma * 0.1;
    aa->renderparams.inversion = inversion;
    aa->renderparams.dither = dither;

    aa->curframe_data[0] = O[0];
    aa->curframe_data[1] = O[1];
    aa->curframe_data[2] = O[2];

    if ( mode == 1 ) {
        livido_memcpy( aa->buf[0], A[0], w * h );
    }
    else if ( mode == 2 ) {
        livido_memcpy( aa->buf[0], A[0], w * h );
        livido_memcpy( aa->buf[1], A[1], w * h );
        livido_memcpy( aa->buf[2], A[2], w * h );
    }

    livido_memset( O[0], 0, w * h );
    livido_memset( O[1], 128, w * h );
    livido_memset( O[2], 128, w * h );

    aa->curframe_width = w;
    aa->curframe_height = h;
    aa_render(aa->aa, &aa->renderparams, 0, 0, aa->runtime.w, aa->runtime.h);
    aa_flush(aa->aa);

    return LIVIDO_NO_ERROR;
}

static int init_font_param( livido_port_t *port )
{
    font_param_t *fp = (font_param_t*) livido_malloc(sizeof(font_param_t));
    fp->maxfonts = 1024;
    fp->fontlist = (char**) livido_malloc(sizeof(char*) * fp->maxfonts );
    livido_memset(fp->fontlist,0,sizeof(char*) * fp->maxfonts );
    fp->fontidx = 0;

    FT_Library lib;
    FT_Init_FreeType(&lib);
    load_fonts(lib, fp->fontlist, &(fp->fontidx), fp->maxfonts);
    FT_Done_FreeType(lib);

    livido_property_set( port, "PLUGIN_param_private", LIVIDO_ATOM_TYPE_VOIDPTR,1,&fp );
    return (fp->fontidx - 1);
}

livido_port_t   *livido_setup(livido_setup_t list[], int version)

{
    LIVIDO_IMPORT(list);

    livido_port_t *port = NULL;
    livido_port_t *in_params[11];
    livido_port_t *in_chans[1];
    livido_port_t *out_chans[1];
    livido_port_t *info = NULL;
    livido_port_t *filter = NULL;

    //@ setup root node, plugin info
    info = livido_port_new( LIVIDO_PORT_TYPE_PLUGIN_INFO );
    port = info;

    livido_set_string_value( port, "maintainer", "Niels");
    livido_set_string_value( port, "version","1");
    
    filter = livido_port_new( LIVIDO_PORT_TYPE_FILTER_CLASS );
    livido_set_int_value( filter, "api_version", LIVIDO_API_VERSION );

    //@ setup function pointers
    livido_set_voidptr_value( filter, "deinit_func", &deinit_instance );
    livido_set_voidptr_value( filter, "init_func", &init_instance );
    livido_set_voidptr_value( filter, "process_func", &process_instance );
    port = filter;

    //@ meta information
    livido_set_string_value( port, "name", "AsciiArt"); 
    livido_set_string_value( port, "description", "AsciiArt with AAlib");
    livido_set_string_value( port, "author", "Alexander Tumin"); 
        
    livido_set_int_value( port, "flags", 0);
    livido_set_string_value( port, "license", "LGPL2.1");
    livido_set_int_value( port, "version", 1);
    
    //@ some palettes veejay-classic uses
    int palettes0[] = {
        LIVIDO_PALETTE_YUV444P,
        0,
    };
    
    //@ setup output channel
    out_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
    port = out_chans[0];
    
        livido_set_string_value( port, "name", "Output Channel");
        livido_set_int_array( port, "palette_list", 2, palettes0);
        livido_set_int_value( port, "flags", 0);

        in_chans[0] = livido_port_new( LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE );
        port = in_chans[0];
        livido_set_string_value( port, "name", "Input Channel");
            livido_set_int_array( port, "palette_list", 2, palettes0);
            livido_set_int_value( port, "flags", 0);
    
    in_params[0] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
    port = in_params[0];

        int num_fonts = init_font_param(port);

        livido_set_string_value(port, "name", "Font" );
        livido_set_string_value(port, "kind", "INDEX" );

        livido_set_int_value(port, "default", 0 );
        livido_set_int_value(port, "min" , 0);
        livido_set_int_value(port, "max", num_fonts);

        livido_set_string_value( port, "description" ,"Font");


    in_params[1] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
    port = in_params[1];

        livido_set_string_value(port, "name", "Font Size" );
        livido_set_string_value(port, "kind", "INDEX" );

        livido_set_int_value(port, "default", 8 );
        livido_set_int_value(port, "min" ,4);
        livido_set_int_value(port, "max", 24);

        livido_set_string_value( port, "description" ,"Font Size");

    in_params[2] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
    port = in_params[2];

        livido_set_string_value(port, "name", "Brightness" );
        livido_set_string_value(port, "kind", "INDEX" );
        livido_set_int_value(port, "default", 50 );
        livido_set_int_value(port, "min" ,0);
        livido_set_int_value(port, "max", 255);

        livido_set_string_value( port, "description" ,"Brightness");
    
    in_params[3] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
    port = in_params[3];

        livido_set_string_value(port, "name", "Contrast" );
        livido_set_string_value(port, "kind", "INDEX" );
        
        livido_set_int_value(port, "default", 64 );
        livido_set_int_value(port, "min" ,0);
        livido_set_int_value(port, "max", 255);
    
        livido_set_string_value( port, "description" ,"Contrast");

    in_params[4] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
    port = in_params[4];

        livido_set_string_value(port, "name", "Gamma" );
        livido_set_string_value(port, "kind", "INDEX" );
        
        livido_set_int_value(port, "default", 14);
        livido_set_int_value(port, "min" ,0);
        livido_set_int_value(port, "max", 100);
        
        livido_set_string_value( port, "description" ,"Gamma");

    in_params[5] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
    port = in_params[5];

        livido_set_string_value(port, "name", "Inversion" );
        livido_set_string_value(port, "kind", "INDEX" );

        livido_set_int_value(port, "default", 0);
        livido_set_int_value(port, "min" ,0);
        livido_set_int_value(port, "max", 1);
        
        livido_set_string_value( port, "description" ,"Inversion");

    in_params[6] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
    port = in_params[6];

        livido_set_string_value(port, "name", "Dithering" );
        livido_set_string_value(port, "kind", "INDEX" );

        livido_set_int_value(port, "default", 2);
        livido_set_int_value(port, "min" ,0);
        livido_set_int_value(port, "max", 3);
        
        livido_set_string_value( port, "description" ,"Dithering");

    in_params[7] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
    port = in_params[7];

        livido_set_string_value(port, "name", "Rollover" );
        livido_set_string_value(port, "kind", "INDEX" );

        livido_set_int_value(port, "default", 0);
        livido_set_int_value(port, "min" ,0);
        livido_set_int_value(port, "max", 1);
        
        livido_set_string_value( port, "description" ,"Rollover");

    in_params[8] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
    port = in_params[8];

        livido_set_string_value(port, "name", "Density" );
        livido_set_string_value(port, "kind", "INDEX" );

        livido_set_int_value(port, "default", 2);
        livido_set_int_value(port, "min" ,2);
        livido_set_int_value(port, "max", 4);
        
        livido_set_string_value( port, "description" ,"Density");

    in_params[9] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
    port = in_params[9];

        livido_set_string_value(port, "name", "Mode" );
        livido_set_string_value(port, "kind", "INDEX" );

        livido_set_int_value(port, "default", 0);
        livido_set_int_value(port, "min" ,0);
        livido_set_int_value(port, "max", 3);
        
        livido_set_string_value( port, "description" ,"Mode");


    in_params[10] = livido_port_new( LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE );
    port = in_params[10];

        livido_set_string_value(port, "name", "Extended ASCII" );
        livido_set_string_value(port, "kind", "INDEX" );

        livido_set_int_value(port, "default", 0);
        livido_set_int_value(port, "min" ,0);
        livido_set_int_value(port, "max", 1);
        
        livido_set_string_value( port, "description" ,"Extended ASCII");

    //@ setup the nodes
    livido_set_portptr_array( filter, "in_parameter_templates", 11, in_params );
    livido_set_portptr_array( filter, "out_channel_templates", 1, out_chans );
    livido_set_portptr_array( filter, "in_channel_templates",1, in_chans );

    livido_set_portptr_value(info, "filters", filter);
    return info;
}
