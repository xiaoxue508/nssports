#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CHEAT_HEADING,
    CHEAT_CODE,
    CHEAT_KEY,
    CHEAT_DATA,
    CHEAT_MASTER,
    CHEAT_RESTORE,
    CHEAT_REBUILD,
    CHEAT_UNSUPPORTED,
} CheatKind;

typedef struct {
    const char *name;
    const char *mod;
    const char *note;
    CheatKind kind;
    uint32_t buttons;
    char key[24];
} CheatInfo;

void cheats_init(void);
int cheats_count(void);
int cheats_rebuild_pending(void);
void cheats_rebuild_allow(void);
const CheatInfo *cheats_info(int i);
int cheats_on(int i);
void cheats_set(int i, int on);
const char *cheats_file(void);
void cheats_frame(uint64_t frame);
const char *cheats_buttons(uint32_t mask, char *buf, int n);

#ifdef __cplusplus
}
#endif
