// Adapted from nxnative dm/host/pad_jc1.h, pad_pro.h, motion_pro.h (MIT, Copyright (c) 2026 seedds contributors).
// Native Windows Switch 1 Joy-Con (L/R) and Pro Controller Bluetooth/USB HID driver using hidapi.
// Safe zero-controller no-op: when no Nintendo HID controller is connected, the poll thread sleeps cleanly without error.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hidapi/hidapi.h>

#include "cfg.h"
#include "host.h"
#include "input_joycon.h"
#include "platform/platform.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define NINTENDO_VID       0x057Eu
#define PID_JOYCON_L       0x2006u
#define PID_JOYCON_R       0x2007u
#define PID_PRO_CONTROLLER 0x2009u

#define JC_REPORT_LEN      49
#define JC_IMU_PERIOD_NS   5000000ull
#define JC_SCAN_IDLE_NS    500000000ull
#define JC_HANDSHAKE_GAP_NS 45000000ull
#define JC_SILENT_RESYNC_NS 2000000000ull

#define STYLE_FULL_KEY  (1u << 0)
#define STYLE_HANDHELD  (1u << 1)
#define STYLE_JOY_DUAL  (1u << 2)
#define STYLE_JOY_LEFT  (1u << 3)
#define STYLE_JOY_RIGHT (1u << 4)

// NpadButton bit positions (matching runtime/input_map.h and horizon HidNpadButton)
enum {
    BTN_A = 1ull << 0, BTN_B = 1ull << 1, BTN_X = 1ull << 2, BTN_Y = 1ull << 3,
    BTN_LS = 1ull << 4, BTN_RS = 1ull << 5, BTN_L = 1ull << 6, BTN_R = 1ull << 7,
    BTN_ZL = 1ull << 8, BTN_ZR = 1ull << 9, BTN_PLUS = 1ull << 10, BTN_MINUS = 1ull << 11,
    BTN_LEFT = 1ull << 12, BTN_UP = 1ull << 13, BTN_RIGHT = 1ull << 14, BTN_DOWN = 1ull << 15,
    BTN_LSL = 1ull << 24, BTN_LSR = 1ull << 25, BTN_RSL = 1ull << 26, BTN_RSR = 1ull << 27,
};

typedef struct {
    int active;
    hid_device *hid;
    char path[512];
    char serial[32];
    uint16_t pid;
    int kind;              // JC_KIND_*
    int player;            // 0..3 or -1
    int assigned_side;     // 0=left/pro-L, 1=right

    // Handshake & SPI calibration
    int handshake_step;    // 0..8, 9=done
    uint64_t next_cmd_ns;
    int tries;
    uint8_t out_counter;
    int spi_imu_ok, spi_stick_ok, spi_color_ok;

    float accel_scale[3];
    float accel_origin[3];
    float gyro_scale[3];   // deg/s per count
    float gyro_origin[3];

    // Stick calibration [0=left, 1=right] -> [axis 0=x, 1=y]: center, max above, max below
    uint16_t stick_center[2][2];
    uint16_t stick_above[2][2];
    uint16_t stick_below[2][2];

    uint32_t body_color, btn_color;
    int battery;

    // Input state
    int have_report;
    uint64_t last_rx_ns;
    uint64_t imu_last_sample_ns;
    uint64_t buttons;
    int32_t lx, ly, rx, ry;
    int imu_streaming;
    float last_acc_g[3];
    float last_gyro_deg[3];
    MotionFusion *fusion;

    // Rumble queue
    int rumble_dirty;
    uint8_t rumble_bytes[8];
    int rumble_silence_left;
    uint64_t last_rumble_ns;
} JoySlot;

static PlatMutex jc_lock = PLAT_MUTEX_INIT;
static JoySlot slots[JC_MAX_SLOTS];
static volatile int32_t jc_started, jc_stop, jc_rescan_req;
static uint32_t cfg_seen;
static int cached_driver = 1, cached_mode = JC_MODE_AUTO, game_single_hint = 0;

