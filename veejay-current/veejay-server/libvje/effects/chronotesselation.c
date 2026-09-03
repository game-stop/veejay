/* 
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
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

 
#include "common.h"
#include <math.h>
#include <stdint.h>
#include <veejaycore/vjmem.h>

#define CHRONOTESSELLATION_PARAMS 13

#define P_FEEDBACK_MIX     0  
#define P_YAW_SPEED        1  
#define P_PITCH            2  
#define P_ZOOM             3  
#define P_INTERVAL         4  
#define P_THICKNESS        5  
#define P_Z_SCALE          6  
#define P_DEPOSIT          7  
#define P_MEMORY           8  
#define P_EROSION          9  
#define P_CHROMA           10 
#define P_COLOR_SHIFT      11 
#define P_GRID_DENSITY     12 

typedef struct {
    int w;
    int h;
    int len;

    int seeded;
    int frame;
    int eff_ready;

    double lfo_yaw;

    uint8_t *prev_y;
    uint8_t *height;
    uint8_t *height_next;

    uint8_t *trail_y;
    uint8_t *trail_u;
    uint8_t *trail_v;

    float eff_feedback_mix;
    float eff_yaw_speed;
    float eff_pitch;
    float eff_zoom;
    float eff_interval;
    float eff_thickness;
    float eff_z_scale;
    float eff_deposit;
    float eff_memory;
    float eff_erosion;
    float eff_chroma;
    float eff_color_shift;
    float eff_grid_density;
} chronotesselation_t;

static inline int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t sf_u8(int v) {
    return (uint8_t) clampi(v, 0, 255);
}

static inline int sf_abs_i(int v) {
    return v < 0 ? -v : v;
}

static inline int sf_blend(int oldv, int newv, int alpha) {
    return (oldv * (256 - alpha) + newv * alpha) >> 8;
}

static inline int sf_smooth_i(float *state, int target, float attack, float release) {
    float cur = *state;
    float diff = (float) target - cur;
    float step = diff >= 0.0f ? attack : release;
    float out = cur + diff * step;
    *state = out;
    return (int) (out + (out >= 0.0f ? 0.5f : -0.5f));
}

vj_effect *chronotesselation_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if (!ve) return NULL;

    ve->num_params = CHRONOTESSELLATION_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->description = "LIDAR Contour Hologram";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->defaults[P_FEEDBACK_MIX]   = 85;  
    ve->defaults[P_YAW_SPEED]      = 0; 
    ve->defaults[P_PITCH]          = 650; 
    ve->defaults[P_ZOOM]           = 60;  
    ve->defaults[P_INTERVAL]       = 16;  
    ve->defaults[P_THICKNESS]      = 2;   
    ve->defaults[P_Z_SCALE]        = 300; 
    ve->defaults[P_DEPOSIT]        = 62;  
    ve->defaults[P_MEMORY]         = 66;  
    ve->defaults[P_EROSION]        = 22;  
    ve->defaults[P_CHROMA]         = 80;   
    ve->defaults[P_COLOR_SHIFT]    = 500; 
    ve->defaults[P_GRID_DENSITY]   = 2;   

    ve->limits[0][P_FEEDBACK_MIX]   = 0;    ve->limits[1][P_FEEDBACK_MIX]   = 100;
    ve->limits[0][P_YAW_SPEED]      = 0;    ve->limits[1][P_YAW_SPEED]      = 1000;
    ve->limits[0][P_PITCH]          = 0;    ve->limits[1][P_PITCH]          = 1000;
    ve->limits[0][P_ZOOM]           = 0;    ve->limits[1][P_ZOOM]           = 100;
    ve->limits[0][P_INTERVAL]       = 4;    ve->limits[1][P_INTERVAL]       = 64;
    ve->limits[0][P_THICKNESS]      = 1;    ve->limits[1][P_THICKNESS]      = 16;
    ve->limits[0][P_Z_SCALE]        = 0;    ve->limits[1][P_Z_SCALE]        = 1000;
    ve->limits[0][P_DEPOSIT]        = 0;    ve->limits[1][P_DEPOSIT]        = 100;
    ve->limits[0][P_MEMORY]         = 0;    ve->limits[1][P_MEMORY]         = 100;
    ve->limits[0][P_EROSION]        = 0;    ve->limits[1][P_EROSION]        = 100;
    ve->limits[0][P_CHROMA]         = 0;    ve->limits[1][P_CHROMA]         = 100;
    ve->limits[0][P_COLOR_SHIFT]    = 0;    ve->limits[1][P_COLOR_SHIFT]    = 1000;
    ve->limits[0][P_GRID_DENSITY]   = 1;    ve->limits[1][P_GRID_DENSITY]   = 16;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Phosphor Decay", "Rotational LFO", "Camera Pitch", "Hologram Scale",
        "Contour Interval", "Line Thickness", "Elevation Scale", "Source Deposit",
        "Terrain Memory", "Erosion", "Chroma Saturation", "Depth Color Shift", "Cloud Density"
    );

    const vj_beat_param_hint_t beat_hints[] = {
        VJ_BEAT_HINT_V2(VJ_BEAT_MEMORY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 40, 95, 68, 94, 220, 1400, 0, 1, 0, VJ_BEAT_COST_CHEAP, 72, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        
        VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, 0, 1000, 100, 100, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, 0, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        
        VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, 0, 1000, 100, 100, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, 0, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        
        VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_AMPLITUDE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_LOW_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 30, 90, 58, 88, 120, 900, 0, 1, 0, VJ_BEAT_COST_CHEAP, 58, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        
        VJ_BEAT_HINT_V2(VJ_BEAT_GRID_SIZE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 4, 32, 82, 100, 8, 520, 0, 2, 0, VJ_BEAT_COST_CHEAP, 84, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        
        VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 1, 16, 92, 100, 8, 420, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        
        VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_AMPLITUDE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 300, 1000, 88, 100, 6, 260, 0, 2, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        
        VJ_BEAT_HINT_V2(VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ACTIVITY, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, 30, 90, 72, 96, 40, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 76, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        
        VJ_BEAT_HINT_V2(VJ_BEAT_MEMORY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 44, 96, 86, 100, 6, 440, 24, 1, 0, VJ_BEAT_COST_CHEAP, 86, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        
        VJ_BEAT_HINT_V2(VJ_BEAT_TURBULENCE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 10, 60, 70, 94, 100, 820, 0, 1, 0, VJ_BEAT_COST_CHEAP, 66, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        
        VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 44, 100, 72, 96, 260, 1800, 0, 1, 0, VJ_BEAT_COST_CHEAP, 64, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        
        VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_PHASE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 250, 750, 72, 96, 100, 820, 0, 2, 0, VJ_BEAT_COST_CHEAP, 62, 0, 0, VJ_BEAT_GROUP_NONE, 0),
        
        VJ_BEAT_HINT_V2(VJ_BEAT_DENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 1, 8, 68, 94, 120, 900, 0, 1, 0, VJ_BEAT_COST_CHEAP, 60, 0, 0, VJ_BEAT_GROUP_NONE, 0)
    };

    return ve;
}

void *chronotesselation_malloc(int w, int h)
{
    chronotesselation_t *c = (chronotesselation_t *) vj_calloc(sizeof(chronotesselation_t));
    if (!c) return NULL;

    c->w = w;
    c->h = h;
    c->len = w * h;
    c->seeded = 0;
    c->frame = 0;
    c->eff_ready = 0;
    c->lfo_yaw = 0.0;

    size_t total_u8 = (size_t) c->len * 6;
    c->prev_y = (uint8_t *) vj_malloc(total_u8);
    if (!c->prev_y) {
        free(c);
        return NULL;
    }

    uint8_t *p = c->prev_y;
    c->height       = p += c->len;
    c->height_next  = p += c->len;
    c->trail_y      = p += c->len;
    c->trail_u      = p += c->len;
    c->trail_v      = p += c->len;

    for (int i = 0; i < c->len; i++) {
        c->prev_y[i] = c->trail_y[i] = 16;
        c->height[i] = c->height_next[i] = 96;
        c->trail_u[i] = c->trail_v[i] = 128;
    }

    return (void *) c;
}

void chronotesselation_free(void *ptr)
{
    chronotesselation_t *c = (chronotesselation_t *) ptr;
    free(c->prev_y); 
    free(c);
}

void chronotesselation_apply(void *ptr, VJFrame *frame, int *args)
{
    chronotesselation_t *c = (chronotesselation_t *) ptr;

    int w = c->w;
    int h = c->h;
    int len = c->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict py = c->prev_y;
    uint8_t *restrict old_ht = c->height;
    uint8_t *restrict new_ht = c->height_next;
    uint8_t *restrict tr_y = c->trail_y;
    uint8_t *restrict tr_u = c->trail_u;
    uint8_t *restrict tr_v = c->trail_v;

    #pragma omp single
    {
        if (!c->eff_ready) {
            c->eff_feedback_mix = (float)args[P_FEEDBACK_MIX];
            c->eff_yaw_speed    = (float)args[P_YAW_SPEED];
            c->eff_pitch        = (float)args[P_PITCH];
            c->eff_zoom         = (float)args[P_ZOOM];
            c->eff_interval     = (float)args[P_INTERVAL];
            c->eff_thickness    = (float)args[P_THICKNESS];
            c->eff_z_scale      = (float)args[P_Z_SCALE];
            c->eff_deposit      = (float)args[P_DEPOSIT];
            c->eff_memory       = (float)args[P_MEMORY];
            c->eff_erosion      = (float)args[P_EROSION];
            c->eff_chroma       = (float)args[P_CHROMA];
            c->eff_color_shift  = (float)args[P_COLOR_SHIFT];
            c->eff_grid_density = (float)args[P_GRID_DENSITY];
            c->eff_ready = 1;
        } else {
            sf_smooth_i(&c->eff_feedback_mix, args[P_FEEDBACK_MIX], 0.150f, 0.110f);
            sf_smooth_i(&c->eff_yaw_speed,    args[P_YAW_SPEED],    0.050f, 0.050f);
            sf_smooth_i(&c->eff_pitch,        args[P_PITCH],        0.080f, 0.060f);
            sf_smooth_i(&c->eff_zoom,         args[P_ZOOM],         0.140f, 0.095f);
            sf_smooth_i(&c->eff_interval,     args[P_INTERVAL],     0.200f, 0.200f);
            sf_smooth_i(&c->eff_thickness,    args[P_THICKNESS],    0.200f, 0.200f);
            sf_smooth_i(&c->eff_z_scale,      args[P_Z_SCALE],      0.120f, 0.080f);
            sf_smooth_i(&c->eff_deposit,      args[P_DEPOSIT],      0.090f, 0.060f);
            sf_smooth_i(&c->eff_memory,       args[P_MEMORY],       0.060f, 0.045f);
            sf_smooth_i(&c->eff_erosion,      args[P_EROSION],      0.085f, 0.060f);
            sf_smooth_i(&c->eff_chroma,       args[P_CHROMA],       0.120f, 0.080f);
            sf_smooth_i(&c->eff_color_shift,  args[P_COLOR_SHIFT],  0.150f, 0.110f);
            sf_smooth_i(&c->eff_grid_density, args[P_GRID_DENSITY], 0.500f, 0.500f);
        }

        double yaw_step = (c->eff_yaw_speed / 1000.0) * 0.05;
        c->lfo_yaw = fmod(c->lfo_yaw + yaw_step, 2.0 * M_PI);
    }

    int deposit = (int)(c->eff_deposit + 0.5f);
    int memory = (int)(c->eff_memory + 0.5f);
    int erosion = (int)(c->eff_erosion + 0.5f);
    
    int ridge_gain = 48 + deposit;
    int upheaval_gain = 32 + deposit;
    int smooth_gain = 4 + ((erosion * 82) / 100);
    int feed = clampi(8 + ((deposit * (150 - (memory >> 1))) / 100), 0, 180);
    int settle = erosion >> 3;

    int fb_alpha = (int)(c->eff_feedback_mix * 2.56f);
    int interval = clampi((int)(c->eff_interval + 0.5f), 1, 64);
    int thickness = clampi((int)(c->eff_thickness + 0.5f), 1, 16);
    int step_size = clampi((int)(c->eff_grid_density + 0.5f), 1, 16);
    
    float f_pitch = (c->eff_pitch / 1000.0f) * M_PI;
    
    float aspect = (float)w / (float)h;
    float fov = (c->eff_zoom / 100.0f) * 2.5f; 
    float z_mult = (c->eff_z_scale / 1000.0f) * 1.5f;
    
    float cos_y = cosf((float)c->lfo_yaw);
    float sin_y = sinf((float)c->lfo_yaw);
    float cos_p = cosf(f_pitch);
    float sin_p = sinf(f_pitch);
    
    int chroma_q = (int)(c->eff_chroma * 2.56f);
    float shift_rad = (c->eff_color_shift / 1000.0f) * M_PI * 2.0f;

    float scan_z = sinf((float)c->frame * 0.03f) * 0.8f * z_mult + (0.8f * z_mult);

    #pragma omp for schedule(static)
    for (int y = 0; y < h; y++) {
        int row = y * w;
        int row_u = (y > 0 ? y - 1 : h - 1) * w;
        int row_d = (y < h - 1 ? y + 1 : 0) * w;
        int sy_row_u = (y > 0 ? y - 1 : y) * w;
        int sy_row_d = (y < h - 1 ? y + 1 : y) * w;

        for (int x = 0; x < w; x++) {
            int i = row + x;
            int hx_l = x > 0 ? x - 1 : w - 1;
            int hx_r = x < w - 1 ? x + 1 : 0;
            int sx_l = x > 0 ? x - 1 : x;
            int sx_r = x < w - 1 ? x + 1 : x;

            int old_h = old_ht[i];
            int avg_h = ((int)old_ht[row + hx_l] + (int)old_ht[row + hx_r] + 
                         (int)old_ht[row_u + x] + (int)old_ht[row_d + x]) >> 2;

            int edge = sf_abs_i((int)Y[row + sx_r] - (int)Y[row + sx_l]) +
                       sf_abs_i((int)Y[sy_row_d + x] - (int)Y[sy_row_u + x]);
            int motion = sf_abs_i((int)Y[i] - (int)py[i]);

            if (edge > 255) edge = 255;

            int ridge = (edge * ridge_gain) >> 7;
            int upheaval = (motion * upheaval_gain) >> 7;

            int target = clampi((int)Y[i] + ridge + upheaval - 24, 0, 255);
            int smoothed = old_h + (((avg_h - old_h) * smooth_gain) >> 8);
            int new_h_val = smoothed + (((target - smoothed) * feed) >> 8);

            if (settle > 0) new_h_val += ((128 - new_h_val) * settle) >> 8;

            new_ht[i] = sf_u8(new_h_val);
            py[i] = Y[i];
        }
    }

    #pragma omp single
    {
        uint8_t *tmp = c->height;
        c->height = c->height_next;
        c->height_next = tmp;
    }

    #pragma omp for schedule(static)
    for (int i = 0; i < len; i++) {
        tr_y[i] = sf_u8((tr_y[i] * fb_alpha) >> 8);
        tr_u[i] = sf_u8(128 + (((tr_u[i] - 128) * fb_alpha) >> 8));
        tr_v[i] = sf_u8(128 + (((tr_v[i] - 128) * fb_alpha) >> 8));
        
        Y[i] = 16;
        U[i] = 128;
        V[i] = 128;
    }

    #pragma omp for schedule(static)
    for (int y = step_size; y < h - step_size; y += step_size) {
        
        float cy = ((float)y / h) - 0.5f;
        
        for (int x = step_size; x < w - step_size; x += step_size) {
            int i = y * w + x;
            int z_int = c->height[i];
            
            if (z_int > 24) {
                int z_l = c->height[i - 1];
                int z_u = c->height[(y - 1) * w + x];
                
                int c_curr = z_int / interval;
                
                if (c_curr != (z_l / interval) || c_curr != (z_u / interval)) {
                    
                    float cx = (((float)x / w) - 0.5f) * aspect;
                    
                    float orig_z = ((float)z_int / 255.0f) * z_mult;

                    float rx = cx * cos_y - cy * sin_y;
                    float ry = cx * sin_y + cy * cos_y;
                    float rz = orig_z;

                    float px = rx;
                    float py = ry * cos_p - rz * sin_p;
                    float pz = ry * sin_p + rz * cos_p; 
                    
                    float cam_dist = 1.8f; 
                    float final_z = pz + cam_dist;

                    int sx = (int)(((px * fov) / final_z) * w) + (w / 2);
                    int sy = (int)(((py * fov) / final_z) * h) + (h / 2);

                    for(int dy = 0; dy < thickness; dy++) {
                        for(int dx = 0; dx < thickness; dx++) {
                            int draw_x = sx + dx;
                            int draw_y = sy + dy;

                            if (draw_x >= 0 && draw_x < w && draw_y >= 0 && draw_y < h) {
                                
                                float dist_to_scan = fabsf(orig_z - scan_z);
                                float scan_intensity = fmaxf(0.0f, 1.0f - (dist_to_scan / 0.15f)); 
                                
                                float depth_fade = fmaxf(0.0f, 1.0f - ((final_z - 0.5f) / 2.5f));

                                float base_glow = 0.5f;
                                float final_intensity = (base_glow + scan_intensity * 0.5f) * depth_fade;
                                int plot_luma = (int)(final_intensity * 255.0f);
                                
                                int out_idx = draw_y * w + draw_x;
                                
                                int current_y = tr_y[out_idx];
                                tr_y[out_idx] = sf_u8(current_y + plot_luma);

                                float hue = ((float)z_int / 255.0f) * M_PI * 4.0f + shift_rad;
                                int u_col = 128 + (int)(cosf(hue) * chroma_q);
                                int v_col = 128 + (int)(sinf(hue) * chroma_q);

                                tr_u[out_idx] = sf_u8( sf_blend(tr_u[out_idx], u_col, plot_luma) );
                                tr_v[out_idx] = sf_u8( sf_blend(tr_v[out_idx], v_col, plot_luma) );
                            }
                        }
                    }
                }
            }
        }
    }

    #pragma omp for schedule(static)
    for (int i = 0; i < len; i++) {
        Y[i] = tr_y[i];
        U[i] = tr_u[i];
        V[i] = tr_v[i];
    }

    #pragma omp single
    c->frame++;
}