// Adapted from MacNativeGames games/mk8dx/runtime/mii_host.h and mii_host.c (MIT, Copyright (c) 2026
// MacNativeGames contributors) and nxnative dm/host/hle_mii.c (MIT, Copyright (c) 2026 seedds contributors).
// Offline nn::mii::Database and Mii applet overrides for NS Sports.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "ov.h"
#include "platform/platform.h"

#define MII_FIELDS 49
#define MII_DEFAULTS 6
#define MII_DB_MAX 100
#define MII_FILE_MAGIC 0x49494d4eu /* "NMII" */

#define MII_R_NOT_FOUND 0x87eu     /* nn::mii module 126, description 4 */
#define MII_R_CANCELED 0xe7eu      /* nn::mii module 126, description 7 */
#define MII_R_TEST_MODE 0x1987eu   /* nn::mii module 126, description 204 */

enum {
    MII_FONT_REGION = 0,
    MII_FAVORITE_COLOR = 1,
    MII_GENDER = 2,
    MII_HEIGHT = 3,
    MII_BUILD = 4,
    MII_TYPE = 5,
    MII_REGION_MOVE = 6,
    MII_BEARD_TYPE = 37,
    MII_MUSTACHE_TYPE = 38,
    MII_GLASSES_TYPE = 41,
    MII_MOLE_TYPE = 45,
};

typedef struct {
    uint8_t create_id[16];
    uint16_t name[11];
    uint8_t f[MII_FIELDS];
    uint8_t pad;
} MiiCharInfo;
_Static_assert(sizeof(MiiCharInfo) == 0x58, "nn::mii::CharInfo");

typedef struct {
    MiiCharInfo info;
    uint32_t source;   /* 0 = database, 1 = default */
} MiiCharInfoElement;
_Static_assert(sizeof(MiiCharInfoElement) == 0x5c, "nn::mii::CharInfoElement");

static const struct { uint8_t lo, hi; } mii_range[MII_FIELDS] = {
    {0, 3},   /* font_region */      {0, 11},  /* favorite_color */   {0, 1},   /* gender */
    {0, 127}, /* height */           {0, 127}, /* build */            {0, 1},   /* type */
    {0, 3},   /* region_move */      {0, 11},  /* faceline_type */    {0, 9},   /* faceline_color */
    {0, 11},  /* faceline_wrinkle */ {0, 11},  /* faceline_make */    {0, 131}, /* hair_type */
    {0, 99},  /* hair_color */       {0, 1},   /* hair_flip */        {0, 59},  /* eye_type */
    {0, 99},  /* eye_color */        {0, 7},   /* eye_scale */        {0, 6},   /* eye_aspect */
    {0, 7},   /* eye_rotate */       {0, 12},  /* eye_x */            {0, 18},  /* eye_y */
    {0, 24},  /* eyebrow_type */     {0, 99},  /* eyebrow_color */    {0, 8},   /* eyebrow_scale */
    {0, 6},   /* eyebrow_aspect */   {0, 11},  /* eyebrow_rotate */   {0, 12},  /* eyebrow_x */
    {3, 18},  /* eyebrow_y */        {0, 17},  /* nose_type */        {0, 8},   /* nose_scale */
    {0, 18},  /* nose_y */           {0, 35},  /* mouth_type */       {0, 99},  /* mouth_color */
    {0, 8},   /* mouth_scale */      {0, 6},   /* mouth_aspect */     {0, 18},  /* mouth_y */
    {0, 99},  /* beard_color */      {0, 5},   /* beard_type */       {0, 5},   /* mustache_type */
    {0, 8},   /* mustache_scale */   {0, 16},  /* mustache_y */       {0, 19},  /* glasses_type */
    {0, 99},  /* glasses_color */    {0, 7},   /* glasses_scale */    {0, 20},  /* glasses_y */
    {0, 1},   /* mole_type */        {0, 8},   /* mole_scale */       {0, 16},  /* mole_x */
    {0, 30},  /* mole_y */
};

