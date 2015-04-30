typedef void Clvdgmic;
#ifdef __cplusplus
extern "C" {
#endif

Clvdgmic *lvdgmic_new(int n);
void lvdgmic_delete(Clvdgmic *ptr);
void lvdgmic_push(Clvdgmic *ptr, int w,int h, int fmt, uint8_t **data, int n);
void lvdgmic_pull(Clvdgmic *ptr, int n, uint8_t **data);
void lvdgmic_gmic(Clvdgmic *ptr, const char *gmic_command);
#ifdef __cplusplus
}
#endif
