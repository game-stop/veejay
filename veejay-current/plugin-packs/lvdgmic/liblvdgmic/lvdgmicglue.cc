#include <stdint.h>
#include <stdlib.h>
#include <gmic.h>
#include "lvdgmicglue.h"
#include "lvdgmic.hh"

extern "C" {

Clvdgmic *lvdgmic_new(int n) {
	lvdgmic *l = new lvdgmic(n);
	return (Clvdgmic*)l;
}

void	lvdgmic_delete(Clvdgmic *ptr)
{
	lvdgmic *l = (lvdgmic*) ptr;
	delete l;
}

void	lvdgmic_push(Clvdgmic *ptr, int w, int h, int fmt, uint8_t **data, int n )
{
	lvdgmic *l = (lvdgmic*) ptr;
	l->push( w,h,fmt, data, n );
}

void	lvdgmic_pull(Clvdgmic *ptr, int n, uint8_t **data)
{
	lvdgmic *l = (lvdgmic*)ptr;
	l->pull(n,data);
}

void	lvdgmic_gmic(Clvdgmic *ptr, const char *str)
{
	lvdgmic *l = (lvdgmic*) ptr;
#ifdef SILENT
	snprintf(l->buf,LGDMIC_CMD_LEN,"-verbose -1 %s", str );
	l->gmic_command( l->buf );
#else
	l->gmic_command( str );
#endif
}

}
