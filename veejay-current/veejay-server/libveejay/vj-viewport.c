/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2008 Niels Elburg <nwelburg@gmail.com>
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
 * Viewport with Perspective Transform Estimation for Veejay
 *
 * Resources:
 *	Gimp 1.0,2.0   (Perspective transformation (C) Spencer Kimball & Peter Mattis)
 *	Cinelerra      (Motion plugin, no author in any file present. GPL2).
 *	Xine           (bresenham line drawing routine)
 *
 */
#include <config.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <veejaycore/defs.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include <libvje/vje.h>
#include <libvje/libvje.h>
#include <libveejay/vj-viewport.h>
#include <libveejay/vj-output-mesh.h>
#include <libvje/effects/opacity.h>
#include <veejaycore/yuvconv.h>
#include <libavutil/pixfmt.h>
#include <veejaycore/vjmem.h>
#include <libveejay/vj-viewport-cfg.h>
#include <libveejay/vj-viewport.h>
#include <math.h>
#include <veejaycore/avcommon.h>

#define X0 0
#define Y0 1
#define X1 2
#define Y1 3
#define X2 4
#define Y2 5
#define X3 6
#define Y3 7
#ifndef MIN
#define MIN(a,b) ( (a)>(b) ? (b) : (a) )
#endif
#ifndef MAX
#define MAX(a,b) ( (a)>(b) ? (a) : (b) )
#endif

#define clamp1(x, y, z) ((x) = ((x) < (y) ? (y) : ((x) > (z) ? (z) : (x))))
#define distance1(x1,y1,x2,y2) (  sqrt( (x1 - x2) * (x1 - x2) + ( y1 - y2 ) * (y1 -y2 ) ) )

#define round1(x) ( (int32_t)( (x>0) ? (x) + 0.5 : (x)  - 0.5 ))
#define min4(a,b,c,d) MIN(MIN(MIN(a,b),c),d)
#define max4(a,b,c,d) MAX(MAX(MAX(a,b),c),d)
#if defined(ARCH_X86) || defined(ARCH_X86_64)
#include <xmmintrin.h>
#endif

#define GRID_STEP 1
#define GRID_START 44
typedef struct
{
	float m[4][4];
} matrix_t;

typedef struct
{
	void *scaler;
    VJFrame *src_frame;
    VJFrame *dst_frame;
	uint8_t *buf[3];
	float	scale;
	float   sx;
	float	sy;
	int	sw;
	int	sh;
} ui_t;

typedef struct
{
	int x;
	int y;
} grid_t;


typedef struct
{
	int32_t x,y;
	int32_t h,w;
	int32_t x0,y0,w0,h0;
	float points[9];
	int   users[4];
	float usermouse[6];
	int   userm;
	int   user;
	int	save;
	int   user_ui;
	int   user_reverse;
	int   user_mode;
	int	grid_resolution;
	int	grid_width;
	int	grid_height;
	int   renew;
	int   disable;
	int   snap_marker;
	int   marker_size;
	float x1;
	float x2;
	float x3;
	float x4;
	float y1;
	float y2;
	float y3;
	float y4;
	int32_t *map;
	uint8_t *img[4];
	matrix_t *M;
	matrix_t *m;
	matrix_t *T;
	char *help;
	uint8_t  grid_val;
	int	parameters[8];
	int32_t tx1,tx2,ty1,ty2;
	int32_t ttx1,ttx2,tty1,tty2;
	int	mode;
	void *sender;
	uint32_t	seq_id;
	ui_t	*ui;
	int 	saved_w;
	int	saved_h;
	grid_t	*grid;	
	int	grid_mode;
	int	initial_active;
	vj_output_mesh *mesh;
	int mesh_selected_point;
	int mesh_hover_point;
	char config_path[1024];
	int config_port;
	int config_frontback;
	int config_bound;
} viewport_t;

#define VIEWPORT_CONFIG_VERSION 2
#define VIEWPORT_CONFIG_MAX_PROFILES 64
#define VIEWPORT_CONFIG_MAX_POINTS (17 * 17)

typedef struct {
    int valid;
    int port;
    int active;
    int frontback;
    int reverse;
    int columns;
    int rows;
    int marker_size;
    int grid_mode;
    int grid_color;
    float source_x;
    float source_y;
    float source_width;
    float source_height;
    float points[VIEWPORT_CONFIG_MAX_POINTS * 2];
} viewport_port_profile_t;


static	float		msx(viewport_t *v, float x);
static	float		msy(viewport_t *v, float y);

static	float		vsx(viewport_t *v, float x);
static	float		vsy(viewport_t *v, float y);

static void		viewport_draw_col( void *data, uint8_t *img, uint8_t *u, uint8_t *v );
static int		viewport_update_perspective( viewport_t *v, float *values );
static void		viewport_process( viewport_t *p );
static void		viewport_compute_grid( viewport_t *v );

static void viewport_mesh_sync_legacy_corners(viewport_t *v);

static int viewport_mesh_event_index(const viewport_t *v, int point)
{
    int columns = 0;
    int rows = 0;
    vj_output_mesh_get_grid(v->mesh, &columns, &rows);

    if(point < 1 || point > columns * rows)
        return -1;

    if(columns == 2 && rows == 2) {
        static const int legacy_to_mesh[4] = { 0, 1, 3, 2 };
        return legacy_to_mesh[point - 1];
    }

    return point - 1;
}

static int viewport_mesh_event_number(const viewport_t *v, int index)
{
    int columns = 0;
    int rows = 0;
    vj_output_mesh_get_grid(v->mesh, &columns, &rows);

    if(index < 0 || index >= columns * rows)
        return 0;
    if(columns == 2 && rows == 2) {
        static const int mesh_to_legacy[4] = { 1, 2, 4, 3 };
        return mesh_to_legacy[index];
    }
    return index + 1;
}

static int viewport_mesh_is_corner(const viewport_t *v, int index)
{
    int columns = 0;
    int rows = 0;
    vj_output_mesh_get_grid(v->mesh, &columns, &rows);
    return index == 0 || index == columns - 1 ||
           index == (rows - 1) * columns ||
           index == rows * columns - 1;
}

static int viewport_mesh_corner_role(const viewport_t *v, int index)
{
    int columns = 0;
    int rows = 0;
    vj_output_mesh_get_grid(v->mesh, &columns, &rows);
    if(index == 0)
        return 0;
    if(index == columns - 1)
        return 1;
    if(index == rows * columns - 1)
        return 2;
    if(index == (rows - 1) * columns)
        return 3;
    return -1;
}

static void viewport_mesh_refresh_legacy_transform(viewport_t *v)
{
    if(!v->map)
        return;
    float points[9] = {
        v->x1, v->y1, v->x2, v->y2,
        v->x3, v->y3, v->x4, v->y4, 0.0f
    };
    viewport_update_perspective(v, points);
}

static int viewport_mesh_create(viewport_t *v, int width, int height,
                                int columns, int rows)
{
    v->mesh = vj_output_mesh_create(width, height, width, height, columns, rows);
    if(v->mesh)
        vj_output_mesh_set_thread_count(v->mesh, vje_max_threads(width * height));
    v->mesh_selected_point = 0;
    v->mesh_hover_point = -1;
    return v->mesh != NULL;
}

static int viewport_mesh_copy_scaled(const viewport_t *source, viewport_t *target,
                                     float scale_x, float scale_y)
{
    int columns = 0;
    int rows = 0;
    vj_output_mesh_get_grid(source->mesh, &columns, &rows);

    if(!vj_output_mesh_set_grid(target->mesh, columns, rows))
        return 0;

    const int count = vj_output_mesh_point_count(source->mesh);
    for(int i = 0; i < count; i++) {
        vj_output_mesh_point point;
        if(!vj_output_mesh_get_point(source->mesh, i, &point) ||
           !vj_output_mesh_set_point(target->mesh, i,
                                     point.x * scale_x, point.y * scale_y))
            return 0;
    }

    if(!vj_output_mesh_set_source_rect(target->mesh,
                                       (float)target->x0, (float)target->y0,
                                       (float)target->w0, (float)target->h0) ||
       !vj_output_mesh_compile(target->mesh))
        return 0;

    target->mesh_selected_point = source->mesh_selected_point;
    if(target->mesh_selected_point < 0 || target->mesh_selected_point >= count)
        target->mesh_selected_point = 0;
    target->mesh_hover_point = -1;

    viewport_mesh_sync_legacy_corners(target);
    vj_output_mesh_get_bounds(target->mesh,
                              &target->ttx1, &target->tty1,
                              &target->ttx2, &target->tty2);
    return 1;
}

static void viewport_mesh_sync_legacy_corners(viewport_t *v)
{
    int columns = 0;
    int rows = 0;
    vj_output_mesh_point point;
    vj_output_mesh_get_grid(v->mesh, &columns, &rows);
    if(columns < 2 || rows < 2)
        return;

    if(vj_output_mesh_get_point(v->mesh, 0, &point)) {
        v->x1 = point.x * 100.0f / (float)v->w;
        v->y1 = point.y * 100.0f / (float)v->h;
    }
    if(vj_output_mesh_get_point(v->mesh, columns - 1, &point)) {
        v->x2 = point.x * 100.0f / (float)v->w;
        v->y2 = point.y * 100.0f / (float)v->h;
    }
    if(vj_output_mesh_get_point(v->mesh, rows * columns - 1, &point)) {
        v->x3 = point.x * 100.0f / (float)v->w;
        v->y3 = point.y * 100.0f / (float)v->h;
    }
    if(vj_output_mesh_get_point(v->mesh, (rows - 1) * columns, &point)) {
        v->x4 = point.x * 100.0f / (float)v->w;
        v->y4 = point.y * 100.0f / (float)v->h;
    }
}

static int viewport_mesh_sync_quad(viewport_t *v)
{
    if(!v->mesh)
        return 0;

    if(!vj_output_mesh_set_source_rect(v->mesh,
                                       (float)v->x0, (float)v->y0,
                                       (float)v->w0, (float)v->h0))
        return 0;

    int columns = 0;
    int rows = 0;
    vj_output_mesh_get_grid(v->mesh, &columns, &rows);
    if(columns == 2 && rows == 2) {
        if(!vj_output_mesh_set_quad(v->mesh,
                                    v->x1 * v->w / 100.0f, v->y1 * v->h / 100.0f,
                                    v->x2 * v->w / 100.0f, v->y2 * v->h / 100.0f,
                                    v->x3 * v->w / 100.0f, v->y3 * v->h / 100.0f,
                                    v->x4 * v->w / 100.0f, v->y4 * v->h / 100.0f))
            return 0;
    }
    else {
        vj_output_mesh_set_point(v->mesh, 0,
                                 v->x1 * v->w / 100.0f,
                                 v->y1 * v->h / 100.0f);
        vj_output_mesh_set_point(v->mesh, columns - 1,
                                 v->x2 * v->w / 100.0f,
                                 v->y2 * v->h / 100.0f);
        vj_output_mesh_set_point(v->mesh, rows * columns - 1,
                                 v->x3 * v->w / 100.0f,
                                 v->y3 * v->h / 100.0f);
        vj_output_mesh_set_point(v->mesh, (rows - 1) * columns,
                                 v->x4 * v->w / 100.0f,
                                 v->y4 * v->h / 100.0f);
    }

    if(!vj_output_mesh_compile(v->mesh))
        return 0;

    vj_output_mesh_get_bounds(v->mesh,
                              &v->ttx1, &v->tty1,
                              &v->ttx2, &v->tty2);
    return 1;
}

static void viewport_profile_init(viewport_port_profile_t *profile)
{
    memset(profile, 0, sizeof(*profile));
    profile->frontback = 1;
    profile->reverse = 1;
    profile->columns = 2;
    profile->rows = 2;
    profile->marker_size = 4;
    profile->grid_mode = 0;
    profile->grid_color = 0xff;
    profile->source_width = 100.0f;
    profile->source_height = 100.0f;
}

static int viewport_profile_valid(const viewport_port_profile_t *profile)
{
    if(!profile || !profile->valid || !profile->active ||
       profile->port <= 0 || profile->port > 65535 ||
       profile->columns < 2 || profile->columns > 17 ||
       profile->rows < 2 || profile->rows > 17 ||
       profile->marker_size <= 0 || profile->marker_size > 128 ||
       profile->grid_mode < 0 || profile->grid_mode > 2 ||
       profile->grid_color < 0 || profile->grid_color > 255 ||
       !isfinite(profile->source_x) || !isfinite(profile->source_y) ||
       !isfinite(profile->source_width) || !isfinite(profile->source_height) ||
       profile->source_x < 0.0f || profile->source_y < 0.0f ||
       profile->source_width <= 0.0f || profile->source_height <= 0.0f ||
       profile->source_x + profile->source_width > 100.0001f ||
       profile->source_y + profile->source_height > 100.0001f)
        return 0;

    const int point_count = profile->columns * profile->rows;
    if(point_count > VIEWPORT_CONFIG_MAX_POINTS)
        return 0;
    for(int point = 0; point < point_count; point++) {
        if(!isfinite(profile->points[point * 2]) ||
           !isfinite(profile->points[point * 2 + 1]))
            return 0;
    }
    return 1;
}

