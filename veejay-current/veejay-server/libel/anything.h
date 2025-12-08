
#ifndef ANYTHING_HH
#define ANYTHING_HH

int raw_io_read_frame(void *ptr, uint8_t *dst);
void *raw_io_open(const char *filename, int w, int h, int fmt );
void raw_io_close(void *ptr);


#endif