static const uint8_t default_face[2][MII_FIELDS] = {
    {0, 0, 0, 64, 64, 0, 0, 0, 0, 0, 0, 33, 1, 0, 2, 8, 4, 3, 4, 2, 12, 6, 1, 4, 3, 6, 2, 10,
     1, 4, 9, 23, 19, 4, 3, 13, 1, 0, 0, 4, 10, 0, 8, 4, 10, 0, 4, 2, 20},
    {0, 0, 1, 64, 64, 0, 0, 0, 0, 0, 0, 12, 1, 0, 4, 8, 4, 3, 3, 2, 12, 0, 1, 4, 3, 6, 2, 10,
     1, 4, 9, 1, 19, 4, 3, 13, 1, 0, 0, 4, 10, 0, 8, 4, 10, 0, 4, 2, 20},
};
static const uint8_t default_favorite[MII_DEFAULTS] = {0, 4, 8, 1, 5, 10};
static const char *const default_names[MII_DEFAULTS] = {"Mii 1", "Mii 2", "Mii 3", "Mii 4", "Mii 5", "Mii 6"};

static PlatMutex mii_lock = PLAT_MUTEX_INIT;
static MiiCharInfo mii_db[MII_DB_MAX];
static int mii_db_count, mii_loaded, mii_updated;

static uint32_t rand_u32(uint32_t bound) {
    uint32_t v = 0;
    if (bound <= 1) return 0;
    if (!plat_random(&v, sizeof v)) return 0;
    return v % bound;
}

static void version4(uint8_t id[16]) {
    id[6] = (uint8_t)((id[6] & 0x0f) | 0x40);
    id[8] = (uint8_t)((id[8] & 0x3f) | 0x80);
}

static void put_name(MiiCharInfo *c, const char *ascii) {
    memset(c->name, 0, sizeof c->name);
    for (unsigned i = 0; ascii[i] && i < 10; i++) c->name[i] = (uint16_t)(unsigned char)ascii[i];
}

static void mii_build_default(MiiCharInfo *out, unsigned index) {
    memset(out, 0, sizeof *out);
    unsigned i = index % MII_DEFAULTS;
    unsigned gender = i >= MII_DEFAULTS / 2;
    memcpy(out->f, default_face[gender], MII_FIELDS);
    out->f[MII_FAVORITE_COLOR] = default_favorite[i];
    static const char seed[] = "NSSdefaultMii00";
    memcpy(out->create_id, seed, 15);
    out->create_id[15] = (uint8_t)i;
    version4(out->create_id);
    put_name(out, default_names[i]);
}

static void mii_build_random(MiiCharInfo *out, int gender) {
    memset(out, 0, sizeof *out);
    for (unsigned i = 0; i < MII_FIELDS; i++)
        out->f[i] = (uint8_t)(mii_range[i].lo + rand_u32((uint32_t)(mii_range[i].hi - mii_range[i].lo + 1u)));
    out->f[MII_FONT_REGION] = 0;
    out->f[MII_TYPE] = 0;
    out->f[MII_REGION_MOVE] = 0;
    out->f[MII_GENDER] = (uint8_t)(gender == 0 || gender == 1 ? gender : (int)rand_u32(2));
    out->f[MII_HEIGHT] = (uint8_t)(32 + rand_u32(64));
    out->f[MII_BUILD] = (uint8_t)(32 + rand_u32(64));
    if (out->f[MII_GENDER]) {
        out->f[MII_BEARD_TYPE] = 0;
        out->f[MII_MUSTACHE_TYPE] = 0;
    }
    if (rand_u32(4)) out->f[MII_GLASSES_TYPE] = 0;
    if (rand_u32(4)) out->f[MII_MOLE_TYPE] = 0;
    plat_random(out->create_id, sizeof out->create_id);
    version4(out->create_id);
    put_name(out, out->f[MII_GENDER] ? "Player F" : "Player M");
}