static int viewport_profiles_load(const char *path,
                                  viewport_port_profile_t *profiles,
                                  int max_profiles)
{
    FILE *fd;
    char line[512];
    int version = 0;
    int count = 0;
    viewport_port_profile_t current;
    int in_profile = 0;
    int points_expected = 0;
    int points_seen = 0;

    if(!path || !profiles || max_profiles <= 0)
        return 0;

    fd = fopen(path, "rb");
    if(!fd)
        return 0;

    if(!fgets(line, sizeof(line), fd) ||
       sscanf(line, "VJVIEWPORT %d", &version) != 1 ||
       version != VIEWPORT_CONFIG_VERSION) {
        fclose(fd);
        veejay_msg(VEEJAY_MSG_WARNING,
                   "Ignoring incompatible viewport configuration %s", path);
        return 0;
    }

    viewport_profile_init(&current);
    while(fgets(line, sizeof(line), fd)) {
        int value = 0;
        if(line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        if(sscanf(line, "PORT %d", &value) == 1) {
            if(in_profile && viewport_profile_valid(&current) &&
               points_expected == current.columns * current.rows &&
               points_seen == points_expected && count < max_profiles)
                profiles[count++] = current;
            viewport_profile_init(&current);
            current.port = value;
            current.valid = 1;
            in_profile = 1;
            points_expected = 0;
            points_seen = 0;
            continue;
        }

        if(!in_profile)
            continue;

        if(sscanf(line, "ACTIVE %d", &value) == 1) {
            current.active = value ? 1 : 0;
            continue;
        }
        if(sscanf(line, "FRONTBACK %d", &value) == 1) {
            current.frontback = value ? 1 : 0;
            continue;
        }
        if(sscanf(line, "REVERSE %d", &value) == 1) {
            current.reverse = value ? 1 : 0;
            continue;
        }
        if(sscanf(line, "GRID %d %d", &current.columns, &current.rows) == 2) {
            continue;
        }
        if(sscanf(line, "SOURCE %f %f %f %f",
                  &current.source_x, &current.source_y,
                  &current.source_width, &current.source_height) == 4) {
            continue;
        }
        if(sscanf(line, "MARKER %d %d %d",
                  &current.marker_size, &current.grid_mode,
                  &current.grid_color) == 3) {
            continue;
        }
        if(sscanf(line, "POINTS %d", &points_expected) == 1) {
            points_seen = 0;
            continue;
        }
        {
            int index = -1;
            float x = 0.0f;
            float y = 0.0f;
            if(sscanf(line, "POINT %d %f %f", &index, &x, &y) == 3 &&
               index == points_seen &&
               index >= 0 && index < VIEWPORT_CONFIG_MAX_POINTS &&
               index < points_expected) {
                current.points[index * 2] = x;
                current.points[index * 2 + 1] = y;
                points_seen++;
                continue;
            }
        }
        if(strncmp(line, "END", 3) == 0) {
            if(viewport_profile_valid(&current) &&
               points_expected == current.columns * current.rows &&
               points_seen == points_expected && count < max_profiles)
                profiles[count++] = current;
            viewport_profile_init(&current);
            in_profile = 0;
            points_expected = 0;
            points_seen = 0;
        }
    }

    if(in_profile && viewport_profile_valid(&current) &&
       points_expected == current.columns * current.rows &&
       points_seen == points_expected && count < max_profiles)
        profiles[count++] = current;

    fclose(fd);
    return count;
}

static int viewport_config_lock(const char *path)
{
    char lock_path[1200];
    struct flock lock;
    int fd;
    int n;

    if(!path)
        return -1;

    n = snprintf(lock_path, sizeof(lock_path), "%s.lock", path);
    if(n < 0 || n >= (int)sizeof(lock_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    fd = open(lock_path, O_CREAT | O_RDWR, 0600);
    if(fd < 0)
        return -1;

    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    while(fcntl(fd, F_SETLKW, &lock) != 0) {
        if(errno == EINTR)
            continue;
        close(fd);
        return -1;
    }

    return fd;
}

static void viewport_config_unlock(int fd)
{
    struct flock lock;

    if(fd < 0)
        return;

    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    fcntl(fd, F_SETLK, &lock);
    close(fd);
}

static int viewport_profiles_write(const char *path,
                                   const viewport_port_profile_t *profiles,
                                   int count)
{
    char tmp_path[1200];
    FILE *fd;
    int raw_fd;
    int n;

    if(!path || !profiles || count < 0)
        return 0;

    if(count == 0) {
        if(unlink(path) != 0 && errno != ENOENT) {
            veejay_msg(VEEJAY_MSG_ERROR,
                       "Unable to remove viewport configuration %s: %s",
                       path, strerror(errno));
            return 0;
        }
        return 1;
    }

    n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp-%ld", path, (long)getpid());
    if(n < 0 || n >= (int)sizeof(tmp_path))
        return 0;

    raw_fd = open(tmp_path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if(raw_fd < 0) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "Unable to open viewport configuration %s: %s",
                   tmp_path, strerror(errno));
        return 0;
    }
    fd = fdopen(raw_fd, "wb");
    if(!fd) {
        close(raw_fd);
        unlink(tmp_path);
        return 0;
    }

    fprintf(fd, "VJVIEWPORT %d\n", VIEWPORT_CONFIG_VERSION);
    for(int i = 0; i < count; i++) {
        const viewport_port_profile_t *profile = &profiles[i];
        if(!viewport_profile_valid(profile))
            continue;

        fprintf(fd, "PORT %d\n", profile->port);
        fprintf(fd, "ACTIVE 1\n");
        fprintf(fd, "FRONTBACK %d\n", profile->frontback ? 1 : 0);
        fprintf(fd, "REVERSE %d\n", profile->reverse ? 1 : 0);
        fprintf(fd, "GRID %d %d\n", profile->columns, profile->rows);
        fprintf(fd, "SOURCE %.9g %.9g %.9g %.9g\n",
                profile->source_x, profile->source_y,
                profile->source_width, profile->source_height);
        fprintf(fd, "MARKER %d %d %d\n",
                profile->marker_size, profile->grid_mode, profile->grid_color);
        fprintf(fd, "POINTS %d\n", profile->columns * profile->rows);
        for(int point = 0; point < profile->columns * profile->rows; point++)
            fprintf(fd, "POINT %d %.9g %.9g\n", point,
                    profile->points[point * 2],
                    profile->points[point * 2 + 1]);
        fprintf(fd, "END\n");
    }

    int write_ok = 1;
    if(fflush(fd) != 0)
        write_ok = 0;
    if(write_ok && fsync(raw_fd) != 0)
        write_ok = 0;
    if(fclose(fd) != 0)
        write_ok = 0;
    if(!write_ok) {
        unlink(tmp_path);
        veejay_msg(VEEJAY_MSG_ERROR,
                   "Unable to flush viewport configuration %s", tmp_path);
        return 0;
    }

    if(rename(tmp_path, path) != 0) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "Unable to replace viewport configuration %s: %s",
                   path, strerror(errno));
        unlink(tmp_path);
        return 0;
    }

    return 1;
}

static int viewport_apply_profile(viewport_t *v,
                                  const viewport_port_profile_t *profile)
{
    vj_output_mesh *mesh;
    int point_count;
    int x0;
    int y0;
    int w0;
    int h0;

    if(!v || !viewport_profile_valid(profile))
        return 0;

    x0 = (int)lrintf(profile->source_x * (float)v->w / 100.0f);
    y0 = (int)lrintf(profile->source_y * (float)v->h / 100.0f);
    w0 = (int)lrintf(profile->source_width * (float)v->w / 100.0f);
    h0 = (int)lrintf(profile->source_height * (float)v->h / 100.0f);
    if(w0 <= 0 || h0 <= 0)
        return 0;

    mesh = vj_output_mesh_create(v->w, v->h, v->w, v->h,
                                 profile->columns, profile->rows);
    if(!mesh)
        return 0;
    vj_output_mesh_set_thread_count(mesh, vje_max_threads(v->w * v->h));
    if(!vj_output_mesh_set_source_rect(mesh,
                                       (float)x0, (float)y0,
                                       (float)w0, (float)h0)) {
        vj_output_mesh_destroy(mesh);
        return 0;
    }

    point_count = profile->columns * profile->rows;
    for(int point = 0; point < point_count; point++) {
        if(!vj_output_mesh_set_point(mesh, point,
                                     profile->points[point * 2] * (float)v->w / 100.0f,
                                     profile->points[point * 2 + 1] * (float)v->h / 100.0f)) {
            vj_output_mesh_destroy(mesh);
            return 0;
        }
    }
    if(!vj_output_mesh_compile(mesh)) {
        vj_output_mesh_destroy(mesh);
        return 0;
    }

    vj_output_mesh_destroy(v->mesh);
    v->mesh = mesh;
    v->x0 = x0;
    v->y0 = y0;
    v->w0 = w0;
    v->h0 = h0;
    v->marker_size = profile->marker_size > 0 ? profile->marker_size : 4;
    v->grid_mode = profile->grid_mode;
    v->grid_val = (uint8_t)profile->grid_color;
    v->user_reverse = profile->reverse ? 1 : 0;
    v->initial_active = 1;
    v->disable = 0;
    v->mesh_selected_point = 0;
    v->mesh_hover_point = -1;
    viewport_mesh_sync_legacy_corners(v);
    viewport_mesh_refresh_legacy_transform(v);
    vj_output_mesh_get_bounds(v->mesh,
                              &v->ttx1, &v->tty1,
                              &v->ttx2, &v->tty2);
    if(v->grid)
        viewport_compute_grid(v);
    if(v->map)
        viewport_process(v);
    return 1;
}

static int viewport_reset_identity(viewport_t *v)
{
    if(!v || !v->mesh)
        return 0;

    if(!vj_output_mesh_set_grid(v->mesh, 2, 2) ||
       !vj_output_mesh_set_source_rect(v->mesh, 0.0f, 0.0f,
                                       (float)v->w, (float)v->h) ||
       !vj_output_mesh_set_point(v->mesh, 0, 0.0f, 0.0f) ||
       !vj_output_mesh_set_point(v->mesh, 1, (float)(v->w - 1), 0.0f) ||
       !vj_output_mesh_set_point(v->mesh, 2, 0.0f, (float)(v->h - 1)) ||
       !vj_output_mesh_set_point(v->mesh, 3,
                                 (float)(v->w - 1), (float)(v->h - 1)) ||
       !vj_output_mesh_compile(v->mesh))
        return 0;

    v->x0 = 0;
    v->y0 = 0;
    v->w0 = v->w;
    v->h0 = v->h;
    v->user_reverse = 1;
    v->initial_active = 0;
    v->user_ui = 0;
    v->disable = 0;
    v->mesh_selected_point = 0;
    v->mesh_hover_point = -1;
    viewport_mesh_sync_legacy_corners(v);
    viewport_mesh_refresh_legacy_transform(v);
    vj_output_mesh_get_bounds(v->mesh,
                              &v->ttx1, &v->tty1,
                              &v->ttx2, &v->tty2);
    if(v->grid)
        viewport_compute_grid(v);
    if(v->map)
        viewport_process(v);
    return 1;
}

static int		viewport_configure( 
					viewport_t *v,
					float x1, float y1,
					float x2, float y2,
					float x3, float y3,
					float x4, float y4,
					int32_t x0,  int32_t y0,
					int32_t w0,  int32_t h0,
					int32_t w,  int32_t h,
					uint32_t reverse,
					uint8_t color,
					int size);

static matrix_t		*viewport_transform(float x1,float y1,float x2,float y2,float *coord );
static inline void	point_map( matrix_t *M, float x, float y, float *nx, float *ny);
static matrix_t *	viewport_invert_matrix( matrix_t *M);
static matrix_t 	*viewport_multiply_matrix( matrix_t *A, matrix_t *B );
static	void		viewport_copy_from( matrix_t *A, matrix_t *B );
static void		viewport_scale_matrix( matrix_t *M, float sx, float sy );
static void		viewport_translate_matrix( matrix_t *M, float x, float y );
static matrix_t		*viewport_identity_matrix(void);
static matrix_t		*viewport_matrix(void);
static void		viewport_find_transform( float *coord, matrix_t *M );
void 		viewport_line (uint8_t *plane,int x1, int y1, int x2, int y2, int w, int h, uint8_t col);
static void		draw_point( uint8_t *plane, int x, int y, int w, int h, int size, int col );
static	void		viewport_prepare_process( viewport_t *v );

