#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct Restbl Restbl;

Restbl *restbl_parse(const uint8_t *data, size_t n);
void restbl_free(Restbl *t);
const char *restbl_key(const char *path, char *buf, size_t n);
int restbl_get(const Restbl *t, const char *key, uint32_t *size);
int restbl_raise(Restbl *t, const char *key, uint32_t size);
int restbl_merge(Restbl *t, const Restbl *base, const Restbl *shipped);
int restbl_take(Restbl *t, const Restbl *from, const char *const *keys, size_t n);
uint8_t *restbl_write(const Restbl *t, size_t *n);
uint32_t restbl_estimate(const char *key, uint64_t size);
uint32_t restbl_grow(const char *key, uint32_t entry, uint64_t old, uint64_t new_size, int scaled);
typedef void RestblMember(const char *name, const uint8_t *data, size_t size, void *ctx);
int restbl_sarc_each(const uint8_t *p, size_t n, RestblMember *each, void *ctx);
typedef void RestblNote(const char *key, uint32_t was, uint32_t now, void *ctx);
int restbl_pack_raise(Restbl *t, const Restbl *base, const uint8_t *mod, size_t mod_n, const uint8_t *orig,
                      size_t orig_n, RestblNote *note, void *ctx);
int restbl_probe(void);