static int mii_db_path(char *out, size_t cap) {
    const char *prof = rt_config()->profile;
    if (!prof || !*prof) return 0;
    return snprintf(out, cap, "%s/mii_db.bin", prof) < (int)cap;
}

typedef struct {
    uint32_t magic;
    uint32_t count;
    MiiCharInfo items[MII_DB_MAX];
} MiiDbFile;

static void mii_db_save_locked(void) {
    char path[PLAT_PATH_MAX];
    if (!mii_db_path(path, sizeof path)) return;
    MiiDbFile f = {0};
    f.magic = MII_FILE_MAGIC;
    f.count = (uint32_t)mii_db_count;
    if (mii_db_count > 0) memcpy(f.items, mii_db, (size_t)mii_db_count * sizeof mii_db[0]);
    plat_file_write_atomic(path, &f, 8u + (size_t)mii_db_count * sizeof mii_db[0]);
}

static void mii_db_load_locked(void) {
    if (mii_loaded) return;
    mii_loaded = 1;
    mii_db_count = 0;
    char path[PLAT_PATH_MAX];
    if (mii_db_path(path, sizeof path)) {
        uint64_t sz = 0;
        uint8_t *buf = plat_file_read(path, &sz);
        if (buf && sz >= 8) {
            uint32_t magic = 0, cnt = 0;
            memcpy(&magic, buf, 4);
            memcpy(&cnt, buf + 4, 4);
            if (magic == MII_FILE_MAGIC && cnt <= MII_DB_MAX && sz >= 8ull + (uint64_t)cnt * sizeof(MiiCharInfo)) {
                memcpy(mii_db, buf + 8, (size_t)cnt * sizeof(MiiCharInfo));
                mii_db_count = (int)cnt;
            }
        }
        free(buf);
    }
    if (mii_db_count == 0) {
        /* Seed one persistent primary Mii in database source alongside the 6 console defaults. */
        mii_build_default(&mii_db[0], 0);
        put_name(&mii_db[0], "Player 1");
        mii_db[0].create_id[15] = 0x10;
        version4(mii_db[0].create_id);
        mii_db_count = 1;
        mii_db_save_locked();
    }
    rt_log("mii: offline database ready (%d user Mii, %d defaults)", mii_db_count, MII_DEFAULTS);
}

static int mii_count_for_source_locked(int src) {
    int n = 0;
    if (src & 1) n += mii_db_count;
    if (src & 2) n += MII_DEFAULTS;
    return n;
}

static void ov_mii_init(Cpu *c) {
    plat_mutex_lock(&mii_lock);
    mii_db_load_locked();
    plat_mutex_unlock(&mii_lock);
    ret_x(c, 0);
}

static void ov_mii_finalize(Cpu *c) { (void)c; }

static void ov_mii_is_available(Cpu *c) { ret_x(c, 1); }

static void ov_mii_is_updated(Cpu *c) {
    int src = (int32_t)arg_x(c, 1);
    plat_mutex_lock(&mii_lock);
    mii_db_load_locked();
    int r = (src & 1) && mii_updated;
    if (r) mii_updated = 0;
    plat_mutex_unlock(&mii_lock);
    ret_x(c, (uint64_t)(r ? 1 : 0));
}

static void ov_mii_is_full(Cpu *c) {
    plat_mutex_lock(&mii_lock);
    mii_db_load_locked();
    int r = mii_db_count >= MII_DB_MAX;
    plat_mutex_unlock(&mii_lock);
    ret_x(c, (uint64_t)(r ? 1 : 0));
}

static void ov_mii_get_count(Cpu *c) {
    int src = (int32_t)arg_x(c, 1);
    plat_mutex_lock(&mii_lock);
    mii_db_load_locked();
    int n = mii_count_for_source_locked(src);
    plat_mutex_unlock(&mii_lock);
    ret_x(c, (uint64_t)(uint32_t)n);
}