#ifdef HAVE_X86CPU
static inline int int_max( int a, int b )
{
        b = a-b;
        a -= b & (b>>31);
        return a;
}
static  inline int int_min( int a, int b )
{
        b = b- a;
        a += b & (b>>31);       // if(a < b) then a = b
        return a;
}
#else
static	inline int int_max(int a, int b )
{
	return MAX(a,b);
}
static	inline int int_min(int a, int b )
{
	return MIN(a,b);
}
#endif
/*
static void		viewport_print_matrix( matrix_t *M )
{
	veejay_msg(0, "|%f\t%f\t%f", M->m[0][0], M->m[0][1], M->m[0][2] );
	veejay_msg(0, "|%f\t%f\t%f", M->m[1][0], M->m[1][1], M->m[1][2] );
	veejay_msg(0, "|%f\t%f\t%f", M->m[2][0], M->m[2][1], M->m[2][2] );
}
*/

/*
 * Bresenham line implementation from Xine
 */

void viewport_line (uint8_t *plane,
		      int x1, int y1, int x2, int y2, int w, int h, uint8_t col) {

  uint8_t *c;
  int dx, dy, t, inc, d, inc1, inc2;
  int swap_x = 0;
  int swap_y = 0;

  if(!plane || w <= 0 || h <= 0)
    return;

  if( x1 < 0 ) x1 = 0; else if (x1 >= w ) x1 = w - 1;
  if( y1 < 0 ) y1 = 0; else if (y1 >= h ) y1 = h - 1;
  if( x2 < 0 ) x2 = 0; else if (x2 >= w ) x2 = w - 1;
  if( y2 < 0 ) y2 = 0; else if (y2 >= h ) y2 = h - 1;

  if(y1 == y2) {
    for(int x = x1 < x2 ? x1 : x2; x <= (x1 > x2 ? x1 : x2); x++)
      plane[y1 * w + x] = col;
    return;
  }

  if(x1 == x2) {
    for(int y = y1 < y2 ? y1 : y2; y <= (y1 > y2 ? y1 : y2); y++)
      plane[y * w + x1] = col;
    return;
  }

  /* sort line */
  if (x2 < x1) {
    t  = x1;
    x1 = x2;
    x2 = t;
    swap_x = 1;
  }
  if (y2 < y1) {
    t  = y1;
    y1 = y2;
    y2 = t;
    swap_y = 1;
  }

  dx = x2 - x1;
  dy = y2 - y1;

  /* unsort line */
  if (swap_x) {
    t  = x1;
    x1 = x2;
    x2 = t;
  }
  if (swap_y) {
    t  = y1;
    y1 = y2;
    y2 = t;
  }

  if( dx>=dy ) {
    if( x1>x2 )
    {
      t = x2; x2 = x1; x1 = t;
      t = y2; y2 = y1; y1 = t;
    }

    if( y2 > y1 ) inc = 1; else inc = -1;

    inc1 = 2*dy;
    d = inc1 - dx;
    inc2 = 2*(dy-dx);

    c = plane + y1 * w + x1;

    while(x1<x2)
    {
      *c++ = col;

      x1++;
      if( d<0 ) {
        d+=inc1;
      } else {
        y1+=inc;
        d+=inc2;
        c = plane + y1 * w + x1;
      }
    }
  } else {
    if( y1>y2 ) {
      t = x2; x2 = x1; x1 = t;
      t = y2; y2 = y1; y1 = t;
    }

    if( x2 > x1 ) inc = 1; else inc = -1;

    inc1 = 2*dx;
    d = inc1-dy;
    inc2 = 2*(dx-dy);

    c = plane + y1 * w + x1;

    while(y1<y2) {
      *c = col;

      c += w;
      y1++;
      if( d<0 ) {
	d+=inc1;
      } else {
	x1+=inc;
	d+=inc2;
	c = plane + y1 * w + x1;
      }
    }
  }
}


static	void	draw_point( uint8_t *plane, int x, int y, int w, int h, int size, int col )
{
	int x1 = x - size *2;
	int y1 = y - size*2;
	int x2 = x + size*2;
	int y2 = y + size*2;

	if( x1 < 0 ) x1 = 0; else if ( x1 >= w ) x1 = w - 1;
	if( y1 < 0 ) y1 = 0; else if ( y1 >= h ) y1 = h - 1;
	if( x2 < 0 ) x2 = 0; else if ( x2 >= w ) x2 = w - 1;
	if( y2 < 0 ) y2 = 0; else if ( y2 >= h ) y2 = h - 1;

	int i,j;
	for( i = y1; i < y2; i ++ ) 
	{
		for( j = x1; j < x2 ; j ++ )
			plane[ i * w + j ] = col;
	}
}

static void		viewport_find_transform( float *coord, matrix_t *M )
{
	double dx1,dx2,dx3,dy1,dy2,dy3;
	double det1,det2;

	dx1 = coord[X1] - coord[X3];
	dx2 = coord[X2] - coord[X3];
	dx3 = coord[X0] - coord[X1] + coord[X3] - coord[X2];
	
	dy1 = coord[Y1] - coord[Y3];
	dy2 = coord[Y2] - coord[Y3];
	dy3 = coord[Y0] - coord[Y1] + coord[Y3] - coord[Y2];

	/* is the mapping affine? */
	if( ((dx3 == 0.0) && (dy3==0.0)) )
	{
		M->m[0][0] = coord[X1] - coord[X0];
		M->m[0][1] = coord[X3] - coord[X1];
		M->m[0][2] = coord[X0];

		M->m[1][0] = coord[Y1] - coord[Y0];
		M->m[1][1] = coord[Y3] - coord[Y1];
		M->m[1][2] = coord[Y0];

		M->m[2][0] = 0.0;
		M->m[2][1] = 0.0;
	}
	else
	{
		det1 = dx3 * dy2 - dy3 * dx2;
		det2 = dx1 * dy2 - dy1 * dx2;

		// prevent division by zero if points are collinear
		if (fabs(det2) < 1e-10) {
            M->m[2][0] = 0.0;
            M->m[2][1] = 0.0;
        } else {
            M->m[2][0] = det1 / det2;
            det1 = dx1 * dy3 - dy1 * dx3;
            M->m[2][1] = det1 / det2;
        }

		M->m[2][0] = det1/det2;
	
		det1 = dx1 * dy3 - dy1 * dx3;
		det2 = dx1 * dy2 - dy1 * dx2;
		M->m[2][1] = det1/det2;

		M->m[0][0] = coord[X1] - coord[X0] + M->m[2][0] * coord[X1];
		M->m[0][1] = coord[X2] - coord[X0] + M->m[2][1] * coord[X2];
		M->m[0][2] = coord[X0];

		M->m[1][0] = coord[Y1] - coord[Y0] + M->m[2][0] * coord[Y1];
		M->m[1][1] = coord[Y2] - coord[Y0] + M->m[2][1] * coord[Y2];
		M->m[1][2] = coord[Y0];
	}

	M->m[2][2] = 1.0;
}

void	viewport_set_ui(void *vv, int i)
{
	viewport_t *v = (viewport_t*) vv;
	v->user_ui = i;
    if(!i)
        v->mesh_hover_point = -1;
}


char *viewport_get_my_help(void *vv)
{
    viewport_t *v = (viewport_t*)vv;
    if(!v->user_ui)
        return NULL;

    char reverse_mode[32];
    snprintf(reverse_mode, sizeof(reverse_mode), "%s",
             v->user_reverse ? "Forward" : "Reverse");

    char guide_mode[96];
    switch(v->grid_mode) {
        case 0:
            snprintf(guide_mode, sizeof(guide_mode),
                     "Guide: marker %dx%d; wheel changes marker size",
                     v->marker_size, v->marker_size);
            break;
        case 1:
            snprintf(guide_mode, sizeof(guide_mode),
                     "Guide: transformed dots every %d pixels",
                     v->grid_resolution);
            break;
        default:
            snprintf(guide_mode, sizeof(guide_mode),
                     "Guide: transformed grid every %d pixels",
                     v->grid_resolution);
            break;
    }

    char tmp[2048];
    snprintf(tmp, sizeof(tmp),
             "Viewport Mesh Setup\n"
             "Left click: move nearest mesh point\n"
             "CTRL + Left click: select point without moving\n"
             "ALT + Mousewheel: select previous/next point\n"
             "CTRL + Cursor Keys: nudge selected point\n"
             "Left Shift + Left click: edit source rectangle corner\n"
             "Right click: mapping direction (%s)\n"
             "Middle click or CTRL + S: save and exit setup\n"
             "Left Shift + Middle click: invert overlay color\n"
             "CTRL + Mousewheel: cycle marker/dots/grid overlay\n"
             "CTRL + H: hide/show this help\n"
             "CTRL + A: toggle transform startup state\n"
             "CTRL + P: toggle live projection\n"
             "%s\n"
             "GTK/VIMS: 007 query state, 162 set/select/grid, 160 nudge, 006 setup\n",
             reverse_mode, guide_mode);

    return strdup(tmp);
}

char *viewport_get_my_status(void *vv)
{
    viewport_t *v = (viewport_t*)vv;
    if(!v->user_ui)
        return NULL;

    int mesh_columns = 0;
    int mesh_rows = 0;
    vj_output_mesh_get_grid(v->mesh, &mesh_columns, &mesh_rows);

    const int point_count = mesh_columns * mesh_rows;
    int selected = v->mesh_selected_point;
    if(selected < 0 || selected >= point_count)
        selected = 0;

    vj_output_mesh_point selected_point = { 0.0f, 0.0f };
    vj_output_mesh_get_point(v->mesh, selected, &selected_point);

    const int selected_number = viewport_mesh_event_number(v, selected);
    const int selected_row = selected / mesh_columns;
    const int selected_col = selected % mesh_columns;
    const int hover_number = viewport_mesh_event_number(v, v->mesh_hover_point);
    const float percent_x = selected_point.x * 100.0f / (float)v->w;
    const float percent_y = selected_point.y * 100.0f / (float)v->h;

    int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
    vj_output_mesh_get_bounds(v->mesh, &bx0, &by0, &bx1, &by1);

    char status[1536];
    snprintf(status, sizeof(status),
             "Mesh %dx%d (%d points) | selected %d [row %d, column %d] | hover %d\n"
             "Selected output: %.2f, %.2f px | normalized: %.4f%%, %.4f%%\n"
             "Source rectangle: %d,%d + %dx%d | mesh bounds: %d,%d - %d,%d\n"
             "Legacy corners: 1=%.2fx%.2f 2=%.2fx%.2f 3=%.2fx%.2f 4=%.2fx%.2f\n",
             mesh_columns, mesh_rows, point_count,
             selected_number, selected_row + 1, selected_col + 1, hover_number,
             selected_point.x, selected_point.y, percent_x, percent_y,
             v->x0, v->y0, v->w0, v->h0, bx0, by0, bx1, by1,
             v->x1, v->y1, v->x2, v->y2,
             v->x3, v->y3, v->x4, v->y4);

    return strdup(status);
}


static matrix_t	*viewport_matrix(void)
{
	matrix_t *M = (matrix_t*) vj_malloc(sizeof(matrix_t));
	uint32_t i,j;
	for( i = 0;i < 3 ; i ++ )
	{
	  for( j = 0; j < 3 ; j++ )
		M->m[i][j] = 0.0;
	}
	return M;
}

static matrix_t	*viewport_identity_matrix(void)
{
	matrix_t *M = viewport_matrix();
	M->m[0][0] = 1.0;
	M->m[1][1] = 1.0;
	M->m[2][2] = 1.0;
	return M;
}

static void		viewport_translate_matrix( matrix_t *M, float x, float y )
{
	float g = M->m[2][0];
	float h = M->m[2][1];
	float i = M->m[2][2];

	M->m[0][0] += x * g;
	M->m[0][1] += x * h;
	M->m[0][2] += x * i;

	M->m[1][0] += y * g;
	M->m[1][1] += y * h;
	M->m[1][2] += y * i;
}

static void		viewport_scale_matrix( matrix_t *M, float sx, float sy )
{
	M->m[0][0] *= sx;
	M->m[0][1] *= sx;
	M->m[0][2] *= sx;

	M->m[1][0] *= sy;
	M->m[1][1] *= sy;
	M->m[1][2] *= sy;
}

static	void		viewport_copy_from( matrix_t *A, matrix_t *B )
{
	uint32_t i,j;
	for( i =0 ; i < 3; i ++ )
		for( j = 0; j < 3 ; j ++ )
			A->m[i][j] = B->m[i][j];
}

static matrix_t 	*viewport_multiply_matrix( matrix_t *A, matrix_t *B )
{
	matrix_t *R = viewport_matrix();

	R->m[0][0] = A->m[0][0] * B->m[0][0] + A->m[0][1] * B->m[1][0] + A->m[0][2] * B->m[2][0];
	R->m[0][1] = A->m[0][0] * B->m[0][1] + A->m[0][1] * B->m[1][1] + A->m[0][2] * B->m[2][1];
	R->m[0][2] = A->m[0][0] * B->m[0][2] + A->m[0][1] * B->m[1][2] + A->m[0][2] * B->m[2][2];

	R->m[1][0] = A->m[1][0] * B->m[0][0] + A->m[1][1] * B->m[1][0] + A->m[1][2] * B->m[2][0];
	R->m[1][1] = A->m[1][0] * B->m[0][1] + A->m[1][1] * B->m[1][1] + A->m[1][2] * B->m[2][1];
	R->m[1][2] = A->m[1][0] * B->m[0][2] + A->m[1][1] * B->m[1][2] + A->m[1][2] * B->m[2][2];

	R->m[2][0] = A->m[2][0] * B->m[0][0] + A->m[2][1] * B->m[0][1] + A->m[2][2] * B->m[2][0];
	R->m[2][1] = A->m[2][0] * B->m[0][1] + A->m[2][1] * B->m[1][1] + A->m[2][2] * B->m[2][1];
	R->m[2][2] = A->m[2][0] * B->m[0][2] + A->m[2][1] * B->m[1][2] + A->m[2][2] * B->m[2][2];	


	return R;
}


