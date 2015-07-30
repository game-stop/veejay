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

static inline int get_n_planes(int fmt) {
	if(fmt < 516 )
		return 1;
	return 3; //FIXME
}

void lvdgmic::push( int w, int h, int fmt, unsigned char **data, int n )
{
	int n_planes = get_n_planes(fmt); 
	gmic_image<float> &img = images._data[n];
	img.assign(w,h, 1, n_planes );

	/* convert data in src to float */
	unsigned int len = images._data[n]._width * images._data[n]._height * images._data[n]._depth;
	unsigned int i;
	int j;

	for( j = 0; j < n_planes; j ++ ) {
		float *in = img._data + (j * len);
		unsigned char *src = data[j];
		for( i = 0; i < len; i ++ ) {
			in[i] = src[i];
		}
	}

	format = fmt;	
}

void lvdgmic::gmic_command( char const *str )
{
	try {
		gmic_instance.run( str, images, image_names );
	} catch(gmic_exception &e) {
		fprintf(stderr,"GMIC error: %s\n", e.what());
	}
}

void lvdgmic::pull(int n, unsigned char **frame)
{
	unsigned int i;
	unsigned int len = images._data[n]._width * images._data[n]._height * images._data[n]._depth;
	int j;
	int n_planes = get_n_planes(format);

	for( j = 0; j < n_planes; j ++ ) {
		/* convert data from float to output */
		unsigned char *dst = frame[j];
		float *out = images._data[n]._data + (j * len);
		for( i = 0; i < len; i ++ ) {
			dst[i] = (unsigned char) out[i];
		}
	}
}
