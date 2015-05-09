#ifndef LVDUTILSFX
#define LVDUTILSFX

#include <stdio.h>
#include <stdint.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#include <math.h>

#ifdef ARCH_X86
#define fast_sin(d,x) asm("fsin" : "=t" (d) : "0" (x))
#define fast_cos(d,x) asm("fcos" : "=t" (d) : "0" (x))
#define fast_sqrt(res,x) asm ("fsqrt" : "=t" (res) : "0" (x)) 
#define sin_cos(si, co, x) asm ("fsincos" : "=t" (co), "=u" (si) : "0" (x))
#else
#define fast_sin(d,x) d = sin(x)
#define fast_cos(d,x) d = cos(x)
#define fast_sqrt( res,x ) res = sqrt(x)
#define sin_cos(si, co, x)     si = sin(x); co = cos(x)
									
#endif	       
static inline int myround(float n) 
{
  if (n >= 0) 
    return (int)(n + 0.5);
  else
    return (int)(n - 0.5);
}

#define _rgb2yuv(r,g,b,y,u,v)\
 {\
        float Ey = (0.299 * (float)r) + (0.587 * (float)g) + (0.114 * (float) b);\
        float Eu = (-0.168736 * (float)r) - (0.331264 * (float)g) + (0.500 * (float)b) + 128.0;\
        float Ev = (0.500 * (float)r) - (0.418688 * (float)g) - (0.081312 * (float)b)+ 128.0;\
        y = myround(Ey);\
        u = myround(Eu);\
        v = myround(Ev);\
        if( y > 0xff ) y = 0xff ; else if ( y < 0 ) y = 0;\
        if( u > 0xff ) u = 0xff ; else if ( u < 0 ) u = 0;\
        if( v > 0xff ) v = 0xff ; else if ( v < 0 ) v = 0;\
 }


#ifdef USE_MATRIX_PLACEMENT
typedef struct
{
        int w;
        int h;
} matrix_t;

typedef matrix_t (*matrix_f)(int i, int s, int w, int h);
static matrix_t matrix_placementA(int photoindex, int size, int w , int h);
static matrix_t matrix_placementB(int photoindex, int size, int w , int h);
static matrix_f        get_matrix_func(int type);

static matrix_t matrix_placementA(int photoindex, int size, int w , int h)
{
        matrix_t m;
        m.w = (photoindex % size) * (w/size);
        m.h = (photoindex / size) * (h/size);
        return m;
}

static matrix_t matrix_placementB(int photoindex, int size, int w , int h)
{
        matrix_t m;
        m.w = (photoindex/size) * (w/size);
        m.h = (photoindex % size) * (h/size);
        return m;
}

static matrix_t matrix_placementC(int photoindex, int size, int w , int h)
{
        matrix_t m;
        int n = size*size-1;
        m.w = ((n-photoindex) % size) * (w/size);
        m.h = ((n-photoindex) / size) * (h/size);
        return m;
}

static matrix_t matrix_placementD(int photoindex, int size, int w , int h)
{
        matrix_t m;
        int n = size*size-1;
        m.w = ((n-photoindex) / size) * (w/size);
        m.h = ((n-photoindex) % size) * (h/size);
        return m;
}

static matrix_f        get_matrix_func(int type)
{
        if(type==0)
                return &matrix_placementA;
        if(type==1)
                return &matrix_placementB;
        if(type==2)
                return &matrix_placementC;
        return &matrix_placementD;              
}
#endif

static	double	lvd_extract_param_number( livido_port_t *instance, const char *pname, int n )
{
	double pn = 0.0;
	livido_port_t *c = NULL;
	int error = livido_property_get( instance, pname,n, &c );
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif

	error = livido_property_get( c, "value", 0, &pn );
	if( error != LIVIDO_NO_ERROR ) {
		printf(" --> %s idx %d invalid\n", pname, n );
	}
#ifdef STRICT_CHECKING
	assert( error == LIVIDO_NO_ERROR );
#endif
	return pn;	
}


static	int	lvd_extract_param_index( livido_port_t *instance, const char *pname, int n )
{
	int pn = 0;
	livido_port_t *c = NULL;
	int error = livido_property_get( instance, pname,n, &c );
	if( error != LIVIDO_NO_ERROR )
		return 0;
	livido_property_get( c, "value", 0, &pn );
	return pn;	
}

static	int	lvd_extract_param_boolean( livido_port_t *instance, const char *pname, int n )
{
	int pn = 0;
	livido_port_t *c = NULL;
	int error = livido_property_get( instance, pname,n, &c );
	if( error != LIVIDO_NO_ERROR )
		return 0;
	livido_property_get( c, "value", 0, &pn );
	return pn;	
}
static	void	lvd_set_param_number( livido_port_t *instance, const char *pname,int id, double num )
{
	livido_port_t *c = NULL;
	int error = livido_property_get( instance, pname,id, &c );
	if(error!=LIVIDO_NO_ERROR)
		return;
	livido_property_set( c, "value",LIVIDO_ATOM_TYPE_DOUBLE, 1, &num );
}

static	int	lvd_extract_channel_values( livido_port_t *instance,
				    const char *pname,
				    int n,
				    int *w,
				    int *h,
				    uint8_t **pixel_data,
				    int *palette )
{
	livido_port_t	*c = NULL;
	int error = livido_property_get( instance, pname,n, &c );
	error = livido_property_get( c, "width", 0,w );
#ifdef STRICT_CHECKING
	if(error!=LIVIDO_NO_ERROR) printf("%s: width not found\n",__FUNCTION__);
	assert( error == LIVIDO_NO_ERROR );
#endif
	if( error != LIVIDO_NO_ERROR )
		return error;
	error = livido_property_get( c, "height",0,h );
#ifdef STRICT_CHECKING
	if(error!=LIVIDO_NO_ERROR) printf("%s: height not found\n",__FUNCTION__);
	
	assert( error == LIVIDO_NO_ERROR );
#endif
	if( error != LIVIDO_NO_ERROR )
		return error;
	error = livido_property_get( c, "current_palette",0, palette );
#ifdef STRICT_CHECKING
	if(error!=LIVIDO_NO_ERROR) printf("%s: current_palette not found\n",__FUNCTION__);
	
	assert( error == LIVIDO_NO_ERROR );
#endif
	if( error != LIVIDO_NO_ERROR )
		return error;

	int i = 0;
	for( i = 0; i <4 ; i ++ )
	{
		error = livido_property_get( c, "pixel_data", i, &(pixel_data[i]));
#ifdef STRICT_CHECKING
		if( error != LIVIDO_NO_ERROR )
			printf("%s: pixel_data[%d] not set in %s %d\n",__FUNCTION__,i,pname,n);
		assert( error == LIVIDO_NO_ERROR );
#endif
		if( error != LIVIDO_NO_ERROR )
			return error;
	}
	return LIVIDO_NO_ERROR;
}

#endif
