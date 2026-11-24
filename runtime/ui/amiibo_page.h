#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void tab_amiibo(float scale);
void amiibo_collect_filenames(void (*callback)(void *, const char *), void *ctx);
void amiibo_request_font_rebuild(void);

#ifdef __cplusplus
}
#endif