static int16_t rd_s16(const uint8_t *p) { return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

static void slot_default_cal(JoySlot *s) {
    for (int i = 0; i < 3; i++) {
        s->accel_scale[i] = 1.0f / 4096.0f;
        s->accel_origin[i] = 0.0f;
        s->gyro_scale[i] = 0.06103f;
        s->gyro_origin[i] = 0.0f;
    }
    for (int k = 0; k < 2; k++)
        for (int a = 0; a < 2; a++) {
            s->stick_center[k][a] = 2048;
            s->stick_above[k][a] = 1400;
            s->stick_below[k][a] = 1400;
        }
    if (s->kind == JC_KIND_LEFT) {
        s->body_color = 0xffe6b90au;   // Neon Blue RGBA
        s->btn_color = 0xff1e1e00u;
    } else if (s->kind == JC_KIND_RIGHT) {
        s->body_color = 0xff283cffu;   // Neon Red RGBA
        s->btn_color = 0xff0a0a1eu;
    } else {
        s->body_color = 0xff323232u;
        s->btn_color = 0xffffffffu;
    }
}

static int refresh_cfg(void) {
    unsigned v = cfg_version();
    if (v == cfg_seen && jc_started) return 0;
    cfg_seen = v;
    int prev_mode = cached_mode;
    const char *drv = cfg_get("TOTK_JOYCON_DRIVER");
    if (!drv || !*drv || !strcmp(drv, "auto") || !strcmp(drv, "hidapi")) cached_driver = 1;
    else if (!strcmp(drv, "sdl")) cached_driver = 2;
    else cached_driver = 0;

    const char *mode = cfg_get("TOTK_JOYCON_MODE");
    if (!mode || !*mode || !strcmp(mode, "auto")) cached_mode = JC_MODE_AUTO;
    else if (!strcmp(mode, "dual")) cached_mode = JC_MODE_DUAL;
    else if (!strcmp(mode, "single")) cached_mode = JC_MODE_SINGLE;
    else cached_mode = JC_MODE_AUTO;
    return cached_mode != prev_mode;
}

static void send_subcmd(JoySlot *s, uint8_t sub, const uint8_t *arg, size_t arg_len) {
    if (!s->hid) return;
    uint8_t buf[JC_REPORT_LEN] = {0};
    buf[0] = 0x01;
    buf[1] = s->out_counter++ & 0x0fu;
    static const uint8_t neutral[8] = {0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40};
    memcpy(buf + 2, neutral, 8);
    buf[10] = sub;
    if (arg && arg_len) memcpy(buf + 11, arg, arg_len > 38 ? 38 : arg_len);
    int w = hid_write(s->hid, buf, sizeof buf);
    (void)w;
}

static uint8_t player_led_mask(int player) {
    if (player < 0 || player >= JC_MAX_PLAYERS) return 0x0f;
    static const uint8_t masks[4] = {0x01, 0x03, 0x07, 0x0f};
    return masks[player];
}

static void assign_players_locked(void) {
    refresh_cfg();
    int prev_player[JC_MAX_SLOTS];
    for (int i = 0; i < JC_MAX_SLOTS; i++) {
        prev_player[i] = slots[i].player;
        if (!slots[i].active) continue;
        slots[i].player = -1;
        slots[i].assigned_side = (slots[i].kind == JC_KIND_RIGHT) ? 1 : 0;
    }
    int used_mask[JC_MAX_PLAYERS] = {0};   // bit 0: L/Pro, bit 1: R, bit 2: Pro exclusive

    // First assign Pro controllers to lowest players
    for (int i = 0; i < JC_MAX_SLOTS; i++) {
        if (!slots[i].active || slots[i].kind != JC_KIND_PRO) continue;
        for (int p = 0; p < JC_MAX_PLAYERS; p++) {
            if (!used_mask[p]) {
                slots[i].player = p;
                used_mask[p] = 7;
                break;
            }
        }
    }

    int single_mode = (cached_mode == JC_MODE_SINGLE) || (cached_mode == JC_MODE_AUTO && game_single_hint);
    if (single_mode) {
        for (int i = 0; i < JC_MAX_SLOTS; i++) {
            if (!slots[i].active || slots[i].kind == JC_KIND_PRO || slots[i].player >= 0) continue;
            for (int p = 0; p < JC_MAX_PLAYERS; p++) {
                if (!used_mask[p]) {
                    slots[i].player = p;
                    used_mask[p] = 4 | (slots[i].kind == JC_KIND_RIGHT ? 2 : 1);
                    break;
                }
            }
        }
    } else {
        // Auto / Dual: pair 1 L + 1 R onto each free player slot
        for (int i = 0; i < JC_MAX_SLOTS; i++) {
            if (!slots[i].active || slots[i].player >= 0) continue;
            int bit = (slots[i].kind == JC_KIND_RIGHT) ? 2 : 1;
            for (int p = 0; p < JC_MAX_PLAYERS; p++) {
                if ((used_mask[p] & 4) || (used_mask[p] & bit)) continue;
                slots[i].player = p;
                used_mask[p] |= bit;
                break;
            }
        }
    }

    for (int i = 0; i < JC_MAX_SLOTS; i++) {
        if (slots[i].active && slots[i].player != prev_player[i] && slots[i].handshake_step >= 9) {
            slots[i].handshake_step = 8; // re-send player LEDs
            slots[i].next_cmd_ns = 0;
        }
    }
}

static void step_handshake(JoySlot *s, uint64_t now) {
    if (!s->hid || s->handshake_step >= 9) return;
    if (now < s->next_cmd_ns) return;
    s->next_cmd_ns = now + JC_HANDSHAKE_GAP_NS;
    switch (s->handshake_step) {
    case 0: { uint8_t a = 0; send_subcmd(s, 0x02, &a, 1); break; } /* Device info */
    case 1: { uint8_t a = 0x30; send_subcmd(s, 0x03, &a, 1); break; } /* Standard full mode */
    case 2: { uint8_t a = 0x01; send_subcmd(s, 0x40, &a, 1); break; } /* IMU enable */
    case 3: { uint8_t a = 0x01; send_subcmd(s, 0x48, &a, 1); break; } /* Vibration enable */
    case 4: { uint8_t a[5] = {0x20, 0x60, 0x00, 0x00, 24}; send_subcmd(s, 0x10, a, 5); break; } /* IMU factory cal */
    case 5: { uint8_t a[5] = {0x3d, 0x60, 0x00, 0x00, 18}; send_subcmd(s, 0x10, a, 5); break; } /* Stick factory */
    case 6: { uint8_t a[5] = {0x10, 0x80, 0x00, 0x00, 22}; send_subcmd(s, 0x10, a, 5); break; } /* Stick user */
    case 7: { uint8_t a[5] = {0x50, 0x60, 0x00, 0x00, 12}; send_subcmd(s, 0x10, a, 5); break; } /* Colors */
    case 8: { uint8_t a = player_led_mask(s->player); send_subcmd(s, 0x30, &a, 1); break; } /* Player LEDs */
    }
    s->handshake_step++;
}

static void parse_stick_block(JoySlot *s, const uint8_t *b, int k, int right_first_layout) {
    /* Left stick layout in factory 0x603D: max_above(x,y), center(x,y), max_below(x,y).
       Right stick layout in factory 0x603D: center(x,y), max_below(x,y), max_above(x,y). */
    uint16_t vals[6];
    for (int j = 0; j < 3; j++) {
        const uint8_t *p = b + 3 * j;
        vals[2 * j] = (uint16_t)(p[0] | ((p[1] & 0x0fu) << 8));
        vals[2 * j + 1] = (uint16_t)((p[1] >> 4) | (p[2] << 4));
    }
    int c = right_first_layout ? 0 : 1, ab = right_first_layout ? 2 : 0, bl = right_first_layout ? 1 : 2;
    for (int a = 0; a < 2; a++) {
        uint16_t ctr = vals[2 * c + a], hi = vals[2 * ab + a], lo = vals[2 * bl + a];
        if (ctr > 500 && ctr < 3600 && hi > 200 && lo > 200) {
            s->stick_center[k][a] = ctr;
            s->stick_above[k][a] = hi;
            s->stick_below[k][a] = lo;
            s->spi_stick_ok = 1;
        }
    }
}

static void handle_subcmd_reply(JoySlot *s, const uint8_t *r, size_t n) {
    if (n < 20 || r[0] != 0x21) return;
    if (r[13] == 0x90 && r[14] == 0x10) {
        uint32_t addr = (uint32_t)r[15] | ((uint32_t)r[16] << 8) | ((uint32_t)r[17] << 16) | ((uint32_t)r[18] << 24);
        uint8_t slen = r[19];
        const uint8_t *d = r + 20;
        if (20 + slen > n) return;
        if (addr == 0x6020u && slen >= 24) {
            for (int i = 0; i < 3; i++) {
                int16_t oa = rd_s16(d + 2 * i), ca = rd_s16(d + 6 + 2 * i);
                int16_t og = rd_s16(d + 12 + 2 * i), cg = rd_s16(d + 18 + 2 * i);
                int da = (int)ca - (int)oa, dg = (int)cg - (int)og;
                s->accel_origin[i] = (float)oa;
                if (abs(da) > 1000) s->accel_scale[i] = 4.0f / (float)da;
                s->gyro_origin[i] = (float)og;
                if (abs(dg) > 1000) {
                    float s816 = 816.0f / (float)dg, s936 = 936.0f / (float)dg;
                    s->gyro_scale[i] = fabsf(fabsf(s816) - 0.061f) <= fabsf(fabsf(s936) - 0.061f) ? s816 : s936;
                }
            }
            s->spi_imu_ok = 1;
        } else if (addr == 0x603du && slen >= 18) {
            parse_stick_block(s, d, 0, 0);
            parse_stick_block(s, d + 9, 1, 1);
        } else if (addr == 0x8010u && slen >= 22) {
            if (rd_u16(d) == 0xa1b2u) parse_stick_block(s, d + 2, 0, 0);
            if (rd_u16(d + 11) == 0xa1b2u) parse_stick_block(s, d + 13, 1, 1);
        } else if (addr == 0x6050u && slen >= 6) {
            uint32_t body = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | 0xff000000u;
            uint32_t btn = (uint32_t)d[3] | ((uint32_t)d[4] << 8) | ((uint32_t)d[5] << 16) | 0xff000000u;
            if ((body & 0xffffffu) != 0xffffffu) s->body_color = body;
            if ((btn & 0xffffffu) != 0xffffffu) s->btn_color = btn;
            s->spi_color_ok = 1;
        }
    }
}

static int32_t scale_stick_axis(uint16_t raw, uint16_t ctr, uint16_t above, uint16_t below) {
    int diff = (int)raw - (int)ctr;
    int span = diff >= 0 ? (int)above : (int)below;
    if (span <= 10) return 0;
    float norm = (float)diff / (float)span;
    float dead = 0.12f;
    if (fabsf(norm) <= dead) return 0;
    float out = (norm > 0 ? (norm - dead) : (norm + dead)) / (1.0f - dead);
    if (out > 1.0f) out = 1.0f;
    if (out < -1.0f) out = -1.0f;
    return (int32_t)lrintf(out * 32767.0f);
}

static void decode_report_0x30(JoySlot *s, const uint8_t *r, size_t n, uint64_t now) {
    if (n < JC_REPORT_LEN || r[0] != 0x30) return;
    uint8_t bat_nib = (r[2] >> 5) & 0x07u;
    s->battery = bat_nib >= 4 ? 100 : bat_nib == 3 ? 75 : bat_nib == 2 ? 50 : bat_nib == 1 ? 25 : 10;

    uint64_t b = 0;
    if (s->kind == JC_KIND_LEFT || s->kind == JC_KIND_PRO) {
        if (r[5] & 0x01) b |= BTN_DOWN;
        if (r[5] & 0x02) b |= BTN_UP;
        if (r[5] & 0x04) b |= BTN_RIGHT;
        if (r[5] & 0x08) b |= BTN_LEFT;
        if (r[5] & 0x10) b |= BTN_LSR;
        if (r[5] & 0x20) b |= BTN_LSL;
        if (r[5] & 0x40) b |= BTN_L;
        if (r[5] & 0x80) b |= BTN_ZL;
        if (r[4] & 0x01) b |= BTN_MINUS;
        if (r[4] & 0x08) b |= BTN_LS;
    }
    if (s->kind == JC_KIND_RIGHT || s->kind == JC_KIND_PRO) {
        if (r[3] & 0x01) b |= BTN_Y;
        if (r[3] & 0x02) b |= BTN_X;
        if (r[3] & 0x04) b |= BTN_B;
        if (r[3] & 0x08) b |= BTN_A;
        if (r[3] & 0x10) b |= BTN_RSR;
        if (r[3] & 0x20) b |= BTN_RSL;
        if (r[3] & 0x40) b |= BTN_R;
        if (r[3] & 0x80) b |= BTN_ZR;
        if (r[4] & 0x02) b |= BTN_PLUS;
        if (r[4] & 0x04) b |= BTN_RS;
    }
    s->buttons = b;

    const uint8_t *ls = r + 6, *rs = r + 9;
    uint16_t raw_lx = (uint16_t)(ls[0] | ((ls[1] & 0x0fu) << 8));
    uint16_t raw_ly = (uint16_t)((ls[1] >> 4) | (ls[2] << 4));
    uint16_t raw_rx = (uint16_t)(rs[0] | ((rs[1] & 0x0fu) << 8));
    uint16_t raw_ry = (uint16_t)((rs[1] >> 4) | (rs[2] << 4));
    if (s->kind == JC_KIND_LEFT || s->kind == JC_KIND_PRO) {
        s->lx = scale_stick_axis(raw_lx, s->stick_center[0][0], s->stick_above[0][0], s->stick_below[0][0]);
        s->ly = scale_stick_axis(raw_ly, s->stick_center[0][1], s->stick_above[0][1], s->stick_below[0][1]);
    }
    if (s->kind == JC_KIND_RIGHT || s->kind == JC_KIND_PRO) {
        s->rx = scale_stick_axis(raw_rx, s->stick_center[1][0], s->stick_above[1][0], s->stick_below[1][0]);
        s->ry = scale_stick_axis(raw_ry, s->stick_center[1][1], s->stick_above[1][1], s->stick_below[1][1]);
    }

    int any_imu = 0;
    const float deg2rad = (float)(M_PI / 180.0);
    for (int k = 0; k < 3; k++) {
        int16_t v[6];
        const uint8_t *p = r + 13 + 12 * k;
        for (int i = 0; i < 6; i++) {
            v[i] = rd_s16(p + 2 * i);
            any_imu |= v[i] != 0;
        }
        if (!any_imu) continue;
        float ra[3], rg[3];
        for (int i = 0; i < 3; i++) {
            ra[i] = (float)v[i] * s->accel_scale[i];
            rg[i] = ((float)v[3 + i] - s->gyro_origin[i]) * s->gyro_scale[i];
        }
        double acc[3], rot[3];
        if (s->kind == JC_KIND_RIGHT) {
            /* Right Joy-Con: +X long=Switch +Y, +Y short right=Switch +X, +Z back=Switch -Z */
            acc[0] = -ra[1];
            acc[1] = -ra[0];
            acc[2] = ra[2];
            rot[0] = (double)(rg[1] * deg2rad);
            rot[1] = (double)(rg[0] * deg2rad);
            rot[2] = (double)(-rg[2] * deg2rad);
        } else {
            /* Left Joy-Con & Pro Controller: +X long=Switch +Y, +Y left=Switch -X, +Z face=Switch +Z */
            acc[0] = ra[1];
            acc[1] = -ra[0];
            acc[2] = -ra[2];
            rot[0] = (double)(-rg[1] * deg2rad);
            rot[1] = (double)(rg[0] * deg2rad);
            rot[2] = (double)(rg[2] * deg2rad);
        }
        uint64_t ts = now - (uint64_t)(2 - k) * JC_IMU_PERIOD_NS;
        if (ts <= s->imu_last_sample_ns) ts = s->imu_last_sample_ns + 100000ull;
        s->imu_last_sample_ns = ts;
        if (!s->fusion) s->fusion = motion_new();
        motion_feed(s->fusion, acc, rot, ts);
        s->last_acc_g[0] = (float)acc[0];
        s->last_acc_g[1] = (float)acc[1];
        s->last_acc_g[2] = (float)acc[2];
        s->last_gyro_deg[0] = (float)(rot[0] * 180.0 / M_PI);
        s->last_gyro_deg[1] = (float)(rot[1] * 180.0 / M_PI);
        s->last_gyro_deg[2] = (float)(rot[2] * 180.0 / M_PI);
    }
    if (any_imu) s->imu_streaming = 1;
    s->have_report = 1;
    s->last_rx_ns = now;
}

static int find_slot_by_path_locked(const char *path) {
    for (int i = 0; i < JC_MAX_SLOTS; i++)
        if (slots[i].active && !strcmp(slots[i].path, path)) return i;
    return -1;
}

static void close_slot_locked(JoySlot *s) {
    if (s->hid) hid_close(s->hid);
    s->hid = NULL;
    s->active = 0;
    s->have_report = 0;
    s->imu_streaming = 0;
    s->player = -1;
}

static void scan_devices(uint64_t now) {
    if (cached_driver != 1) return;
    struct hid_device_info *devs = hid_enumerate(NINTENDO_VID, 0x0000);
    if (!devs) return;

    for (struct hid_device_info *cur = devs; cur; cur = cur->next) {
        int kind = cur->product_id == PID_JOYCON_L ? JC_KIND_LEFT
                 : cur->product_id == PID_JOYCON_R ? JC_KIND_RIGHT
                 : cur->product_id == PID_PRO_CONTROLLER ? JC_KIND_PRO
                 : JC_KIND_NONE;
        if (kind == JC_KIND_NONE || !cur->path) continue;
        plat_mutex_lock(&jc_lock);
        int exists = find_slot_by_path_locked(cur->path);
        plat_mutex_unlock(&jc_lock);
        if (exists >= 0) continue;

        hid_device *h = hid_open_path(cur->path);
        if (!h) continue;
        hid_set_nonblocking(h, 1);

        plat_mutex_lock(&jc_lock);
        int free_idx = -1;
        for (int i = 0; i < JC_MAX_SLOTS; i++)
            if (!slots[i].active) { free_idx = i; break; }
        if (free_idx < 0) {
            plat_mutex_unlock(&jc_lock);
            hid_close(h);
            continue;
        }
        JoySlot *s = &slots[free_idx];
        MotionFusion *keep_fusion = s->fusion;
        memset(s, 0, sizeof *s);
        s->fusion = keep_fusion ? keep_fusion : motion_new();
        motion_restart(s->fusion);
        s->active = 1;
        s->hid = h;
        s->pid = cur->product_id;
        s->kind = kind;
        snprintf(s->path, sizeof s->path, "%s", cur->path);
        if (cur->serial_number) {
            for (int k = 0; k < 31 && cur->serial_number[k]; k++)
                s->serial[k] = (char)(cur->serial_number[k] & 0x7fu);
        }
        slot_default_cal(s);
        s->next_cmd_ns = now;
        s->last_rx_ns = now;
        assign_players_locked();
        plat_mutex_unlock(&jc_lock);
        rt_log("joycon: connected %s (pid 0x%04x, player %d)",
               kind == JC_KIND_LEFT ? "Joy-Con (L)" : kind == JC_KIND_RIGHT ? "Joy-Con (R)" : "Pro Controller",
               (unsigned)cur->product_id, slots[free_idx].player + 1);
    }
    hid_free_enumeration(devs);
}

static void joycon_thread_main(void *arg) {
    (void)arg;
    if (hid_init() != 0) {
        rt_log("joycon: hid_init failed; physical Joy-Con HID disabled");
        return;
    }
    uint64_t next_scan = 0;
    while (!plat_cas32(&jc_stop, 0, 0)) {
        if (refresh_cfg()) {
            plat_mutex_lock(&jc_lock);
            assign_players_locked();
            plat_mutex_unlock(&jc_lock);
        }
        uint64_t now = kern_now_ns();
        if (cached_driver == 1 && (now >= next_scan || plat_xchg32(&jc_rescan_req, 0))) {
            scan_devices(now);
            next_scan = now + JC_SCAN_IDLE_NS;
        }

        int any_active = 0;
        uint8_t buf[128];
        for (int i = 0; i < JC_MAX_SLOTS; i++) {
            plat_mutex_lock(&jc_lock);
            JoySlot *s = &slots[i];
            if (!s->active || !s->hid) {
                plat_mutex_unlock(&jc_lock);
                continue;
            }
            if (cached_driver != 1) {
                close_slot_locked(s);
                assign_players_locked();
                plat_mutex_unlock(&jc_lock);
                continue;
            }
            any_active = 1;
            step_handshake(s, now);

            for (int rtry = 0; rtry < 6; rtry++) {
                int r = hid_read(s->hid, buf, sizeof buf);
                if (r < 0) {
                    rt_log("joycon: disconnected %s (player %d)",
                           s->kind == JC_KIND_LEFT ? "Joy-Con (L)" : s->kind == JC_KIND_RIGHT ? "Joy-Con (R)" : "Pro Controller",
                           s->player + 1);
                    close_slot_locked(s);
                    assign_players_locked();
                    break;
                }
                if (r == 0) break;
                if (buf[0] == 0x21) handle_subcmd_reply(s, buf, (size_t)r);
                else if (buf[0] == 0x30) decode_report_0x30(s, buf, (size_t)r, now);
                else if (buf[0] == 0x3f && s->handshake_step >= 9) {
                    /* Simple HID mode after sleep: re-run full-mode handshake */
                    s->handshake_step = 1;
                    s->next_cmd_ns = now;
                }
            }
            if (s->active && s->handshake_step >= 9 && now - s->last_rx_ns > JC_SILENT_RESYNC_NS) {
                s->handshake_step = 1;
                s->last_rx_ns = now;
            }
            if (s->active && s->hid && (s->rumble_dirty || s->rumble_silence_left > 0) &&
                now - s->last_rumble_ns >= 15000000ull) {
                uint8_t rp[JC_REPORT_LEN] = {0};
                rp[0] = 0x10;
                rp[1] = s->out_counter++ & 0x0fu;
                memcpy(rp + 2, s->rumble_bytes, 8);
                hid_write(s->hid, rp, sizeof rp);
                s->last_rumble_ns = now;
                if (s->rumble_dirty) s->rumble_dirty = 0;
                else if (s->rumble_silence_left > 0) s->rumble_silence_left--;
            }
            plat_mutex_unlock(&jc_lock);
        }
        plat_sleep_ms(any_active ? 4 : 50);
    }
    plat_mutex_lock(&jc_lock);
    for (int i = 0; i < JC_MAX_SLOTS; i++) close_slot_locked(&slots[i]);
    plat_mutex_unlock(&jc_lock);
    hid_exit();
}

void joycon_init(void) {
    if (plat_xchg32(&jc_started, 1)) return;
    plat_xchg32(&jc_stop, 0);
    refresh_cfg();
    if (!plat_thread_spawn(joycon_thread_main, NULL, "joycon_hid", 0))
        rt_log("joycon: failed to spawn HID thread (falling back to KBM/SDL)");
}

void joycon_shutdown(void) {
    if (!plat_cas32(&jc_started, 0, 0)) return;
    plat_xchg32(&jc_stop, 1);
}

int joycon_driver_mode(void) {
    refresh_cfg();
    return cached_driver;
}

int joycon_pairing_mode(void) {
    refresh_cfg();
    return cached_mode;
}

void joycon_set_game_single_hint(int single) {
    int want = single ? 1 : 0;
    plat_mutex_lock(&jc_lock);
    int changed = (game_single_hint != want);
    game_single_hint = want;
    if (changed && cached_mode == JC_MODE_AUTO) {
        assign_players_locked();
        rt_log("joycon: auto-pairing switched to %s (game request)", want ? "single" : "dual");
    }
    plat_mutex_unlock(&jc_lock);
}

void joycon_rescan(void) { plat_xchg32(&jc_rescan_req, 1); }

void joycon_recenter_all(void) {
    plat_mutex_lock(&jc_lock);
    for (int i = 0; i < JC_MAX_SLOTS; i++)
        if (slots[i].active && slots[i].fusion) motion_restart(slots[i].fusion);
    plat_mutex_unlock(&jc_lock);
    kbm_motion_recenter();
}

void joycon_calibrate_player(int player) {
    uint64_t now = kern_now_ns();
    plat_mutex_lock(&jc_lock);
    for (int i = 0; i < JC_MAX_SLOTS; i++)
        if (slots[i].active && slots[i].player == player && slots[i].fusion)
            motion_calibrate_start(slots[i].fusion, "Joy-Con", now);
    plat_mutex_unlock(&jc_lock);
}

int joycon_get_slot_status(int slot, JoyconSlotStatus *out) {
    if (slot < 0 || slot >= JC_MAX_SLOTS || !out) return 0;
    plat_mutex_lock_shared(&jc_lock);
    const JoySlot *s = &slots[slot];
    if (!s->active) {
        memset(out, 0, sizeof *out);
        out->player = -1;
        plat_mutex_unlock_shared(&jc_lock);
        return 0;
    }
    out->connected = 1;
    out->kind = s->kind;
    out->player = s->player;
    out->battery = s->battery;
    out->imu_ok = s->imu_streaming;
    out->spi_cal_ok = s->spi_imu_ok || s->spi_stick_ok;
    out->pid = s->pid;
    out->body_color = s->body_color;
    out->btn_color = s->btn_color;
    memcpy(out->acc_g, s->last_acc_g, sizeof out->acc_g);
    memcpy(out->gyro_deg, s->last_gyro_deg, sizeof out->gyro_deg);
    snprintf(out->serial, sizeof out->serial, "%s", s->serial);
    plat_mutex_unlock_shared(&jc_lock);
    return 1;
}

int joycon_connected_count(void) {
    int n = 0;
    plat_mutex_lock_shared(&jc_lock);
    for (int i = 0; i < JC_MAX_SLOTS; i++)
        if (slots[i].active && slots[i].have_report) n++;
    plat_mutex_unlock_shared(&jc_lock);
    return n;
}

uint32_t joycon_player_style(int player) {
    if (player < 0 || player >= JC_MAX_PLAYERS) return 0;
    int has_l = 0, has_r = 0, has_pro = 0;
    plat_mutex_lock_shared(&jc_lock);
    for (int i = 0; i < JC_MAX_SLOTS; i++) {
        if (!slots[i].active || slots[i].player != player || !slots[i].have_report) continue;
        if (slots[i].kind == JC_KIND_LEFT) has_l = 1;
        else if (slots[i].kind == JC_KIND_RIGHT) has_r = 1;
        else if (slots[i].kind == JC_KIND_PRO) has_pro = 1;
    }
    plat_mutex_unlock_shared(&jc_lock);
    if (has_pro) return STYLE_FULL_KEY;
    if (has_l && has_r) return STYLE_JOY_DUAL;
    if (has_l) return STYLE_JOY_LEFT;
    if (has_r) return STYLE_JOY_RIGHT;
    return 0;
}

static void apply_horizontal_remap(int kind, uint64_t *buttons, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry) {
    uint64_t in = *buttons;
    uint64_t out = in & (BTN_MINUS | BTN_PLUS | BTN_L | BTN_R | BTN_ZL | BTN_ZR | BTN_LS | BTN_RS);
    if (kind == JC_KIND_LEFT) {
        if (in & BTN_DOWN) out |= BTN_A;
        if (in & BTN_LEFT) out |= BTN_B;
        if (in & BTN_UP) out |= BTN_X;
        if (in & BTN_RIGHT) out |= BTN_Y;
        if (in & BTN_LSL) out |= BTN_L;
        if (in & BTN_LSR) out |= BTN_R;
        int32_t x = *lx, y = *ly;
        *lx = -y;
        *ly = x;
    } else if (kind == JC_KIND_RIGHT) {
        if (in & BTN_X) out |= BTN_A;
        if (in & BTN_A) out |= BTN_B;
        if (in & BTN_Y) out |= BTN_X;
        if (in & BTN_B) out |= BTN_Y;
        if (in & BTN_RSL) out |= BTN_L;
        if (in & BTN_RSR) out |= BTN_R;
        int32_t x = *rx, y = *ry;
        *rx = y;
        *ry = -x;
    }
    *buttons = out;
}

static uint64_t mirror_left_to_right_btns(uint64_t in) {
    uint64_t out = 0;
    if (in & BTN_RIGHT) out |= BTN_A;
    if (in & BTN_DOWN)  out |= BTN_B;
    if (in & BTN_UP)    out |= BTN_X;
    if (in & BTN_LEFT)  out |= BTN_Y;
    if (in & BTN_L)     out |= BTN_R;
    if (in & BTN_ZL)    out |= BTN_ZR;
    if (in & BTN_MINUS) out |= BTN_PLUS;
    if (in & BTN_LS)    out |= BTN_RS;
    if (in & BTN_LSL)   out |= BTN_RSL;
    if (in & BTN_LSR)   out |= BTN_RSR;
    return out;
}

static uint64_t mirror_right_to_left_btns(uint64_t in) {
    uint64_t out = 0;
    if (in & BTN_A)     out |= BTN_RIGHT;
    if (in & BTN_B)     out |= BTN_DOWN;
    if (in & BTN_X)     out |= BTN_UP;
    if (in & BTN_Y)     out |= BTN_LEFT;
    if (in & BTN_R)     out |= BTN_L;
    if (in & BTN_ZR)    out |= BTN_ZL;
    if (in & BTN_PLUS)  out |= BTN_MINUS;
    if (in & BTN_RS)    out |= BTN_LS;
    if (in & BTN_RSL)   out |= BTN_LSL;
    if (in & BTN_RSR)   out |= BTN_LSR;
    return out;
}

int joycon_player_read(int player, uint32_t style_mask, uint64_t *buttons,
                       int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry, int hold_type) {
    if (player < 0 || player >= JC_MAX_PLAYERS) return 0;
    *buttons = 0;
    *lx = *ly = *rx = *ry = 0;
    int found = 0, single_kind = JC_KIND_NONE;
    int has_l = 0, has_r = 0, has_pro = 0;
    plat_mutex_lock_shared(&jc_lock);
    for (int i = 0; i < JC_MAX_SLOTS; i++) {
        const JoySlot *s = &slots[i];
        if (!s->active || s->player != player || !s->have_report) continue;
        found = 1;
        single_kind = s->kind;
        if (s->kind == JC_KIND_LEFT) has_l = 1;
        else if (s->kind == JC_KIND_RIGHT) has_r = 1;
        else if (s->kind == JC_KIND_PRO) has_pro = 1;
        *buttons |= s->buttons;
        if (s->kind == JC_KIND_LEFT || s->kind == JC_KIND_PRO) {
            *lx = s->lx;
            *ly = s->ly;
        }
        if (s->kind == JC_KIND_RIGHT || s->kind == JC_KIND_PRO) {
            *rx = s->rx;
            *ry = s->ry;
        }
    }
    plat_mutex_unlock_shared(&jc_lock);
    if (!found) return 0;
    if (hold_type == 1 && (style_mask == STYLE_JOY_LEFT || style_mask == STYLE_JOY_RIGHT)) {
        apply_horizontal_remap(single_kind, buttons, lx, ly, rx, ry);
        if (single_kind == JC_KIND_RIGHT) { *lx = *rx; *ly = *ry; }
    } else if (has_l && !has_r && !has_pro) {
        if (style_mask & (STYLE_JOY_DUAL | STYLE_FULL_KEY | STYLE_HANDHELD)) {
            *buttons |= mirror_left_to_right_btns(*buttons);
            *rx = *lx;
            *ry = *ly;
        } else if (style_mask == STYLE_JOY_RIGHT) {
            *buttons = mirror_left_to_right_btns(*buttons);
            *rx = *lx;
            *ry = *ly;
            *lx = 0;
            *ly = 0;
        }
    } else if (has_r && !has_l && !has_pro) {
        if (style_mask & (STYLE_JOY_DUAL | STYLE_FULL_KEY | STYLE_HANDHELD)) {
            *buttons |= mirror_right_to_left_btns(*buttons);
            *lx = *rx;
            *ly = *ry;
        } else if (style_mask == STYLE_JOY_LEFT) {
            *buttons = mirror_right_to_left_btns(*buttons);
            *lx = *rx;
            *ly = *ry;
            *rx = 0;
            *ry = 0;
        }
    }
    return 1;
}

void joycon_player_colors(int player, uint32_t *left_body, uint32_t *left_btn,
                          uint32_t *right_body, uint32_t *right_btn) {
    uint32_t lb = 0xffe6b90au, lbt = 0xff1e1e00u;   // Neon Blue
    uint32_t rb = 0xff283cffu, rbt = 0xff0a0a1eu;   // Neon Red
    plat_mutex_lock_shared(&jc_lock);
    for (int i = 0; i < JC_MAX_SLOTS; i++) {
        const JoySlot *s = &slots[i];
        if (!s->active || s->player != player) continue;
        if (s->kind == JC_KIND_LEFT || s->kind == JC_KIND_PRO) {
            lb = s->body_color;
            lbt = s->btn_color;
        }
        if (s->kind == JC_KIND_RIGHT) {
            rb = s->body_color;
            rbt = s->btn_color;
        }
    }
    plat_mutex_unlock_shared(&jc_lock);
    if (left_body) *left_body = lb;
    if (left_btn) *left_btn = lbt;
    if (right_body) *right_body = rb;
    if (right_btn) *right_btn = rbt;
}

static JoySlot *find_imu_slot_locked(int player, int dev) {
    JoySlot *fallback = NULL;
    for (int i = 0; i < JC_MAX_SLOTS; i++) {
        JoySlot *s = &slots[i];
        if (!s->active || s->player != player || !s->imu_streaming || !s->fusion) continue;
        if (s->kind == JC_KIND_PRO) return s;
        if (dev == 0 && s->kind == JC_KIND_LEFT) return s;
        if (dev == 1 && s->kind == JC_KIND_RIGHT) return s;
        fallback = s;
    }
    return fallback;
}

int joycon_read_six_axis(int player, int dev, MotionState *out, int max, uint64_t now_ns) {
    if (player < 0 || player >= JC_MAX_PLAYERS || !out || max <= 0) return 0;
    MotionFusion *f = NULL;
    plat_mutex_lock_shared(&jc_lock);
    JoySlot *s = find_imu_slot_locked(player, dev);
    if (s) f = s->fusion;
    plat_mutex_unlock_shared(&jc_lock);
    if (!f) return 0;
    int n = motion_read(f, out, max, now_ns);
    if (n <= 0) return 0;
    for (int i = n; i < max; i++) out[i] = out[n - 1];
    return 1;
}

void joycon_restart_six_axis(int player, int dev) {
    if (player < 0 || player >= JC_MAX_PLAYERS) return;
    plat_mutex_lock_shared(&jc_lock);
    JoySlot *s = find_imu_slot_locked(player, dev);
    if (s && s->fusion) motion_restart(s->fusion);
    plat_mutex_unlock_shared(&jc_lock);
}

static uint8_t encode_hd_amp(float amp) {
    if (amp <= 0.01f) return 0;
    if (amp > 1.0f) amp = 1.0f;
    int enc = 0;
    if (amp > 0.23f) enc = (int)lrintf(log2f(amp * 8.7f) * 32.0f);
    else if (amp > 0.12f) enc = (int)lrintf(log2f(amp * 17.0f) * 16.0f);
    else enc = (int)lrintf(log2f(amp * 112.0f) * 4.0f);
    if (enc < 1) enc = 1;
    if (enc > 0x7e) enc = 0x7e;
    return (uint8_t)enc;
}

static void encode_hd_rumble(float amp_low, float freq_low, float amp_high, float freq_high, uint8_t out[4]) {
    static const uint8_t neutral[4] = {0x00, 0x01, 0x40, 0x40};
    memcpy(out, neutral, 4);

    uint8_t enc_hi = encode_hd_amp(amp_high);
    if (enc_hi) {
        float f = freq_high > 0.0f ? freq_high : 320.0f;
        if (f < 81.75f) f = 81.75f;
        if (f > 1252.57f) f = 1252.57f;
        int enc_freq = (int)lrintf(log2f(f / 10.0f) * 32.0f);
        uint16_t hf = enc_freq > 0x60 ? (uint16_t)((enc_freq - 0x60) * 4) : 0;
        if (hf > 0x01fc) hf = 0x01fc;
        uint16_t hf_amp = (uint16_t)(enc_hi * 2u);
        out[0] = (uint8_t)(hf & 0xffu);
        out[1] = (uint8_t)(((hf >> 8) & 0x01u) | (hf_amp & 0xfeu));
    }

    uint8_t enc_lo = encode_hd_amp(amp_low);
    if (enc_lo) {
        float f = freq_low > 0.0f ? freq_low : 160.0f;
        if (f < 40.87f) f = 40.87f;
        if (f > 626.28f) f = 626.28f;
        int enc_freq = (int)lrintf(log2f(f / 10.0f) * 32.0f);
        uint8_t lf = enc_freq > 0x40 ? (uint8_t)(enc_freq - 0x40) : 0;
        if (lf > 0x7fu) lf = 0x7fu;
        out[2] = (uint8_t)(lf | ((enc_lo & 0x01u) << 7));
        out[3] = (uint8_t)(0x40u + (enc_lo >> 1));
    }
}

void joycon_send_rumble(int player, int dev, float amp_low, float freq_low, float amp_high, float freq_high) {
    if (player < 0 || player >= JC_MAX_PLAYERS) return;
    uint8_t pkt[4];
    encode_hd_rumble(amp_low, freq_low, amp_high, freq_high, pkt);

    static const uint8_t neutral[4] = {0x00, 0x01, 0x40, 0x40};
    int on = memcmp(pkt, neutral, 4) != 0;

    plat_mutex_lock(&jc_lock);
    int matched = 0;
    JoySlot *fallback = NULL;
    for (int i = 0; i < JC_MAX_SLOTS; i++) {
        JoySlot *s = &slots[i];
        if (!s->active || s->player != player || !s->hid) continue;
        if (!fallback) fallback = s;
        int match = (s->kind == JC_KIND_PRO) ||
                    (dev == 0 && s->kind == JC_KIND_LEFT) ||
                    (dev == 1 && s->kind == JC_KIND_RIGHT);
        if (!match) continue;
        matched = 1;
        if (s->kind == JC_KIND_RIGHT) {
            memcpy(s->rumble_bytes, neutral, 4);
            memcpy(s->rumble_bytes + 4, pkt, 4);
        } else {
            memcpy(s->rumble_bytes, pkt, 4);
            memcpy(s->rumble_bytes + 4, s->kind == JC_KIND_PRO ? pkt : neutral, 4);
        }
        s->rumble_dirty = 1;
        if (on) s->rumble_silence_left = 3;
    }
    if (!matched && fallback) {
        if (fallback->kind == JC_KIND_RIGHT) {
            memcpy(fallback->rumble_bytes, neutral, 4);
            memcpy(fallback->rumble_bytes + 4, pkt, 4);
        } else {
            memcpy(fallback->rumble_bytes, pkt, 4);
            memcpy(fallback->rumble_bytes + 4, fallback->kind == JC_KIND_PRO ? pkt : neutral, 4);
        }
        fallback->rumble_dirty = 1;
        if (on) fallback->rumble_silence_left = 3;
    }
    plat_mutex_unlock(&jc_lock);
}
