/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include <veejaycore/vjmem.h>
#include "pixelate.h"

vj_effect *pixelate_init(int width, int height)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 1;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->limits[0][0] = 1;
	ve->limits[1][0] = (width < height ? width: height);
	ve->defaults[0] = 8;
	ve->description = "Pixelate";
	ve->sub_format = -1;
	ve->extra_frame = 0;
	ve->has_user =0;
	ve->parallel = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Pixel Size");
	return ve;
}

void pixelate_apply( void *ptr, VJFrame *frame, int *args )
{

	int pixelSize = args[0];
    int width = frame->width;
    int height = frame->height;

	uint8_t *dstY = frame->data[0];
	uint8_t *dstU = frame->data[1];
	uint8_t *dstV = frame->data[2];

    for (int i = 0; i < height; i += pixelSize) {
        for (int j = 0; j < width; j += pixelSize) {
            int totalY = 0;
            int totalU = 0;
            int totalV = 0;
            int count = 0;

            for (int y = i; y < i + pixelSize && y < height; ++y) {
                for (int x = j; x < j + pixelSize && x < width; ++x) {
                    int index = y * width + x;
                    totalY += dstY[index];
                    totalU += dstU[index];
                    totalV += dstV[index];
                    count++;
                }
            }

            int averageY = totalY / count;
            int averageU = totalU / count;
            int averageV = totalV / count;

            for (int y = i; y < i + pixelSize && y < height; ++y) {
                for (int x = j; x < j + pixelSize && x < width; ++x) {
                    int index = y * width + x;
                    dstY[index] = (uint8_t)averageY;
                    dstU[index] = (uint8_t)averageU;
                    dstV[index] = (uint8_t)averageV;
                }
            }
        }
    }

}