static matrix_t *viewport_invert_matrix(matrix_t *M)
{
    double det = M->m[0][0] * (M->m[1][1] * M->m[2][2] - M->m[1][2] * M->m[2][1]) -
                 M->m[0][1] * (M->m[1][0] * M->m[2][2] - M->m[1][2] * M->m[2][0]) +
                 M->m[0][2] * (M->m[1][0] * M->m[2][1] - M->m[1][1] * M->m[2][0]);

    if (det == 0.0)
    {
        veejay_msg(0, "Determinant is 0.0, inverse of matrix not possible");
        return NULL;
    }

    matrix_t *R = (matrix_t*) vj_malloc(sizeof(matrix_t));
    if (!R) return NULL;

    double inv_det = 1.0 / det;

    R->m[0][0] =  (M->m[1][1] * M->m[2][2] - M->m[1][2] * M->m[2][1]) * inv_det;
    R->m[0][1] = -(M->m[0][1] * M->m[2][2] - M->m[0][2] * M->m[2][1]) * inv_det;
    R->m[0][2] =  (M->m[0][1] * M->m[1][2] - M->m[0][2] * M->m[1][1]) * inv_det;

    R->m[1][0] = -(M->m[1][0] * M->m[2][2] - M->m[1][2] * M->m[2][0]) * inv_det;
    R->m[1][1] =  (M->m[0][0] * M->m[2][2] - M->m[0][2] * M->m[2][0]) * inv_det;
    R->m[1][2] = -(M->m[0][0] * M->m[1][2] - M->m[0][2] * M->m[1][0]) * inv_det;

    R->m[2][0] =  (M->m[1][0] * M->m[2][1] - M->m[1][1] * M->m[2][0]) * inv_det;
    R->m[2][1] = -(M->m[0][0] * M->m[2][1] - M->m[0][1] * M->m[2][0]) * inv_det;
    R->m[2][2] =  (M->m[0][0] * M->m[1][1] - M->m[0][1] * M->m[1][0]) * inv_det;

    return R;
}

static	inline void		point_map( matrix_t *M, float x, float y, float *nx, float *ny)
{
	float w = M->m[2][0] * x + M->m[2][1] * y + M->m[2][2];

	if( w == 0.0 )
		w = 1.0;
	else
		w = 1.0 / w;

	*nx = (M->m[0][0] * x + M->m[0][1] * y + M->m[0][2] ) * w;
	*ny = (M->m[1][0] * x + M->m[1][1] * y + M->m[1][2] ) * w;

}

static	inline void		point_map_int( matrix_t *M, float x, float y, int *nx, int *ny)
{
	float w = M->m[2][0] * x + M->m[2][1] * y + M->m[2][2];

	if( w == 0.0 )
		w = 1.0;
	else
		w = 1.0 / w;

	*nx = round1( (M->m[0][0] * x + M->m[0][1] * y + M->m[0][2] ) * w);
	*ny = round1( (M->m[1][0] * x + M->m[1][1] * y + M->m[1][2] ) * w);

}




static matrix_t	*viewport_transform(
	float x1,
	float y1,
	float x2,
	float y2,
	float *coord )
{
	float sx=1.0,sy=1.0;

	if( (x2-x1) > 0.0 )
		sx = 1.0 / (x2-x1);
	if( (y2-y1) > 0.0 )
		sy = 1.0 / (y2-y1);

	matrix_t *H = viewport_matrix();
	viewport_find_transform( coord, H );

	matrix_t *I = viewport_identity_matrix();
	viewport_translate_matrix( I, -x1, -y1 );
	viewport_scale_matrix( I, sx,sy );
	matrix_t *R = viewport_multiply_matrix( H,I );
	free(I);
	free(H);
	return R;
}
 
static	float		msy(viewport_t *v, float y)
{
	if( v->ui->scale == 1.0f ) { 
		return y;
	}
	ui_t *u = v->ui;
	int	    cy = v->h / 2;
	int	    dy = cy - ( u->sh / 2 );

	float		a = (float) dy / ( v->h / 100.0f );
	float		s  = (float) v->h / (float) v->ui->sh;

	return (y/s) + a;
}

static	float		msx(viewport_t *v, float x)
{
	if( v->ui->scale == 1.0f ) { 
		return x;
	}
	ui_t *u = v->ui;
	int	    cx = v->w / 2;
	int	    dx = cx - ( u->sw / 2 );

	float		a = (float) dx / ( v->w / 100.0f );
	float		s  = (float) v->w / (float) v->ui->sw;

	return (x/s) + a;
}

static	float		vsx(viewport_t *v, float x)
{
	if( v->ui->scale == 1.0f ) { 
		return x;
	}
	ui_t *u = v->ui;
	int	    cx = v->w / 2;
	int	    dx = cx - ( u->sw / 2 );

	float		a = (float) dx / ( v->w / 100.0f );
	float		s  = (float) v->w / (float) v->ui->sw;
	return (x-a)*s;
}
static	float		vsy(viewport_t *v, float x)
{
	if( v->ui->scale == 1.0f ) 
		return x;

	ui_t *u = v->ui;
	int	    cy = v->h / 2;
	int	    dy = cy - ( u->sh / 2 );

	float		a = (float) dy / ( v->h / 100.0f );

	float		s  = (float) v->h / (float) v->ui->sh;
	return (x-a)*s;
}



static int		viewport_configure( 
					viewport_t *v,
					float x1, float y1, /* output */
					float x2, float y2,
					float x3, float y3,
					float x4, float y4,
					int32_t x0,  int32_t y0, /* input */
					int32_t w0,  int32_t h0,
					int32_t wid,  int32_t hei,
					uint32_t reverse,
					uint8_t  color,
					int grid_resolution)
{
	int w = wid, h = hei;
	if( grid_resolution <= 8 )
		grid_resolution = GRID_START;
	float rat = (h/(float)w);

	v->grid_width = grid_resolution;
	v->grid_height = grid_resolution * rat;
	
	v->points[X0] = (float) x1 * (float) w / 100.0;
	v->points[Y0] = (float) y1 * (float) h / 100.0;

	v->points[X1] = (float) x2 * (float) w / 100.0;
	v->points[Y1] = (float) y2 * (float) h / 100.0;

	v->points[X2] = (float) x3 * (float) w / 100.0;
	v->points[Y2] = (float) y3 * (float) h / 100.0;

	v->points[X3] = (float) x4 * (float) w / 100.0;
	v->points[Y3] = (float) y4 * (float) h / 100.0;

	
	v->w = wid; /* image plane boundaries */
	v->x = 0;
	v->h = hei;
	v->y = 0;

	v->x0 = x0; 
	v->y0 = y0;
	v->w0 = w0;
	v->h0 = h0;

	v->grid_val  = color;

	v->x1 = x1;
	v->x2 = x2;
	v->x3 = x3;
	v->x4 = x4;
	v->y1 = y1;
	v->y2 = y2;
	v->y3 = y3;
	v->y4 = y4;
	v->user_reverse = reverse;

    if(!viewport_mesh_sync_quad(v)) {
        return 0;
    }

	float tmp = v->points[X3];
	v->points[X3] = v->points[X2];
	v->points[X2] = tmp;
	tmp = v->points[Y3];
	v->points[Y3] = v->points[Y2];
	v->points[Y2] = tmp;

	matrix_t *m = viewport_transform( x0, y0, x0 + w0, y0 + h0, v->points );

	if(v->m) {
	 free(v->m);
	 v->m = NULL;
	}
	if(v->M) {
	 free(v->M);
	 v->M = NULL;
	}
	
	if ( reverse )
	{
		v->m = viewport_matrix();
		viewport_copy_from( v->m, m );
		v->M = viewport_invert_matrix( v->m );
		if(!v->M)
		{
			free(m);
			free(v->m);
			v->m = NULL;
			return 0;
		}
		free(m);
		viewport_prepare_process( v );
		return 1;

	}
	else
	{
		matrix_t *im = viewport_invert_matrix( m );
		if(!im)
		{
			free(m);
			return 0;
		}
		v->M = m;
		v->m = im;
		viewport_prepare_process( v );
		return 1;
	}

	return 0;
}

static void viewport_process(viewport_t *p)
{
    const int32_t w = p->w;
    const int32_t h = p->h;
    const int32_t X = p->x0;
    const int32_t Y = p->y0;

    matrix_t *m = p->m;

    const int32_t len = w * h;
    const int32_t sentinel_idx = len + 1;
	
    const float xinc = m->m[0][0], yinc = m->m[1][0], winc = m->m[2][0];
    const float m01  = m->m[0][1], m11  = m->m[1][1], m21  = m->m[2][1];
    const float m02  = m->m[0][2], m12  = m->m[1][2], m22  = m->m[2][2];

    const int32_t tx1 = p->ttx1, tx2 = p->ttx2;
    const int32_t ty1 = p->tty1, ty2 = p->tty2;

    int32_t *restrict map = p->map;

    for (int32_t y = ty1; y < ty2; y++)
    {
        float tx = xinc * (tx1 + 0.5f) + m01 * (y + 0.5f) + m02;
        float ty = yinc * (tx1 + 0.5f) + m11 * (y + 0.5f) + m12;
        float tw = winc * (tx1 + 0.5f) + m21 * (y + 0.5f) + m22;

        int32_t *restrict row_map = &map[y * w + tx1];

        #pragma GCC ivdep
        for (int32_t x = tx1; x < tx2; x++)
        {
            float inv_w = (tw != 0.0f) ? (1.0f / tw) : 0.0f;
            int32_t itx = (int32_t)(tx * inv_w);
            int32_t ity = (int32_t)(ty * inv_w);

            int isValid = (itx >= X && itx < w && ity >= Y && ity < h);
            
            *row_map++ = isValid ? (ity * w + itx) : sentinel_idx;

            tx += xinc;
            ty += yinc;
            tw += winc;
        }
    }
}

static	void	viewport_prepare_process( viewport_t *v )
{
	const int32_t X = v->x0;
	const int32_t Y = v->y0;

	float dx1,dx2,dx3,dx4,dy1,dy2,dy3,dy4;
	matrix_t *M = v->M;

	point_map( M, v->x, v->y, &dx1, &dy1);
	point_map( M, v->x + v->w, v->y, &dx2, &dy2 );
	point_map( M, v->x, v->y + v->h, &dx4, &dy4 );
	point_map( M, v->x + v->w, v->y + v->h, &dx3, &dy3 );
	
	v->tx1 = round1( min4( dx1, dx2, dx3, dx4 ) );
	v->ty1 = round1( min4( dy1, dy2, dy3, dy4 ) );
	v->tx2 = round1( max4( dx1, dx2, dx3, dx4 ) );	
	v->ty2 = round1( max4( dy1, dy2, dy3, dy4 ) );
	
	clamp1( v->ty1 , Y, Y + v->h0 );
	clamp1( v->ty2 ,Y,Y + v->h0 );
	clamp1( v->tx1, X, X + v->w0 );
	clamp1( v->tx2, X, X + v->w0 );

	v->ttx2 = v->tx2;
	v->tty2 = v->ty2;
	v->ttx1 = v->tx1;
	v->tty1 = v->ty1;

	clamp1( v->ttx2,0, v->w );	
	clamp1( v->tty2,0, v->h );
	clamp1( v->ttx1,0, v->w );	
	clamp1( v->tty1,0, v->h );
}

void viewport_process_dynamic(void *data, uint8_t *restrict in[3], uint8_t *restrict out[3])
{
    viewport_t *v = (viewport_t*)data;
    if(v->disable)
        return;
    const uint8_t *input[3] = { in[0], in[1], in[2] };
    uint8_t *output[3] = { out[0], out[1], out[2] };
    vj_output_mesh_render_yuv444(v->mesh, input, output);
}

void viewport_process_dynamic_alpha(void *data, uint8_t *restrict in[4], uint8_t *restrict out[4])
{
    viewport_t *v = (viewport_t*)data;
    if(v->disable)
        return;
    const uint8_t *input[4] = { in[0], in[1], in[2], in[3] };
    uint8_t *output[4] = { out[0], out[1], out[2], out[3] };
    vj_output_mesh_render_yuv444_alpha(v->mesh, input, output);
}

