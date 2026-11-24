#pragma once
#include <stdint.h>

enum { MOD_ROMFS = 1, MOD_EXEFS = 2, MOD_SUBSDK = 4, MOD_CHEATS = 8 };
typedef enum {
    MOD_OK,
    MOD_EMPTY,
    MOD_PLUGIN,
    MOD_EXEFS_LATER,
    MOD_EXEFS_UNSUPPORTED,
    MOD_EXEFS_OTHER,
    MOD_BAD_NAME,
} ModProblem;

typedef struct {
    char name[256];
    char romfs[2048];
    char exefs[2048];
    char cheats[2048];
    int kind, enabled;
    int id;
    int no_code;
    int without_code;
    char code[64];
    uint32_t unsupported;
    ModProblem problem;
    uint32_t replaced, added, overridden;
    uint32_t other_version;
    char note[256];
    uint32_t pending, pending_key;
    int nsets;
    uint32_t sets[16];
    int ndata;
    uint32_t data[16];
    uint32_t data_bytes;
    char overrides[256];
    int nplugins, plugin_hooks;
    uint32_t plugin_keys[4], plugin_contents[4];
    char plugin_file[64];
    char plugin_note[256];
    char plugin_off[256];
} ModInfo;

typedef enum { MOD_PART_FILES, MOD_PART_DATA, MOD_PART_CODE, MOD_PARTS } ModPart;
typedef struct {
    int winner, loser;
    ModPart part;
    uint32_t count;
} ModConflict;

void mods_conflict(int winner, int loser, ModPart part, uint32_t count);
int mods_conflict_count(void);
const ModConflict *mods_conflict_at(int i);
void mods_conflicts_log(ModPart part);
const ModInfo *mods_by_id(int id);
void mods_scan(void);
void mods_peek_romfs(void (*cb)(const char *name, const char *romfs, void *ctx), void *ctx);
const char *mods_dir(void);
int mods_count(void);
ModInfo *mods_at(int i);
void mods_set_enabled(int i, int on);
void mods_set_no_code(int i, int on);
void mods_move(int i, int delta);
int mods_changed(void);
void mods_exefs_switch(void);
void mods_exefs_apply(uint64_t base, uint64_t size, uint64_t code_end,
                      int (*store)(uint64_t addr, const uint8_t *bytes, int n));
long mods_exefs_data(const ModInfo *m, uint64_t base, uint64_t size, uint64_t code_end,
                     int (*store)(uint64_t addr, const uint8_t *bytes, int n));
void mods_probe_cache(const char *status, const char *data);
void mods_rescan(void);
int mods_exefs_pending(uint32_t *key);
void mods_plugin_off(int set, const char *why);
void mods_probe_plugins(const char *status, const char *stale);
const char *mods_main_build_id(void);
