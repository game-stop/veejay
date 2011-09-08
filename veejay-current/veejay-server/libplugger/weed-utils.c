#include <config.h>
#include <libavutil/pixfmt.h>
#include <libplugger/specs/mini-weed.h>
static struct {
	int weed_palette;
	int pixfmt;
} weed_palette_table[] = {
	{WEED_PALETTE_YUV422P,	PIX_FMT_YUVJ422P},
	{WEED_PALETTE_YUV420P,	PIX_FMT_YUVJ420P},
	{WEED_PALETTE_I420,		PIX_FMT_YUVJ420P},
	{WEED_PALETTE_YVU420P,	PIX_FMT_YUV420P},
	{WEED_PALETTE_YV12,		PIX_FMT_YUV420P},
	{WEED_PALETTE_RGB24,	PIX_FMT_RGB24},
	{WEED_PALETTE_BGR24,	PIX_FMT_BGR24},
//	{WEED_PALETTE_ARGB,		PIX_FMT_ARGB},
//	{WEED_PALETTE_BGRA,		PIX_FMT_BGRA},
	{-1,					-1},
};

int	weed_palette2_pixfmt(int w, int subtype) {
	int i;
	for( i = 0; weed_palette_table[i].weed_palette != -1 ; i ++ ) {
		if( weed_palette_table[i].weed_palette == w ) {
			if(subtype == WEED_YUV_SUBSPACE_YCBCR || subtype == WEED_YUV_SUBSPACE_BT709 ) {
				if( w == WEED_PALETTE_YUV422P ) {
					return PIX_FMT_YUV422P;
				} else {
					return PIX_FMT_YUV420P;
				}
			}
			return weed_palette_table[i].pixfmt;
		}
	}
	return -1;
}