void			viewport_destroy( void *data )
{
	viewport_t *v = (viewport_t*)data;
	if( v )
	{
		if( v->M ) free( v->M );
		if( v->m ) free( v->m );
		if( v->T ) free( v->T );
		if( v->map ) free( v->map );
        if( v->mesh ) vj_output_mesh_destroy(v->mesh);
		if( v->help ) free( v->help );
		if( v->ui ) {
			if( v->ui->scaler )
				yuv_free_swscaler(v->ui->scaler);
            if(v->ui->src_frame)
                free(v->ui->src_frame);
            if(v->ui->dst_frame)
                free(v->ui->dst_frame);
			if( v->ui->buf[0] )
				free(v->ui->buf[0]);
			free(v->ui);
		}	
		if(v->grid) free(v->grid);
		free(v);
	}
	v = NULL;
}

static	int		viewport_update_perspective( viewport_t *v, float *values )
{
	v->disable = 0;

	int res = viewport_configure (v, v->x1, v->y1,
					 v->x2, v->y2,
					 v->x3, v->y3,
					 v->x4, v->y4,	
					 v->x0, v->y0,	
					 v->w0, v->h0,
					 v->w,  v->h,
					 v->user_reverse,
					 v->grid_val,
					 v->grid_resolution );


	if(! res )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Viewport: Invalid quadrilateral. Trying to fallback");

		v->x1 = values[0]; v->x2 = values[2]; v->x3 = values[4]; v->x4 = values[6];
		v->y1 = values[1]; v->y2 = values[3]; v->y3 = values[5]; v->y4 = values[7];

		if(!viewport_configure( v, v->x1, v->y1, v->x2, v->y2, v->x3, v->y3,v->x4,v->y4,
				v->x0, v->y0, v->w0, v->h0,v->w,v->h, v->user_reverse, v->grid_val,v->grid_resolution ))
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to configure the viewport");
			veejay_msg(VEEJAY_MSG_ERROR, "If you are using a preset-configuration, remove or fix ~/.veejay/viewport.cfg");
			v->disable = 1;
			return 0;
		}
        res = 1;
	}

	// Clear map
	const int len = v->w * v->h;
	int k;
	for( k = 0 ; k < len ; k ++ )
		v->map[k] = len+1;
	

	// Update map
	viewport_process( v );

	return res;
}
static int      nearest_div(int val )
{
        int r = val % 8;
        while(r--)
                val--;
        return val;
}

static	void	*viewport_init_swscaler(ui_t *u, int w, int h)
{
	uint8_t *dummy[3] = { NULL,NULL,NULL };
	int nw = w * u->scale;
	int nh = h * u->scale;
	u->sw  = nearest_div(nw);
	u->sh  = nearest_div(nh);
	u->src_frame = yuv_yuv_template(dummy[0], dummy[1], dummy[2],
                                      w, h, PIX_FMT_GRAY8);
	u->dst_frame = yuv_yuv_template(u->buf[0], NULL, NULL,
                                      u->sw, u->sh, PIX_FMT_GRAY8);
	if(!u->src_frame || !u->dst_frame) {
		if(u->src_frame) free(u->src_frame);
		if(u->dst_frame) free(u->dst_frame);
		u->src_frame = NULL;
		u->dst_frame = NULL;
		return NULL;
	}

	sws_template t;
	memset(&t,0,sizeof(sws_template));
	t.flags = yuv_which_scaler();
	u->sx   = (float)w / (float) u->sw;
	u->sy   = (float)h / (float) u->sh;
	void *scaler = yuv_init_swscaler(u->src_frame, u->dst_frame,
                                      &t, yuv_sws_get_cpu_flags());
	if(!scaler) {
		free(u->src_frame);
		free(u->dst_frame);
		u->src_frame = NULL;
		u->dst_frame = NULL;
	}
	return scaler;
}


void	viewport_reconfigure(void *vv)
{
	viewport_t *v = (viewport_t*) vv;
	float p[9];
	
	p[0] = v->x1;
	p[2] = v->x2;
	p[4] = v->x3;
	p[6] = v->x4;
	p[1] = v->y1;
	p[3] = v->y2;
	p[5] = v->y3;	
	p[7] = v->y4;

	viewport_update_perspective(v,p);

}

void	viewport_set_composite(void *vc, int mode, int colormode)
{
	viewport_config_t *c = (viewport_config_t*) vc;
	c->composite_mode = mode;
	c->colormode = colormode;
}
int	viewport_get_color_mode_from_config(void *vc)
{
	viewport_config_t *c = (viewport_config_t*) vc;
	return c->colormode;
}

int	viewport_get_composite_mode_from_config(void *vc)
{
	viewport_config_t *c = (viewport_config_t*) vc;
	return c->composite_mode;
}

int	viewport_get_initial_active( void *vv )
{
	viewport_t *v = (viewport_t*) vv;
	return v->initial_active;
}

void	viewport_set_initial_active( void *vv, int status )
{
	viewport_t *v = (viewport_t*) vv;
	v->initial_active = status;
}

void	*viewport_get_configuration(void *vv, char *filename )
{
    (void)filename;
	viewport_t *v = (viewport_t*) vv;
	viewport_config_t *o = (viewport_config_t*) vj_calloc(sizeof(viewport_config_t));
	o->saved_w = v->saved_w;
	o->saved_h = v->saved_h;
	o->reverse = v->user_reverse;
	o->grid_resolution = v->grid_resolution;
	o->grid_color = v->grid_val;
	o->frontback = 0;
	o->x0 	= v->x0;
	o->y0	= v->y0;
	o->w0   = v->w0;
	o->h0   = v->h0;
	o->x1   = v->x1;
	o->x2   = v->x2;
    o->x3   = v->x3;
    o->x4   = v->x4;
    o->y1   = v->y1;
	o->y2   = v->y2;
	o->y3   = v->y3;
  	o->y4   = v->y4;
	o->scale = v->ui->scale;
	o->initial_active = v->initial_active;

	return o;
}

int	viewport_reconfigure_from_config(void *vv, void *config, char *filename)
{
	viewport_t *v = (viewport_t*) vv;
	viewport_config_t *c = (viewport_config_t*) config;
	viewport_config_t *o = viewport_get_configuration(vv, filename);

	veejay_msg(0,"Configuration saved %dx%d, have %dx%d",
			c->saved_w, c->saved_h,v->saved_w,v->saved_h );

	// Clear map
	const int len = v->w * v->h;
	int k;
	for( k = 0 ; k < len ; k ++ )
		v->map[k] = len+1;

	v->disable = 0;

	// try to initialize the new settings
	int res = viewport_configure( v, 	c->x1,c->y1,
						c->x2,c->y2,
						c->x3,c->y3,
						c->x4,c->y4,		
						c->x0,c->y0,
						c->w0,c->h0,
						v->w,v->h,
						c->reverse,
						c->grid_color,
						c->grid_resolution );

	if(!res) {
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot load calibration settings, restoring defaults");
		res = viewport_configure( v,	o->x1,o->y1,
						o->x2,o->y2,
						o->x3,o->y3,
						o->x4,o->y4,		
						o->x0,o->y0,
						o->w0,o->h0,
						v->w,v->h,
						o->reverse,
						o->grid_color,
						o->grid_resolution );
	}

	if(!res) {	
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to revert to old configuration");
		v->disable = 1;
		free(o);
		return 0;
	}

	
	if( res ) {
		v->user_ui = 0;
		viewport_process( v );
		veejay_msg(VEEJAY_MSG_DEBUG, 
		"Reconfigured calibration for %dx%d to (1)=%fx%f\t(2)=%fx%f\t(3)=%fx%f\t(4)=%fx%f\t%fx%f+%fx%f",
			v->w,v->h,v->x1,v->y1,v->x2,v->y2,v->x3,v->y3,v->x4,v->y4,v->x0,v->y0,v->w0,v->h0);

	}
	free(o);
	return 1;
}
void	viewport_update_from(void *vv, void *bb)
{
	if( vv == NULL || bb == NULL )
		return;

	viewport_t *v = (viewport_t*) vv;
	viewport_t *b = (viewport_t*) bb;

	float p[9];
	
	p[0] = b->x1;
	p[2] = b->x2;
	p[4] = b->x3;
	p[6] = b->x4;
	p[1] = b->y1;
	p[3] = b->y2;
	p[5] = b->y3;	
	p[7] = b->y4;


	float sx = (float) b->w / (float) v->w;
	float sy = (float) b->h / (float) v->h;

	b->x0 = v->x0 * sx;
	b->y0 = v->y0 * sy;
	b->w0 = v->w0 * sx;
	b->h0 = v->h0 * sy;
	b->x  = v->x * sx;
	b->y  = v->y * sy;
	
	b->x1 = v->x1;
	b->y1 = v->y1;
	b->x2 = v->x2;
	b->y2 = v->y2;
	b->x3 = v->x3;
	b->y3 = v->y3;
	b->x4 = v->x4;
	b->y4 = v->y4;

	b->user_reverse = v->user_reverse;
	

	if(viewport_update_perspective(b,p) && viewport_mesh_copy_scaled(v, b, sx, sy)) {
		veejay_msg(VEEJAY_MSG_DEBUG, "Configured input %dx%d to (1)=%fx%f\t(2)=%fx%f\t(3)=%fx%f\t(4)=%fx%f\t%dx%d+%dx%d",
			b->w,b->h,b->x1,b->y1,b->x2,b->y2,b->x3,b->y3,b->x4,b->y4,b->x0,b->y0,b->w0,b->h0);
	}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG,"Failed to apply projection calibration. Press CTRL-s to configure this sample or press CTRL-p to disable"); 
		b->disable = 1;
	}

}

void *viewport_init(int x0, int y0, int w0, int h0, int w, int h,
                    int iw, int ih, char *filename, int *enable,
                    int *frontback, int mode)
{
    (void)mode;
    veejay_msg(VEEJAY_MSG_DEBUG, "\tBacking  : %dx%d", w, h);
    veejay_msg(VEEJAY_MSG_DEBUG, "\tRectangle: %dx%d+%dx%d", x0, y0, w0, h0);

    viewport_t *v = (viewport_t*)vj_calloc(sizeof(viewport_t));
    if(!v)
        return NULL;

    v->ui = (ui_t*)vj_calloc(sizeof(ui_t));
    if(!v->ui) {
        viewport_destroy(v);
        return NULL;
    }

    v->ui->buf[0] = (uint8_t*)vj_calloc(sizeof(uint8_t) * (w * h));
    v->ui->scale = 0.5f;
    if(!v->ui->buf[0] || !(v->ui->scaler = viewport_init_swscaler(v->ui, iw, ih))) {
        viewport_destroy(v);
        return NULL;
    }

    v->saved_w = w;
    v->saved_h = h;
    v->w = w;
    v->h = h;
    v->marker_size = 4;
    v->disable = 0;
    v->config_port = 0;
    v->config_frontback = 1;
    v->config_bound = 0;
    if(filename)
        snprintf(v->config_path, sizeof(v->config_path), "%s", filename);

    if(!viewport_mesh_create(v, w, h, 2, 2)) {
        viewport_destroy(v);
        return NULL;
    }

    if(!viewport_configure(v,
                           0.0f, 0.0f,
                           100.0f, 0.0f,
                           100.0f, 100.0f,
                           0.0f, 100.0f,
                           x0, y0, w0, h0,
                           w, h, 1, 0xff, w / 32)) {
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize identity viewport");
        viewport_destroy(v);
        return NULL;
    }

    v->initial_active = 0;
    v->user_ui = 0;
    if(enable)
        *enable = 0;
    if(frontback)
        *frontback = 1;

    v->map = (int32_t*)vj_calloc(sizeof(int32_t) * (v->w * v->h + (v->w * 2)));
    if(!v->map) {
        veejay_msg(VEEJAY_MSG_ERROR, "Memory allocation error");
        viewport_destroy(v);
        return NULL;
    }

    const int len = v->w * v->h;
    const int eln = len + (v->w * 2);
    veejay_memset(v->map, len + 1, eln);
    viewport_process(v);

    if(v->grid_resolution > 0)
        viewport_compute_grid(v);

    return (void*)v;
}

