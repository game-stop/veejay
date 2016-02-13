
extern void *memcpy_new_line_size_64_preload_192(void *dest,
    const void *src, size_t n);

extern void *memcpy_new_line_size_64_preload_192_align_32(void *dest,
    const void *src, size_t n);

extern void *memcpy_new_line_size_64_preload_192_aligned_access(void *dest,
    const void *src, size_t n);

extern void *memcpy_new_line_size_32_preload_192(void *dest,
    const void *src, size_t n);

extern void *memcpy_new_line_size_32_preload_192_align_32(void *dest,
    const void *src, size_t n);

extern void *memcpy_new_line_size_32_preload_96(void *dest,
    const void *src, size_t n);

extern void *memcpy_new_line_size_32_preload_96_aligned_access(void *dest,
    const void *src, size_t n);

extern void *memcpy_new_neon_line_size_64(void *dest, const void *src, size_t n);

extern void *memcpy_new_neon_line_size_32(void *dest, const void *src, size_t n);

extern void *memcpy_new_neon_line_size_32_auto(void *dest, const void *src, size_t n);

extern void *memset_new_align_0(void *dest, int c, size_t size);

extern void *memset_new_align_8(void *dest, int c, size_t size);

extern void *memset_new_align_32(void *dest, int c, size_t size);

extern void *memset_neon(void *dest, int c, size_t size);
