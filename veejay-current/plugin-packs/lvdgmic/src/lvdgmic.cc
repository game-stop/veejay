#include <stdint.h>
#include <stdlib.h>
#include <cmath>
#include <gmic.h>
#include "lvdgmic.hh"

lvdgmic::lvdgmic(int n)
{
	images.assign(n);
	buf = (char*) malloc( sizeof(char) * LGDMIC_CMD_LEN );
}

lvdgmic::~lvdgmic()
{
	images.assign(0U);
	free(buf);
}



void lvdgmic::push( int w, int h, int fmt, unsigned char **data, int n )
{
	int n_planes = 1; // Y only for now
	gmic_image<float> &img = images._data[n];
	img.assign(w,h, 1, n_planes );

	/* convert data in src to float */
	float *in = img._data;
	unsigned char *src = data[0];
	unsigned int len = images._data[n]._width * images._data[n]._height * images._data[n]._depth;
	unsigned int i;
	for( i = 0; i < len; i ++ ) {
		in[i] = src[i];
	}
}

void lvdgmic::gmic_command( char const *str )
{
	try {
		gmic_instance.run( str, images, image_names );
	} catch(gmic_exception &e) {
		fprintf(stderr,"GMIC error: %s\n", e.what());
	}
}

void	lvdgmic::pull(int n, unsigned char **frame)
{
	float *out = images._data[n]._data;
	unsigned char *dst = frame[0];
	unsigned int i;
	unsigned int len = images._data[n]._width * images._data[n]._height * images._data[n]._depth;

	/* convert data from float to output */
	for( i = 0; i < len; i ++ ) {
		dst[i] = (unsigned char) out[i];
	}
}
