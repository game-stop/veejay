
#include "stdlib.h"
#include "wipe.h"
#include "../../config.h"

vj_effect *wipe_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 150;
    ve->defaults[1] = 0;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 25;
    ve->has_internal_data = 0;
    ve->description = "Transition Wipe";
    ve->sub_format = 0;
    ve->extra_frame = 1;
    return ve;
}

static int g_wipe_width = 0;
static int g_wipe_height = 0;
void wipe_apply( VJFrame *frame, VJFrame *frame2,
		int w, int h, int opacity, int inc)
{
    /* w, h increasen */
    transop_apply(frame, frame2, g_wipe_width, g_wipe_height, 0, 0, 0, 0, w,
		  h, opacity);
    g_wipe_width += inc;
    g_wipe_height += ((w / h) - 0.5 + inc);

    if (g_wipe_width > w || g_wipe_height > h) {
	g_wipe_width = 0;
	g_wipe_height = 0;
    }
}
void wipe_free(){}