void *viewport_clone(void *vv, int new_w, int new_h)
{
	viewport_t *v = (viewport_t*)vv;
	if(!v)
		return NULL;

	viewport_t *q = (viewport_t*)vj_malloc(sizeof(viewport_t));
	if(!q)
		return NULL;
	veejay_memcpy(q, v, sizeof(viewport_t));

	q->M = NULL;
	q->m = NULL;
	q->T = NULL;
	q->map = NULL;
	q->grid = NULL;
	q->mesh = NULL;
	q->help = NULL;
	q->ui = NULL;

	float sx = (float)new_w / (float)v->w;
	float sy = (float)new_h / (float)v->h;
	q->initial_active = v->initial_active;
	q->x0 = v->x0 * sx;
	q->y0 = v->y0 * sy;
	q->w0 = v->w0 * sx;
	q->h0 = v->h0 * sy;
	q->x = v->x * sx;
	q->y = v->y * sy;
	q->w = new_w;
	q->h = new_h;
	q->saved_w = new_w;
	q->saved_h = new_h;
	q->usermouse[0] = 0.0;
	q->usermouse[1] = 0.0;
	q->usermouse[2] = 0.0;
	q->usermouse[3] = 0.0;
	q->disable = 0;

	q->ui = (ui_t*)vj_calloc(sizeof(ui_t));
	if(!q->ui) {
		viewport_destroy(q);
		return NULL;
	}
	q->ui->buf[0] = (uint8_t*)vj_calloc(sizeof(uint8_t) * (new_w * new_h));
	q->ui->scale = 1.0f;
	if(!q->ui->buf[0] || !(q->ui->scaler = viewport_init_swscaler(q->ui, new_w, new_h))) {
		viewport_destroy(q);
		return NULL;
	}

	int mesh_columns = 2;
	int mesh_rows = 2;
	vj_output_mesh_get_grid(v->mesh, &mesh_columns, &mesh_rows);
	if(!viewport_mesh_create(q, new_w, new_h, mesh_columns, mesh_rows)) {
		viewport_destroy(q);
		return NULL;
	}

	int res = viewport_configure(q, q->x1, q->y1,
							 q->x2, q->y2,
							 q->x3, q->y3,
							 q->x4, q->y4,
							 q->x0, q->y0,
							 q->w0, q->h0,
							 new_w, new_h,
							 q->user_reverse,
							 q->grid_val,
							 q->grid_resolution);
	q->user_ui = 0;
	if(res)
		res = viewport_mesh_copy_scaled(v, q, sx, sy);

	if(!res) {
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid point locations");
		viewport_destroy(q);
		return NULL;
	}

	q->map = (int32_t*)vj_malloc(sizeof(int32_t) * (q->w * q->h + (q->w * 2)));
	if(!q->map) {
		viewport_destroy(q);
		return NULL;
	}

	const int len = q->w * q->h;
	const int eln = len + (q->w * 2);
	for(int k = len; k < eln; k++)
		q->map[k] = len + 1;
	viewport_process(q);

	veejay_msg(VEEJAY_MSG_INFO,"\tConfiguring input:");
	veejay_msg(VEEJAY_MSG_INFO, "\tPoints   :\t(1) %fx%f (2) %fx%f", q->x1,q->y1,q->x2,q->y2);
	veejay_msg(VEEJAY_MSG_INFO, "\t         :\t(3) %fx%f (4) %fx%f", q->x3,q->y3,q->x4,q->y4);
	veejay_msg(VEEJAY_MSG_INFO, "\tQuad     :\t%dx%d+%dx%d", q->x0,q->y0,q->w0,q->h0);
	veejay_msg(VEEJAY_MSG_INFO, "\tDimension:\t%dx%d",q->w,q->h);
	return (void*)q;
}


int	 viewport_active( void *data )
{
	viewport_t *v = (viewport_t*) data;
	return v->user_ui;
}

char	*viewport_get_help(void *data)
{
	viewport_t *v = (viewport_t*)data;
	return v->help;
}



int viewport_bind_port_configuration(void *data,
                                     const char *homedir,
                                     int port,
                                     int *active)
{
    viewport_t *v = (viewport_t*)data;
    viewport_port_profile_t profiles[VIEWPORT_CONFIG_MAX_PROFILES];
    int count;

    if(active)
        *active = 0;
    if(!v || !homedir || port <= 0)
        return 0;

    int n = snprintf(v->config_path, sizeof(v->config_path),
                     "%s/viewport.cfg", homedir);
    if(n < 0 || n >= (int)sizeof(v->config_path))
        return 0;

    v->config_port = port;
    v->config_frontback = 1;
    v->config_bound = 1;

    memset(profiles, 0, sizeof(profiles));
    count = viewport_profiles_load(v->config_path,
                                   profiles,
                                   VIEWPORT_CONFIG_MAX_PROFILES);
    for(int i = 0; i < count; i++) {
        if(profiles[i].port != port)
            continue;

        if(!viewport_apply_profile(v, &profiles[i])) {
            veejay_msg(VEEJAY_MSG_ERROR,
                       "Viewport configuration for port %d is invalid", port);
            viewport_reset_identity(v);
            return 0;
        }

        v->config_frontback = profiles[i].frontback ? 1 : 0;
        v->initial_active = 1;
        if(active)
            *active = 1;
        veejay_msg(VEEJAY_MSG_INFO,
                   "Restored and activated viewport configuration for port %d from %s",
                   port, v->config_path);
        return 1;
    }

    viewport_reset_identity(v);
    veejay_msg(VEEJAY_MSG_INFO,
               "No saved viewport configuration for port %d", port);
    return 0;
}

int viewport_save_bound_configuration(void *data, int frontback)
{
    viewport_t *v = (viewport_t*)data;
    viewport_port_profile_t profiles[VIEWPORT_CONFIG_MAX_PROFILES];
    viewport_port_profile_t profile;
    int columns = 0;
    int rows = 0;
    int count;
    int replace = -1;
    int lock_fd;
    int n;

    if(!v || !v->config_bound || v->config_port <= 0 || !v->config_path[0]) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "Viewport configuration is not bound to a VeeJay port");
        return 0;
    }

    viewport_profile_init(&profile);
    profile.valid = 1;
    profile.port = v->config_port;
    profile.active = 1;
    profile.frontback = frontback ? 1 : 0;
    profile.reverse = v->user_reverse ? 1 : 0;
    profile.marker_size = v->marker_size;
    profile.grid_mode = v->grid_mode;
    profile.grid_color = v->grid_val;
    profile.source_x = (float)v->x0 * 100.0f / (float)v->w;
    profile.source_y = (float)v->y0 * 100.0f / (float)v->h;
    profile.source_width = (float)v->w0 * 100.0f / (float)v->w;
    profile.source_height = (float)v->h0 * 100.0f / (float)v->h;
    vj_output_mesh_get_grid(v->mesh, &columns, &rows);
    profile.columns = columns;
    profile.rows = rows;

    if(!viewport_profile_valid(&profile))
        return 0;

    for(int point = 0; point < columns * rows; point++) {
        vj_output_mesh_point p;
        if(!vj_output_mesh_get_point(v->mesh, point, &p))
            return 0;
        profile.points[point * 2] = p.x * 100.0f / (float)v->w;
        profile.points[point * 2 + 1] = p.y * 100.0f / (float)v->h;
    }

    lock_fd = viewport_config_lock(v->config_path);
    if(lock_fd < 0) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "Unable to lock viewport configuration %s: %s",
                   v->config_path, strerror(errno));
        return 0;
    }

    memset(profiles, 0, sizeof(profiles));
    count = viewport_profiles_load(v->config_path,
                                   profiles,
                                   VIEWPORT_CONFIG_MAX_PROFILES);
    for(int i = 0; i < count; i++) {
        if(profiles[i].port == v->config_port) {
            replace = i;
            break;
        }
    }

    if(replace >= 0)
        profiles[replace] = profile;
    else if(count < VIEWPORT_CONFIG_MAX_PROFILES)
        profiles[count++] = profile;
    else {
        viewport_config_unlock(lock_fd);
        veejay_msg(VEEJAY_MSG_ERROR,
                   "Viewport configuration contains too many port profiles");
        return 0;
    }

    if(!viewport_profiles_write(v->config_path, profiles, count)) {
        viewport_config_unlock(lock_fd);
        return 0;
    }
    viewport_config_unlock(lock_fd);

    char legacy_mesh[1200];
    n = snprintf(legacy_mesh, sizeof(legacy_mesh), "%s.mesh", v->config_path);
    if(n > 0 && n < (int)sizeof(legacy_mesh))
        unlink(legacy_mesh);

    v->config_frontback = profile.frontback;
    v->initial_active = 1;
    veejay_msg(VEEJAY_MSG_INFO,
               "Saved viewport configuration for port %d to %s; it will activate automatically on restart",
               v->config_port, v->config_path);
    return 1;
}

int viewport_reset_bound_configuration(void *data)
{
    viewport_t *v = (viewport_t*)data;
    viewport_port_profile_t profiles[VIEWPORT_CONFIG_MAX_PROFILES];
    int count;
    int lock_fd;
    int out = 0;

    if(!v || !v->config_bound || v->config_port <= 0 || !v->config_path[0])
        return 0;

    lock_fd = viewport_config_lock(v->config_path);
    if(lock_fd < 0) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "Unable to lock viewport configuration %s: %s",
                   v->config_path, strerror(errno));
        return 0;
    }

    memset(profiles, 0, sizeof(profiles));
    count = viewport_profiles_load(v->config_path,
                                   profiles,
                                   VIEWPORT_CONFIG_MAX_PROFILES);
    for(int i = 0; i < count; i++) {
        if(profiles[i].port != v->config_port)
            profiles[out++] = profiles[i];
    }

    if(!viewport_profiles_write(v->config_path, profiles, out)) {
        viewport_config_unlock(lock_fd);
        return 0;
    }
    viewport_config_unlock(lock_fd);

    if(!viewport_reset_identity(v))
        return 0;

    char legacy_mesh[1200];
    int n = snprintf(legacy_mesh, sizeof(legacy_mesh), "%s.mesh", v->config_path);
    if(n > 0 && n < (int)sizeof(legacy_mesh))
        unlink(legacy_mesh);

    v->config_frontback = 1;
    veejay_msg(VEEJAY_MSG_INFO,
               "Reset viewport for port %d; saved configuration removed",
               v->config_port);
    return 1;
}

int viewport_get_bound_port(void *data)
{
    viewport_t *v = (viewport_t*)data;
    return v ? v->config_port : 0;
}

void viewport_save_settings(void *data, int frontback, char *path)
{
    (void)path;
    viewport_save_bound_configuration(data, frontback);
}

void viewport_save_current_settings(void *data, const char *homedir,
                                    int mode, int id, int frontback)
{
    (void)homedir;
    (void)mode;
    (void)id;
    viewport_save_bound_configuration(data, frontback);
}


int	viewport_finetune_coord(void *data, int screen_width, int screen_height,int inc_x, int inc_y)
{
    (void)screen_width;
    (void)screen_height;
	viewport_t *v = (viewport_t*) data;
	if(!v->user_ui)
		return 0;

	int point = -1;
	int i;

	//@ use screen width/height
	double dist = 100.0;
	int	x = v->usermouse[4];
	int	y = v->usermouse[5];
	float p_cpy[9];
	float p[9];
	
	p[0] = v->x1;
	p[2] = v->x2;
	p[4] = v->x3;
	p[6] = v->x4;
	p[1] = v->y1;
	p[3] = v->y2;
	p[5] = v->y3;	
	p[7] = v->y4;

	int j;

	float ix = (float) inc_x * 0.1f;
	float iy = (float) inc_y * 0.1f;

	for  ( j = 0 ; j < 8 ; j += 2 ) {
		p_cpy[j] = p[j];
		p_cpy[j+1]=p[j+1];
		p[j]  =  msx(v, p[j] );
		p[j+1]=  msy(v, p[j+1] );
	}	

	if( v->user_ui )
	{
		double dt[4];
		dt[0] = sqrt( (p[0] - x) * (p[0] - x) + ( p[1] - y ) * (p[1] -y ) );
		dt[1] = sqrt( (p[2] - x) * (p[2] - x) + ( p[3] - y ) * (p[3] -y ) );
		dt[2] = sqrt( (p[4] - x) * (p[4] - x) + ( p[5] - y ) * (p[5] -y ) );
		dt[3] = sqrt( (p[6] - x) * (p[6] - x) + ( p[7] - y ) * (p[7] -y ) );
	
		for ( i = 0; i < 4;  i ++ )
		{
			if( dt[i] < dist )
			{
				dist = dt[i];
				point = i;
			}	
		}
	}
	
	if( point < 0 )
		return 0;

	switch( point ) 
	{
		case 0:
		v->x1 = vsx(v, p[0] + ix);
		v->y1 = vsy(v, p[1] + iy);
		break;
		case 1:
		v->x2 = vsx(v, p[2] + ix);
		v->y2 = vsy(v, p[3] + iy);
		break;
		case 2:
		v->x3 = vsx(v,p[4] + ix);
		v->y3 = vsy(v,p[5] + iy);
		break;
		case 3:
		v->x4 = vsx(v,p[6] + ix);
		v->y4 = vsy(v,p[7] + iy);
		break;
	}
	viewport_update_perspective( v, p_cpy );
	if(v->grid)
		viewport_compute_grid(v);
	return 1;
}


static int viewport_mesh_commit_point(viewport_t *v, int index, float x, float y)
{
    vj_output_mesh_point previous;
    if(!v || !v->mesh ||
       !vj_output_mesh_get_point(v->mesh, index, &previous))
        return 0;

    if(!vj_output_mesh_set_point(v->mesh, index, x, y) ||
       !vj_output_mesh_compile(v->mesh)) {
        vj_output_mesh_set_point(v->mesh, index, previous.x, previous.y);
        vj_output_mesh_compile(v->mesh);
        return 0;
    }

    v->mesh_selected_point = index;
    viewport_mesh_sync_legacy_corners(v);
    if(viewport_mesh_is_corner(v, index))
        viewport_mesh_refresh_legacy_transform(v);
    vj_output_mesh_get_bounds(v->mesh,
                              &v->ttx1, &v->tty1,
                              &v->ttx2, &v->tty2);
    return 1;
}