static void ov_mii_get_elements(Cpu *c) {
    int *out_count = arg_p(c, 1);
    MiiCharInfoElement *out = arg_p(c, 2);
    int max = (int32_t)arg_x(c, 3);
    int src = (int32_t)arg_x(c, 4);
    int n = 0;
    plat_mutex_lock(&mii_lock);
    mii_db_load_locked();
    if (max > 0 && out) {
        if (src & 1) {
            for (int i = 0; i < mii_db_count && n < max; i++, n++) {
                out[n].info = mii_db[i];
                out[n].source = 0;
            }
        }
        if (src & 2) {
            for (unsigned i = 0; i < MII_DEFAULTS && n < max; i++, n++) {
                mii_build_default(&out[n].info, i);
                out[n].source = 1;
            }
        }
    }
    plat_mutex_unlock(&mii_lock);
    if (out_count) *out_count = n;
    ret_x(c, 0);
}

static void ov_mii_get_charinfo(Cpu *c) {
    int *out_count = arg_p(c, 1);
    MiiCharInfo *out = arg_p(c, 2);
    int max = (int32_t)arg_x(c, 3);
    int src = (int32_t)arg_x(c, 4);
    int n = 0;
    plat_mutex_lock(&mii_lock);
    mii_db_load_locked();
    if (max > 0 && out) {
        if (src & 1) {
            for (int i = 0; i < mii_db_count && n < max; i++, n++) out[n] = mii_db[i];
        }
        if (src & 2) {
            for (unsigned i = 0; i < MII_DEFAULTS && n < max; i++, n++) mii_build_default(&out[n], i);
        }
    }
    plat_mutex_unlock(&mii_lock);
    if (out_count) *out_count = n;
    ret_x(c, 0);
}

static void ov_mii_get_index(Cpu *c) {
    int *out_index = arg_p(c, 1);
    const MiiCharInfo *info = arg_p(c, 2);
    if (!info || !out_index) { ret_x(c, MII_R_NOT_FOUND); return; }
    plat_mutex_lock(&mii_lock);
    mii_db_load_locked();
    int found = -1;
    for (int i = 0; i < mii_db_count; i++) {
        if (!memcmp(mii_db[i].create_id, info->create_id, sizeof info->create_id)) {
            found = i;
            break;
        }
    }
    plat_mutex_unlock(&mii_lock);
    if (found < 0) {
        ret_x(c, MII_R_NOT_FOUND);
        return;
    }
    *out_index = found;
    ret_x(c, 0);
}

static void ov_mii_update_latest(Cpu *c) {
    MiiCharInfo *info = arg_p(c, 1);
    int src = (int32_t)arg_x(c, 2);
    if (!info) { ret_x(c, MII_R_NOT_FOUND); return; }
    plat_mutex_lock(&mii_lock);
    mii_db_load_locked();
    int found = 0;
    if (src & 1) {
        for (int i = 0; i < mii_db_count; i++) {
            if (!memcmp(mii_db[i].create_id, info->create_id, sizeof info->create_id)) {
                *info = mii_db[i];
                found = 1;
                break;
            }
        }
    }
    if (!found && (src & 2)) {
        for (unsigned i = 0; i < MII_DEFAULTS; i++) {
            MiiCharInfo d;
            mii_build_default(&d, i);
            if (!memcmp(d.create_id, info->create_id, sizeof info->create_id)) {
                *info = d;
                found = 1;
                break;
            }
        }
    }
    plat_mutex_unlock(&mii_lock);
    ret_x(c, found ? 0 : MII_R_NOT_FOUND);
}

static void ov_mii_build_random(Cpu *c) {
    MiiCharInfo *out = arg_p(c, 1);
    const int32_t *desc = arg_p(c, 2);
    int gender = -1;
    if (desc && (desc[0] == 0 || desc[0] == 1)) gender = desc[0];
    if (out) mii_build_random(out, gender);
    ret_x(c, 0);
}