int viewport_mesh_set_grid(void *data, int columns, int rows)
{
    viewport_t *v = (viewport_t*)data;
    if(!v || !v->mesh || !vj_output_mesh_set_grid(v->mesh, columns, rows))
        return 0;
    v->mesh_selected_point = 0;
    v->mesh_hover_point = -1;
    viewport_mesh_sync_legacy_corners(v);
    vj_output_mesh_get_bounds(v->mesh, &v->ttx1, &v->tty1, &v->ttx2, &v->tty2);
    return 1;
}

int viewport_mesh_get_grid(void *data, int *columns, int *rows)
{
    viewport_t *v = (viewport_t*)data;
    if(!v || !v->mesh)
        return 0;
    vj_output_mesh_get_grid(v->mesh, columns, rows);
    return 1;
}

int viewport_mesh_set_point_scaled(void *data, int point, int scale, int x, int y)
{
    viewport_t *v = (viewport_t*)data;
    if(!v || !v->mesh || scale <= 0)
        return 0;

    const int index = viewport_mesh_event_index(v, point);
    if(index < 0)
        return 0;

    const float px = ((float)x / (float)scale) * (float)v->w / 100.0f;
    const float py = ((float)y / (float)scale) * (float)v->h / 100.0f;
    return viewport_mesh_commit_point(v, index, px, py);
}

int viewport_mesh_select_point(void *data, int point)
{
    viewport_t *v = (viewport_t*)data;
    if(!v || !v->mesh)
        return 0;

    const int index = viewport_mesh_event_index(v, point);
    if(index < 0)
        return 0;

    v->mesh_selected_point = index;
    return 1;
}

int viewport_mesh_get_point_scaled(void *data, int point, int scale, int *x, int *y)
{
    viewport_t *v = (viewport_t*)data;
    if(!v || !v->mesh || scale <= 0 || !x || !y)
        return 0;

    const int index = viewport_mesh_event_index(v, point);
    vj_output_mesh_point mesh_point;
    if(index < 0 || !vj_output_mesh_get_point(v->mesh, index, &mesh_point))
        return 0;

    *x = (int)lrintf((mesh_point.x * 100.0f / (float)v->w) * (float)scale);
    *y = (int)lrintf((mesh_point.y * 100.0f / (float)v->h) * (float)scale);
    return 1;
}

int viewport_mesh_get_state(void *data, viewport_mesh_state_t *state)
{
    viewport_t *v = (viewport_t*)data;
    if(!v || !v->mesh || !state)
        return 0;

    vj_output_mesh_get_grid(v->mesh, &state->columns, &state->rows);
    state->point_count = vj_output_mesh_point_count(v->mesh);
    state->selected_point = viewport_mesh_event_number(v, v->mesh_selected_point);
    state->output_width = v->w;
    state->output_height = v->h;
    state->source_x = v->x0;
    state->source_y = v->y0;
    state->source_width = v->w0;
    state->source_height = v->h0;
    return 1;
}

int viewport_mesh_nudge_selected(void *data, int inc_x, int inc_y)
{
    viewport_t *v = (viewport_t*)data;
    if(!v || !v->mesh)
        return 0;

    const int count = vj_output_mesh_point_count(v->mesh);
    if(v->mesh_selected_point < 0 || v->mesh_selected_point >= count)
        v->mesh_selected_point = 0;

    vj_output_mesh_point point;
    if(!vj_output_mesh_get_point(v->mesh, v->mesh_selected_point, &point))
        return 0;

    const float dx = (float)inc_x * 0.001f * (float)v->w;
    const float dy = (float)inc_y * 0.001f * (float)v->h;
    return viewport_mesh_commit_point(v, v->mesh_selected_point,
                                      point.x + dx, point.y + dy);
}

int viewport_mesh_point_count(void *data)
{
    viewport_t *v = (viewport_t*)data;
    return (v && v->mesh) ? vj_output_mesh_point_count(v->mesh) : 0;
}

int viewport_external_mouse(void *data, uint8_t *img[3], int sx, int sy, int button, int frontback, int screen_width, int screen_height, char *homedir, int mode, int id)
{
    (void)img;
    viewport_t *v = (viewport_t*)data;
    
    if (sx == 0 && sy == 0 && button == 0)
        return 0;
    
    if (button == 3 && v->user_ui == 0)
        return 0;

    int ch = 0;
    int mesh_changed = 0;
    int point = -1;
    int mesh_index = -1;
    int i;

    float x = (float)sx / (screen_width / 100.0f);
    float y = (float)sy / (screen_height / 100.0f);
    
    double dist = 100.0;
    int cx = v->w / 2;
    int cy = v->h / 2;
    int dx = cx - (v->ui->sw / 2);
    int dy = cy - (v->ui->sh / 2);
    
    float scx = (float)v->w / (float)v->ui->sw;
    float scy = (float)v->h / (float)v->ui->sh;
    
    int nsx = (sx - dx) * scx;
    int nsy = (sy - dy) * scy;

    v->usermouse[2] = (float)nsx;
    v->usermouse[3] = (float)nsy;
    v->usermouse[4] = x;
    v->usermouse[5] = y;

    float p_cpy[9];
    float p[9];
    
    // Make a copy of the parameters
    p[0] = v->x1; p[1] = v->y1;
    p[2] = v->x2; p[3] = v->y2;
    p[4] = v->x3; p[5] = v->y3;
    p[6] = v->x4; p[7] = v->y4;

    int j;
    for (j = 0; j < 8; j += 2) {
        p_cpy[j] = p[j];
        p_cpy[j + 1] = p[j + 1];
        p[j] = msx(v, p[j]);
        p[j + 1] = msy(v, p[j + 1]);
    }

    float tx = vsx(v, v->usermouse[4]);
    float ty = vsy(v, v->usermouse[5]);

    for (i = 0; i < 4; i++)
        v->users[i] = 1;

    if(v->user_ui) {
        const int mesh_points = vj_output_mesh_point_count(v->mesh);
        for(i = 0; i < mesh_points; i++) {
            vj_output_mesh_point mesh_point;
            if(!vj_output_mesh_get_point(v->mesh, i, &mesh_point))
                continue;

            const float px = msx(v, mesh_point.x * 100.0f / (float)v->w);
            const float py = msy(v, mesh_point.y * 100.0f / (float)v->h);
            const double dt = sqrt((px - x) * (px - x) +
                                   (py - y) * (py - y));
            if(dt < dist) {
                dist = dt;
                mesh_index = i;
            }
        }
        if(mesh_index >= 0)
            point = viewport_mesh_corner_role(v, mesh_index);
        v->mesh_hover_point = mesh_index;
    }
    else {
        v->mesh_hover_point = -1;
    }

    v->save = 0;

    if((button == 1 && mesh_index >= 0) ||
       ((button == 6 || button == 12) && point >= 0))
        v->save = 1;

    if(v->mesh_selected_point >= 0) {
        const int selected_corner = viewport_mesh_corner_role(v, v->mesh_selected_point);
        if(selected_corner >= 0)
            v->users[selected_corner] = 2;
    }

    if(button == 10 && mesh_index >= 0) {
        v->mesh_selected_point = mesh_index;
        return 0;
    }

    if(button == 13 || button == 14) {
        const int count = vj_output_mesh_point_count(v->mesh);
        if(count > 0) {
            int selected = v->mesh_selected_point;
            if(selected < 0 || selected >= count)
                selected = 0;
            selected += (button == 13) ? -1 : 1;
            if(selected < 0)
                selected = count - 1;
            else if(selected >= count)
                selected = 0;
            v->mesh_selected_point = selected;
        }
        return 0;
    }

    if (button == 0)
    {
        v->usermouse[0] = x;
        v->usermouse[1] = y;
    }

    if (button == 2)
    {
        v->user_reverse = !v->user_reverse;
        ch = 1;
    }

    if (button == 3)
    {
        v->user_ui = !v->user_ui;

        if(v->user_ui == 0)
            viewport_save_current_settings(v, homedir, mode, id, frontback);
    }

	if( button == 6 && point >= 0)
	{
		int32_t right = v->x0 + v->w0;
		int32_t bottom = v->y0 + v->h0;

		switch( point )
		{
			case 0:
				v->x0 = nsx; v->y0 = nsy;
				v->w0 = right - v->x0;
				v->h0 = bottom - v->y0;
				break;
			case 1:
				v->y0 = nsy;
				v->w0 = nsx - v->x0;
				v->h0 = bottom - v->y0;
				break;
			case 2:
				v->w0 = nsx - v->x0;    
				v->h0 = nsy - v->y0;
				break;
			case 3:
				v->x0 = nsx;
				v->w0 = right - v->x0;
				v->h0 = nsy - v->y0;
				break;
		}

		if (v->w0 < 10) v->w0 = 10;
		if (v->h0 < 10) v->h0 = 10;
		
		clamp1(v->x0, 0, v->w - v->w0);
		clamp1(v->y0, 0, v->h - v->h0);
		ch = 1;
	}

    if (button == 15) {
        v->grid_mode--;
        if (v->grid_mode < 0)
            v->grid_mode = 2;
    }

    if (button == 5) // Wheel Up
    {
        if (v->grid_mode == 0) {
            v->marker_size--;
            if (v->marker_size < 2) v->marker_size = 4;
        } else {
            v->grid_resolution -= GRID_STEP;
            if (v->grid_resolution < 2) v->grid_resolution = 2;
            viewport_compute_grid(v);
        }
    }

    if (button == 16)
    {
        v->grid_mode++;
        if (v->grid_mode > 2)
            v->grid_mode = 0;
    }

    if (button == 4) // Wheel Down
    {
        if (v->grid_mode == 0) {
            v->marker_size++;
            if (v->marker_size > v->w / 16) v->marker_size = 4;
        } else {
            v->grid_resolution += GRID_STEP;
            if (v->grid_resolution > v->w) v->grid_resolution = v->w;
            viewport_compute_grid(v);
        }
    }

    if (button == 7)
    {
        v->grid_val = (v->grid_val == 0xff) ? 0 : 0xff;
    }

    if(v->save) {
        if(button == 1 && mesh_index >= 0) {
            mesh_changed = viewport_mesh_commit_point(
                v, mesh_index,
                tx * (float)v->w / 100.0f,
                ty * (float)v->h / 100.0f);
        }
        else {
            ch = 1;
        }
    }

    if(mesh_changed) {
        if(v->grid)
            viewport_compute_grid(v);
        return 1;
    }

    if(ch) {
        viewport_update_perspective(v, p_cpy);
        if(v->grid)
            viewport_compute_grid(v);
        return 1;
    }

    return 0;
}

void		viewport_push_frame(void *data, int w, int h, uint8_t *Y, uint8_t *U, uint8_t *V )
{
	viewport_t *v = (viewport_t*) data;
	ui_t *u = v->ui;
    (void)w;
    (void)h;

    u->src_frame->data[0] = Y;
    u->src_frame->data[1] = U;
    u->src_frame->data[2] = V;
    u->dst_frame->data[0] = u->buf[0];
    yuv_convert_and_scale(u->scaler, u->src_frame, u->dst_frame);
}

static void		viewport_translate_frame(void *data, uint8_t *plane ) 
{
	viewport_t *v = (viewport_t*) data;
	ui_t	   *u = v->ui;
	int	    cx = v->w / 2;
	int	    cy = v->h / 2;
	int	     w = v->w;
	int	    dx = cx - ( u->sw / 2 );
	int	    dy = cy - ( u->sh / 2 );

	int 		x,y;

	uint8_t		*img = u->buf[0];
	for( y = 0; y < u->sh; y ++ ) {
		for( x = 0; x < u->sw; x ++ ) {
			plane[ (dy + y ) * w + dx + x ] = img[ y * u->sw + x ];
		}
	}
}

static	void	viewport_draw_marker(viewport_t *v, int x, int y, int w, int h, uint8_t *plane )
{
	int x1 = x - v->marker_size;
	int y1 = y - v->marker_size;
	int x2 = x + v->marker_size;
	int y2 = y + v->marker_size;

	if( x1 < 0 ) x1 = 0; else if ( x1 >= w ) x1 = w - 1;
	if( y1 < 0 ) y1 = 0; else if ( y1 >= h ) y1 = h - 1;
	if( x2 < 0 ) x2 = 0; else if ( x2 >= w ) x2 = w - 1;
	if( y2 < 0 ) y2 = 0; else if ( y2 >= h ) y2 = h - 1;

	int i,j;
	for( j = x1; j < x2 ; j ++ )
		plane[ y1 * w + j ] = v->grid_val;

	for( i = y1; i < y2; i ++ ) 
	{
		plane[ i * w + x1 ] = v->grid_val;
		plane[ i * w + x2 ] = v->grid_val;
	}

	for( j = x1; j < x2 ; j ++ )
		plane[ y2 * w + j ] = v->grid_val;

}