static void ov_mii_build_default(Cpu *c) {
    MiiCharInfo *out = arg_p(c, 1);
    int index = (int32_t)arg_x(c, 2);
    if (out) mii_build_default(out, (unsigned)(index < 0 ? 0 : index));
    ret_x(c, 0);
}

static void ov_mii_format(Cpu *c) {
    plat_mutex_lock(&mii_lock);
    mii_db_count = 0;
    mii_loaded = 1;
    mii_updated = 1;
    mii_db_save_locked();
    plat_mutex_unlock(&mii_lock);
    ret_x(c, 0);
}

static void ov_mii_test_mode(Cpu *c) { ret_x(c, MII_R_TEST_MODE); }

static void ov_mii_append(Cpu *c) {
    int *out_index = arg_p(c, 0);
    plat_mutex_lock(&mii_lock);
    mii_db_load_locked();
    if (mii_db_count >= MII_DB_MAX) {
        plat_mutex_unlock(&mii_lock);
        ret_x(c, MII_R_CANCELED);
        return;
    }
    int idx = mii_db_count++;
    mii_build_random(&mii_db[idx], -1);
    char label[16];
    snprintf(label, sizeof label, "Mii %d", idx + 1);
    put_name(&mii_db[idx], label);
    mii_updated = 1;
    mii_db_save_locked();
    plat_mutex_unlock(&mii_lock);
    if (out_index) *out_index = idx;
    rt_log("mii: AppendMii created Mii #%d", idx + 1);
    ret_x(c, 0);
}

static void ov_mii_show_edit(Cpu *c) {
    rt_log("mii: ShowMiiEdit (offline host returns immediately)");
    ret_x(c, 0);
}

HOST_OVERRIDE("_ZN2nn3mii8Database10InitializeEv", ov_mii_init);
HOST_OVERRIDE("_ZN2nn3mii8Database8FinalizeEv", ov_mii_finalize);
HOST_OVERRIDE("_ZNK2nn3mii8Database11IsAvailableEv", ov_mii_is_available);
HOST_OVERRIDE("_ZN2nn3mii8Database9IsUpdatedEi", ov_mii_is_updated);
HOST_OVERRIDE("_ZNK2nn3mii8Database14IsFullDatabaseEv", ov_mii_is_full);
HOST_OVERRIDE("_ZNK2nn3mii8Database8GetCountEi", ov_mii_get_count);
HOST_OVERRIDE("_ZN2nn3mii8Database3GetEPiPNS0_15CharInfoElementEii", ov_mii_get_elements);
HOST_OVERRIDE("_ZN2nn3mii8Database3GetEPiPNS0_8CharInfoEii", ov_mii_get_charinfo);
HOST_OVERRIDE("_ZNK2nn3mii8Database8GetIndexEPiRKNS0_8CharInfoE", ov_mii_get_index);
HOST_OVERRIDE("_ZNK2nn3mii8Database12UpdateLatestEPNS0_8CharInfoEi", ov_mii_update_latest);
HOST_OVERRIDE("_ZN2nn3mii8Database11BuildRandomEPNS0_8CharInfoERKNS0_10RandomDescE", ov_mii_build_random);
HOST_OVERRIDE("_ZNK2nn3mii8Database12BuildDefaultEPNS0_8CharInfoEi", ov_mii_build_default);
HOST_OVERRIDE("_ZN2nn3mii8Database6FormatEv", ov_mii_format);
HOST_OVERRIDE("_ZN2nn3mii8Database6ImportEPKvm", ov_mii_test_mode);
HOST_OVERRIDE("_ZNK2nn3mii8Database6ExportEPvm", ov_mii_test_mode);
HOST_OVERRIDE("_ZN2nn3mii9AppendMiiEPi", ov_mii_append);
HOST_OVERRIDE("_ZN2nn3mii11ShowMiiEditEv", ov_mii_show_edit);