static	void	viewport_draw_grid(viewport_t *v, int width, int height, uint8_t *plane )
{	
	int x,y;
	grid_t *grid = v->grid;
	int k = 0;
	int n = v->grid_width * v->grid_height; 
	int j = 0;
	for( y = 0; y < v->grid_height; y ++) {	
			k = y * v->grid_width;
			j = k + v->grid_width-1;
			viewport_line( plane, grid[k].x, grid[k].y,
			                     grid[j].x, grid[j].y,
						width,height,
						170);
	}

	k = 0;
	n = (v->grid_height-1) * v->grid_width;
	for( x = 0; x < v->grid_width; x ++ )
	{
		k = x;
		j = n + x;
		viewport_line( plane, grid[k].x, grid[k].y,
				      grid[j].x, grid[j].y,
					width,height,
					170);
	} 
}

static	void	viewport_draw_points(viewport_t *v, int width, int height, uint8_t *plane )
{
	int k;
	for(k = 0; k < (v->grid_width*v->grid_height); k ++ ) 
	{
		int x=v->grid[k].x;
		int y=v->grid[k].y;
		if( x >= 0 && y >= 0 && x < width && y < height )
			plane[y * width + x] = v->grid_val;
	}
}
static	void	viewport_compute_grid( viewport_t *v )
{
	int k = 0;
	int gw = v->w/ v->grid_resolution;
	int gh = v->h/v->grid_resolution;
	v->grid_width = gw;
	v->grid_height = gh;

	int x,y;
	if(v->grid) {
		free(v->grid);
		v->grid = NULL;
	}
	if(!v->grid) {
		v->grid = (grid_t*) vj_malloc(sizeof(grid_t) * gw *gh);
	}
	grid_t *grid = v->grid;

	for(y = 0; y < gh; y ++ )
		for( x = 0; x < gw; x ++ ) {
			point_map_int( v->M, x * v->grid_resolution,
				             y * v->grid_resolution,&(grid[k].x), &(grid[k].y));
			k++;
		}
}


void		viewport_set_marker( void *data, int status )
{
	viewport_t *v = (viewport_t*) data;
	v->snap_marker = status;
	//v->marker_size = 1;
}

static int viewport_mesh_ui_point(viewport_t *v, int index, int *x, int *y)
{
    vj_output_mesh_point point;
    if(!vj_output_mesh_get_point(v->mesh, index, &point))
        return 0;

    const float percent_x = point.x * 100.0f / (float)v->w;
    const float percent_y = point.y * 100.0f / (float)v->h;
    *x = (int)(msx(v, percent_x) * ((float)v->w / 100.0f));
    *y = (int)(msy(v, percent_y) * ((float)v->h / 100.0f));
    return 1;
}

static uint8_t viewport_mesh_line_color(const viewport_t *v, int emphasized)
{
    if(v->grid_val == 0xff)
        return emphasized ? 0xff : 0xb0;
    return emphasized ? 0x00 : 0x50;
}

static void viewport_draw_mesh_handle(viewport_t *v, uint8_t *plane,
                                      int width, int height, int index,
                                      int x, int y)
{
    const int selected = index == v->mesh_selected_point;
    const int hovered = index == v->mesh_hover_point;
    const int corner = viewport_mesh_is_corner(v, index);
    const uint8_t color = viewport_mesh_line_color(v, selected || hovered);
    const int size = selected ? 3 : ((hovered || corner) ? 2 : 1);

    draw_point(plane, x, y, width, height, size, color);

    if(selected) {
        const uint8_t cross = (color == 0xff) ? 0x30 : 0xd0;
        viewport_line(plane, x - 8, y, x + 8, y, width, height, cross);
        viewport_line(plane, x, y - 8, x, y + 8, width, height, cross);
    }
}

static void viewport_draw_mesh(viewport_t *v, uint8_t *plane, int width, int height)
{
    int columns = 0;
    int rows = 0;
    vj_output_mesh_get_grid(v->mesh, &columns, &rows);

    int selected_row = -1;
    int selected_col = -1;
    if(v->mesh_selected_point >= 0 &&
       v->mesh_selected_point < columns * rows) {
        selected_row = v->mesh_selected_point / columns;
        selected_col = v->mesh_selected_point % columns;
    }

    for(int row = 0; row < rows; row++) {
        const uint8_t color = viewport_mesh_line_color(v, row == selected_row);
        for(int col = 0; col + 1 < columns; col++) {
            int x0, y0, x1, y1;
            const int p0 = row * columns + col;
            if(viewport_mesh_ui_point(v, p0, &x0, &y0) &&
               viewport_mesh_ui_point(v, p0 + 1, &x1, &y1))
                viewport_line(plane, x0, y0, x1, y1,
                              width, height, color);
        }
    }

    for(int col = 0; col < columns; col++) {
        const uint8_t color = viewport_mesh_line_color(v, col == selected_col);
        for(int row = 0; row + 1 < rows; row++) {
            int x0, y0, x1, y1;
            const int p0 = row * columns + col;
            if(viewport_mesh_ui_point(v, p0, &x0, &y0) &&
               viewport_mesh_ui_point(v, p0 + columns, &x1, &y1))
                viewport_line(plane, x0, y0, x1, y1,
                              width, height, color);
        }
    }

    const int count = columns * rows;
    for(int i = 0; i < count; i++) {
        int x, y;
        if(viewport_mesh_ui_point(v, i, &x, &y))
            viewport_draw_mesh_handle(v, plane, width, height, i, x, y);
    }
}


static void	viewport_draw_col( void *data, uint8_t *plane, uint8_t *u, uint8_t *V )
{
	viewport_t *v = (viewport_t*) data;
    (void)u;
    (void)V;
	int	width = v->w;
	int 	height = v->h;

	float wx =(float) v->w / 100.0f;
	float wy =(float) v->h / 100.0f;

	if(v->grid)
		switch(v->grid_mode)
		{
			case 2:
				viewport_draw_grid(v,width,height,plane);
			break;	
			case 1:
				viewport_draw_points(v,width,height,plane);
			break;
		}

	
    viewport_draw_mesh(v, plane, width, height);
	
	//@ Project rectangle in v->w * v->h , but scaled to size of >sw >sh
	ui_t *ui = v->ui;
	int	    cx = v->w / 2;
	int	    cy = v->h / 2;
	int	    dx = cx - ( ui->sw / 2 );
	int	    dy = cy - ( ui->sh / 2 );
	float		s  = (float) v->w / (float) v->ui->sw;
	float		sy = (float) v->h / (float) v->ui->sh;
	int vx0 = (v->x0  / s) + dx;
	int vy0 = (v->y0 / sy) + dy;
	int vw0 = v->w0  / s;
	int vh0 = v->h0  / sy;
      
       viewport_line( plane,   v->x0,          v->y0,                  v->x0 + v->w0,   v->y0,          width,height, 110);
       viewport_line( plane,   v->x0+v->w0,     v->y0,                  v->x0 + v->w0,   v->y0 + v->h0,   width,height, 110 );
       viewport_line( plane,   v->x0 + v->w0,   v->y0 + v->h0,           v->x0,          v->y0 + v->h0,   width,height, 110 );
       viewport_line( plane,   v->x0,          v->y0 +v->h0,            v->x0,          v->y0,          width,height, 110);


//* Projection quad
       viewport_line( plane,   vx0,          vy0,                  vx0 + vw0,   vy0,          width,height, 65);
       viewport_line( plane,   vx0+vw0,     vy0,                  vx0 + vw0,   vy0 + vh0,   width,height, 65 );
       viewport_line( plane,   vx0 + vw0,   vy0 + vh0,           vx0,          vy0 + vh0,   width,height, 65 );
       viewport_line( plane,   vx0,          vy0 +vh0,            vx0,          vy0,          width,height, 65);


	 int mx = v->usermouse[0] * wx;
	 int my = v->usermouse[1] * wy;
	 
	viewport_draw_marker(v, mx,my,width,height,plane );
	

	 if( mx >= 0 && my >= 0 && mx <= width && my < height )
	 {
		if( mx >= 0 && my >= 0 && mx < width && my < height )
		{
			if( abs(v->grid_val - plane[my*width+mx]) < 32 )
				plane[my*width+mx] = 0xff - plane[my*width+mx];
			else
				plane[my * width + mx] = v->grid_val;
		}
	 }
}

int	viewport_render_ssm(void *vdata )
{
	viewport_t *v = (viewport_t*) vdata;

	if( v->disable || v->user_ui) 
		return 0;

	return 1;
}

void	viewport_draw_interface_color( void *vdata, uint8_t *img[3] )
{
	viewport_t *v = (viewport_t*) vdata;
	viewport_translate_frame( v, img[0] );
	viewport_draw_col( v, img[0],img[1],img[2] );
}

void viewport_produce_full_img(void *vdata, uint8_t *img[3], uint8_t *out_img[3])
{
    viewport_t *v = (viewport_t*)vdata;
    const uint8_t *input[3] = { img[0], img[1], img[2] };
    vj_output_mesh_render_yuv444(v->mesh, input, out_img);
}

void viewport_produce_bw_img(void *vdata, uint8_t *img[3], uint8_t *out_img[3], int Yonly)
{
    if(!Yonly) {
        viewport_produce_full_img(vdata, img, out_img);
        return;
    }
    viewport_t *v = (viewport_t*)vdata;
    vj_output_mesh_render_luma(v->mesh, img[0], out_img[0]);
}

#define pack_yuyv_pixel( y0,u0,u1,y1,v0,v1 )\
        ( (uint32_t)(y0) ) +\
        ( (uint32_t)((u0+u1)>>1) << 8) +\
        ( (uint32_t)(y1) << 16 ) +\
        ( (uint32_t)((v0+v1)>>1) << 24 )

void viewport_produce_full_img_yuyv(void *vdata, uint8_t *restrict img[3], uint8_t *restrict out_img)
{
    viewport_t *v = (viewport_t*)vdata;
    const uint8_t *input[3] = { img[0], img[1], img[2] };
    vj_output_mesh_render_yuyv(v->mesh, input, out_img);
}

void viewport_render_dynamic( void *vdata, uint8_t *in[3], uint8_t *out[3],int width, int height )
{
    (void)width;
    (void)height;
	viewport_t *v = (viewport_t*) vdata;

	viewport_process_dynamic( v, in,out );

}

void *viewport_fx_init_map( int wid, int hei, int x1, int y1,  
		int x2, int y2, int x3, int y3, int x4, int y4, int reverse)
{
	viewport_t *v = (viewport_t*) vj_calloc(sizeof(viewport_t));
    if(!viewport_mesh_create(v, wid, hei, 2, 2)) {
        free(v);
        return NULL;
    }

	v->x1 = x1;
	v->y1 = y1;
	v->x2 = x2;
	v->y2 = y2;
	v->x3 = x3;
	v->y3 = y3;
	v->x4 = x4;
	v->y4 = y4;

	int res = viewport_configure (v, 
			v->x1, v->y1,
			v->x2, v->y2,
			v->x3, v->y3,
			v->x4, v->y4,
			0,0,
			wid,hei,
			wid,hei,
			reverse,
			0xff,
			32 );

	v->user_ui = 0;

	if(! res )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid point locations");
		viewport_destroy( v );
		return NULL;
	}

    return (void*)v;
}

int	viewport_get_mode( void *vv ) {
	viewport_t *v = (viewport_t*) vv;
	return v->user_ui;
}

void *viewport_fx_zoom_init(int type, int wid, int hei, int x, int y, int zoom, int dir)
{
	viewport_t *v = (viewport_t*) vj_calloc(sizeof(viewport_t));
    if(!viewport_mesh_create(v, wid, hei, 2, 2)) {
        free(v);
        return NULL;
    }
	float fracx = (float) wid;
	float fracy = (float) hei;

	fracx *= 0.01f;
	fracy *= 0.01f;

	if( type == VP_QUADZOOM )
	{
		float cx = (float) x;
		float cy = (float) y;

		cx = cx / fracx;
		cy = cy / fracy;

		float  w = 1.0 * zoom * 0.5; 
		float  h = 1.0 * zoom * 0.5;

		v->x1 = cx - w;
		v->y1 = cy - h;
		v->x2 = cx + w;
		v->y2 = cy - h;
		v->x3 = cx + w;
		v->y3 = cy + h;
		v->x4 = cx - w;
		v->y4 = cy + h;
	}

	int res = viewport_configure (v, 
			v->x1, v->y1,
			v->x2, v->y2,
			v->x3, v->y3,
			v->x4, v->y4,
			0,0,
			wid,hei,
			wid,hei,
			dir,
			0xff,
			wid/32 );

	v->user_ui = 0;

	if(! res )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid point locations");
		viewport_destroy( v );
		return NULL;
	}


   	return (void*)v;
}



