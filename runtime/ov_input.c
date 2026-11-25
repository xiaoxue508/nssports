// Adapted from MacNativeGames games/totk/runtime/input.c and hid_feedback.c @ b75be8cf (MIT, Copyright (c) 2026
// MacNativeGames contributors).
// nn::hid overrides: one connected controller fed by input_poll (gamepad and keyboard,
// runtime/input_win.c; nothing pressed with TOTK_INPUT=none). Docked (default) it is a full-key controller as player 1 (id 0, style bit 0); with
// TOTK_OPERATION_MODE=handheld it is the handheld controller (id 0x20, style bit 1) and player 1 is not connected,
// which is the controller the game reads in that mode. Mouse and keyboard report no input. The controller has two
// vibration devices (left, right) played on the gamepad's motors (TOTK_RUMBLE=0: none) and a six-axis sensor fed by
// the gamepad's gyro and accelerometer (TOTK_MOTION=0, live: reads at rest). State layouts follow libnx HID.
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "gfx.h"
#include "host.h"
#include "input.h"
#include "input_joycon.h"
#include "ov.h"
#include "platform/platform.h"

typedef struct { int64_t sampling; uint64_t buttons; int32_t lx, ly, rx, ry; uint32_t attributes, pad; } NpadState;
typedef struct { int64_t sampling; int32_t x, y, dx, dy, wx, wy; uint32_t buttons, attributes; } MouseState;
typedef struct { int64_t sampling; uint32_t modifiers, attributes; uint64_t keys[4]; } KeyboardState;
typedef struct { int8_t players; uint8_t pad[3]; uint32_t selected; } ControllerSupportResult;
_Static_assert(sizeof(NpadState) == 40 && sizeof(MouseState) == 40 && sizeof(KeyboardState) == 48, "hid state ABI");
_Static_assert(sizeof(ControllerSupportResult) == 8, "controller support result ABI");

enum {
    STYLE_FULL_KEY = 1u << 0,
    STYLE_HANDHELD = 1u << 1,
    STYLE_JOY_DUAL = 1u << 2,
    STYLE_JOY_LEFT = 1u << 3,
    STYLE_JOY_RIGHT = 1u << 4,
    ATTR_CONNECTED = 1u << 0,
    ATTR_WIRED = 1u << 1,
    ATTR_LEFT_CONN = 1u << 2,
    ATTR_LEFT_WIRED = 1u << 3,
    ATTR_RIGHT_CONN = 1u << 4,
    ATTR_RIGHT_WIRED = 1u << 5,
};
#define RES_CTRL_CANCELED ((3101u << 9) | 202u)
#define RES_INVALID_NPAD  ((869u << 9) | 202u)

static PlatMutex lock = PLAT_MUTEX_INIT;
static uint32_t styles = STYLE_FULL_KEY | STYLE_JOY_DUAL | STYLE_JOY_LEFT | STYLE_JOY_RIGHT;
static uint32_t supported_ids = (1u << 0) | (1u << 9);   // No1 and Handheld by default until SetSupportedNpadIdType
static uint32_t single_mask;                             // per player bit: forced Single Joy-Con mode
static int enabled, mouse_ready, keyboard_ready, hold_type;
static int64_t sample;

static uint32_t id_bit(uint32_t id) {
    if (id < 8) return 1u << id;
    if (id == 0x10u) return 1u << 8;
    if (id == 0x20u) return 1u << 9;
    return 0;
}

static uint32_t pad_id(void) { return ov_handheld() ? 0x20u : 0u; }

static int npad_to_player(uint32_t id) {
    if (id < 4) return (ov_handheld() && id == 0) ? -1 : (int)id;
    if (id == 0x20u && ov_handheld()) return 0;
    return -1;
}

static uint32_t pick_first_style(uint32_t mask) {
    if (ov_handheld() && (mask & STYLE_HANDHELD)) return STYLE_HANDHELD;
    if (mask & STYLE_JOY_DUAL) return STYLE_JOY_DUAL;
    if (mask & STYLE_JOY_RIGHT) return STYLE_JOY_RIGHT;
    if (mask & STYLE_JOY_LEFT) return STYLE_JOY_LEFT;
    if (mask & STYLE_FULL_KEY) return STYLE_FULL_KEY;
    if (mask & STYLE_HANDHELD) return STYLE_HANDHELD;
    return 0;
}

// Active style reported on `id` taking into account physical Joy-Con/Pro HID controllers, KBM P1 fallback, and game styles.
static uint32_t npad_active_style(uint32_t id) {
    if (!enabled) return 0;
    if (!(supported_ids & id_bit(id))) return 0;
    int p = npad_to_player(id);
    if (p < 0) return 0;
    uint32_t phys = joycon_player_style(p);
    if (phys) {
        if ((phys == STYLE_JOY_DUAL) && (single_mask & (1u << p)) && (styles & (STYLE_JOY_LEFT | STYLE_JOY_RIGHT)))
            phys = (styles & STYLE_JOY_RIGHT) ? STYLE_JOY_RIGHT : STYLE_JOY_LEFT;
        if (phys & styles) return phys & styles;
        /* If game doesn't accept FullKey (e.g. NS Sports with Pro Controller), map P1 Pro onto JoyDual if accepted */
        if (phys == STYLE_FULL_KEY && (styles & STYLE_JOY_DUAL)) return STYLE_JOY_DUAL;
        return pick_first_style(styles);
    }
    if (p == 0) return pick_first_style(styles);
    return 0;
}

static uint32_t pad_style(void) {
    uint32_t s = npad_active_style(pad_id());
    return s ? s : (ov_handheld() ? STYLE_HANDHELD : STYLE_FULL_KEY);
}

static uint32_t style_to_index(uint32_t s) {
    if (s & STYLE_FULL_KEY) return 3;
    if (s & STYLE_HANDHELD) return 4;
    if (s & STYLE_JOY_DUAL) return 5;
    if (s & STYLE_JOY_LEFT) return 6;
    if (s & STYLE_JOY_RIGHT) return 7;
    return 3;
}

static const uint32_t *need_id(Cpu *c, int i, const char *fn) {
    const uint32_t *id = arg_p(c, i);
    if (!id) rt_fatal("nn::hid::%s: null controller id", fn);
    return id;
}

static uint64_t add_stick_bits(uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry) {
    const int32_t th = 16384;
    if (lx < -th) b |= 1ull << 16;
    if (ly > th)  b |= 1ull << 17;
    if (lx > th)  b |= 1ull << 18;
    if (ly < -th) b |= 1ull << 19;
    if (rx < -th) b |= 1ull << 20;
    if (ry > th)  b |= 1ull << 21;
    if (rx > th)  b |= 1ull << 22;
    if (ry < -th) b |= 1ull << 23;
    return b;
}

// void InitializeNpad()
static void ov_initialize_npad(Cpu *c) {
    (void)c;
    plat_mutex_lock(&lock);
    enabled = 1;
    plat_mutex_unlock(&lock);
    joycon_init();
    kbm_motion_init();
    rt_log("hid: controllers initialized (%s, %d physical HID controller(s))",
           ov_handheld() ? "handheld" : "docked", joycon_connected_count());
}

// void SetSupportedNpadIdType(const NpadIdType *ids, size_t count)
static void ov_supported_ids(Cpu *c) {
    const uint32_t *ids = arg_p(c, 0);
    uint64_t n = arg_x(c, 1);
    if (n > 10 || (!ids && n)) rt_fatal("nn::hid::SetSupportedNpadIdType: invalid id list (%llu ids)", (unsigned long long)n);
    uint32_t m = 0;
    for (uint64_t i = 0; i < n; i++) m |= id_bit(ids[i]);
    plat_mutex_lock(&lock);
    uint32_t prev = supported_ids;
    supported_ids = m;
    plat_mutex_unlock(&lock);
    if (prev != m) rt_log("hid: SetSupportedNpadIdType mask=%#x (%llu ids)", m, (unsigned long long)n);
    joycon_init();
    kbm_motion_init();
}

// void SetSupportedNpadStyleSet(NpadStyleSet styles)
static void ov_supported_styles(Cpu *c) {
    uint32_t s = (uint32_t)arg_x(c, 0);
    plat_mutex_lock(&lock);
    uint32_t prev = styles;
    styles = s;
    plat_mutex_unlock(&lock);
    if (prev != s) rt_log("hid: SetSupportedNpadStyleSet=%#x", s);
}

// NpadStyleSet GetSupportedNpadStyleSet()
static void ov_get_supported_styles(Cpu *c) {
    plat_mutex_lock_shared(&lock);
    uint32_t v = styles;
    plat_mutex_unlock_shared(&lock);
    ret_x(c, v);
}

// NpadStyleSet GetNpadStyleSet(const NpadIdType &id)
static void ov_style_set(Cpu *c) {
    uint32_t id = *need_id(c, 0, "GetNpadStyleSet");
    plat_mutex_lock_shared(&lock);
    uint32_t v = npad_active_style(id);
    plat_mutex_unlock_shared(&lock);
    ret_x(c, v);
}

// int GetNpadStates(<State> *out, int count, const NpadIdType &id)
static int fill_npad_states(Cpu *c, NpadState *out, int max, uint32_t id, uint32_t want) {
    if (max <= 0 || !out) return 0;
    plat_mutex_lock(&lock);
    uint32_t cur_style = npad_active_style(id);
    int ht = hold_type;
    int64_t base_s = sample += (max < 16 ? max : 16);
    plat_mutex_unlock(&lock);

    NpadState s = {.sampling = base_s};
    int p = npad_to_player(id);
    int live = want && cur_style && (cur_style & want);
    if (live && p >= 0) {
        s.attributes = ATTR_CONNECTED;
        if (want & STYLE_JOY_DUAL) s.attributes |= ATTR_LEFT_CONN | ATTR_RIGHT_CONN;
        else if (want & STYLE_JOY_LEFT) s.attributes |= ATTR_LEFT_CONN;
        else if (want & STYLE_JOY_RIGHT) s.attributes |= ATTR_RIGHT_CONN;
        else if (want & STYLE_HANDHELD) {
            s.attributes |= ATTR_WIRED | ATTR_LEFT_CONN | ATTR_LEFT_WIRED | ATTR_RIGHT_CONN | ATTR_RIGHT_WIRED;
        }
        if (joycon_player_style(p) != 0) {
            joycon_player_read(p, want, &s.buttons, &s.lx, &s.ly, &s.rx, &s.ry, ht);
        }
        if (p == 0) {
            uint64_t kbtn = 0;
            int32_t klx = 0, kly = 0, krx = 0, kry = 0;
            input_poll(&kbtn, &klx, &kly, &krx, &kry);
            kbm_motion_poll(&kbtn, &klx, &kly, &krx, &kry);
            s.buttons |= kbtn;
            if ((int64_t)klx * klx + (int64_t)kly * kly > (int64_t)s.lx * s.lx + (int64_t)s.ly * s.ly) {
                s.lx = klx;
                s.ly = kly;
            }
            if ((int64_t)krx * krx + (int64_t)kry * kry > (int64_t)s.rx * s.rx + (int64_t)s.ry * s.ry) {
                s.rx = krx;
                s.ry = kry;
            }
            quicksave_guest(c);
            vk_reflex_marker(6, vk_reflex_frame());   // VK_LATENCY_MARKER_INPUT_SAMPLE_NV
        }
        s.buttons = add_stick_bits(s.buttons, s.lx, s.ly, s.rx, s.ry);
    }
    int n = max < 16 ? max : 16;
    for (int i = 0; i < n; i++) {
        out[i] = s;
        out[i].sampling = base_s - i;
    }
    return n;
}

static void npad_states(Cpu *c, uint32_t want) {
    NpadState *out = arg_p(c, 0);
    int max = (int)arg_x(c, 1);
    uint32_t id = *need_id(c, 2, "GetNpadStates");
    if (max <= 0) { ret_x(c, 0); return; }
    if (!out) rt_fatal("nn::hid::GetNpadStates: null output");
    ret_x(c, (uint64_t)fill_npad_states(c, out, max, id, want));
}
static void ov_states_full_key(Cpu *c) { npad_states(c, STYLE_FULL_KEY); }
static void ov_states_handheld(Cpu *c) { npad_states(c, STYLE_HANDHELD); }
static void ov_states_joy_dual(Cpu *c) { npad_states(c, STYLE_JOY_DUAL); }
static void ov_states_joy_left(Cpu *c) { npad_states(c, STYLE_JOY_LEFT); }
static void ov_states_joy_right(Cpu *c) { npad_states(c, STYLE_JOY_RIGHT); }

static void ov_state_single(Cpu *c, uint32_t want) {
    NpadState *out = arg_p(c, 0);
    uint32_t id = *need_id(c, 1, "GetNpadState");
    if (out) fill_npad_states(c, out, 1, id, want);
}
static void ov_state_full_key(Cpu *c) { ov_state_single(c, STYLE_FULL_KEY); }
static void ov_state_handheld(Cpu *c) { ov_state_single(c, STYLE_HANDHELD); }
static void ov_state_joy_dual(Cpu *c) { ov_state_single(c, STYLE_JOY_DUAL); }
static void ov_state_joy_left(Cpu *c) { ov_state_single(c, STYLE_JOY_LEFT); }
static void ov_state_joy_right(Cpu *c) { ov_state_single(c, STYLE_JOY_RIGHT); }

// void SetNpadJoyHoldType(NpadJoyHoldType type)
static void ov_hold_type(Cpu *c) {
    int64_t t = (int32_t)arg_x(c, 0);
    if (t < 0 || t > 1) rt_fatal("nn::hid::SetNpadJoyHoldType: invalid hold type %lld", (long long)t);
    plat_mutex_lock(&lock);
    int prev = hold_type;
    hold_type = (int)t;
    plat_mutex_unlock(&lock);
    if (prev != (int)t) rt_log("hid: npad joy hold type %d (%s)", (int)t, t ? "horizontal" : "vertical");
}

static void ov_get_hold_type(Cpu *c) {
    plat_mutex_lock_shared(&lock);
    int ht = hold_type;
    plat_mutex_unlock_shared(&lock);
    ret_x(c, (uint64_t)ht);
}

static void ov_get_joy_assign(Cpu *c) {
    uint32_t id = *need_id(c, 0, "GetNpadJoyAssignment");
    plat_mutex_lock_shared(&lock);
    int s = id < 4 ? (int)((single_mask >> id) & 1u) : 0;
    plat_mutex_unlock_shared(&lock);
    if (!s && id < 4) {
        uint32_t ps = joycon_player_style((int)id);
        if (ps == STYLE_JOY_LEFT || ps == STYLE_JOY_RIGHT) s = 1;
    }
    ret_x(c, (uint64_t)s);
}

static void ov_set_joy_single(Cpu *c) {
    uint32_t id = *need_id(c, 0, "SetNpadJoyAssignmentModeSingle");
    if (id >= 4) return;
    plat_mutex_lock(&lock);
    uint32_t prev = single_mask;
    single_mask |= 1u << id;
    plat_mutex_unlock(&lock);
    if (!(prev & (1u << id)))
        rt_log("hid: SetNpadJoyAssignmentModeSingle(id=%u)", id);
    if (id == 0) joycon_set_game_single_hint(1);
}

static void ov_set_joy_dual(Cpu *c) {
    uint32_t id = *need_id(c, 0, "SetNpadJoyAssignmentModeDual");
    if (id >= 4) return;
    plat_mutex_lock(&lock);
    uint32_t prev = single_mask;
    single_mask &= ~(1u << id);
    plat_mutex_unlock(&lock);
    if (prev & (1u << id))
        rt_log("hid: SetNpadJoyAssignmentModeDual(id=%u)", id);
    if (id == 0) joycon_set_game_single_hint(0);
}

static void ov_swap_npad(Cpu *c) { (void)c; }

// void DisconnectNpad(const NpadIdType &id)
static void ov_disconnect(Cpu *c) {
    uint32_t id = *need_id(c, 0, "DisconnectNpad");
    (void)id;
}

// Result ShowControllerSupport(ControllerSupportResultInfo *out, const ControllerSupportArg &arg)
static uint32_t run_controller_support(ControllerSupportResult *out, const int8_t *arg) {
    int8_t min_p = arg ? arg[0] : 1, max_p = arg ? arg[1] : 4;
    int enable_single = arg ? (int)arg[5] : 0;
    if (max_p <= 0) max_p = 4;
    int connected = 1;
    for (int p = 1; p < JC_MAX_PLAYERS; p++)
        if (joycon_player_style(p) != 0) connected++;
    if (joycon_pairing_mode() == JC_MODE_AUTO && joycon_connected_count() >= 2 &&
        (connected < min_p || (max_p >= 2 && enable_single))) {
        joycon_set_game_single_hint(1);
        connected = 1;
        for (int p = 1; p < JC_MAX_PLAYERS; p++)
            if (joycon_player_style(p) != 0) connected++;
    }
    if (connected > max_p) connected = max_p;
    uint32_t sel = pad_id();
    if (out) *out = (ControllerSupportResult){.players = (int8_t)connected, .selected = sel};
    if (min_p > connected) {
        rt_log("hid: ShowControllerSupport wanted %d..%d players, %d connected (canceled)", min_p, max_p, connected);
        return RES_CTRL_CANCELED;
    }
    rt_log("hid: ShowControllerSupport min=%d max=%d single=%d -> %d connected (sel=%#x)",
           min_p, max_p, enable_single, connected, sel);
    return 0;
}

static void ov_controller_support(Cpu *c) {
    ControllerSupportResult *out = arg_p(c, 0);
    const int8_t *arg = arg_p(c, 1);
    ret_x(c, run_controller_support(out, arg));
}

static void ov_controller_support_nores(Cpu *c) {
    const int8_t *arg = arg_p(c, 0);
    ret_x(c, run_controller_support(NULL, arg));
}

static void ov_initialize_mouse(Cpu *c) {
    (void)c;
    plat_mutex_lock(&lock);
    mouse_ready = 1;
    plat_mutex_unlock(&lock);
}

static void ov_initialize_keyboard(Cpu *c) {
    (void)c;
    plat_mutex_lock(&lock);
    keyboard_ready = 1;
    plat_mutex_unlock(&lock);
}

// int GetMouseStates(MouseState *out, int count): a mouse that is not connected
static void ov_mouse_states(Cpu *c) {
    MouseState *out = arg_p(c, 0);
    int max = (int)arg_x(c, 1);
    if (max <= 0) { ret_x(c, 0); return; }
    if (!out) rt_fatal("nn::hid::GetMouseStates: null output");
    plat_mutex_lock(&lock);
    if (!mouse_ready) rt_fatal("nn::hid::GetMouseStates before InitializeMouse");
    *out = (MouseState){.sampling = ++sample};
    plat_mutex_unlock(&lock);
    ret_x(c, 1);
}

// void GetKeyboardState(KeyboardState *out): a keyboard with no key down
static void ov_keyboard_state(Cpu *c) {
    KeyboardState *out = arg_p(c, 0);
    if (!out) rt_fatal("nn::hid::GetKeyboardState: null output");
    plat_mutex_lock(&lock);
    if (!keyboard_ready) rt_fatal("nn::hid::GetKeyboardState before InitializeKeyboard");
    *out = (KeyboardState){.sampling = ++sample};
    plat_mutex_unlock(&lock);
}

// ---- vibration and six-axis sensor (MacNativeGames hid_feedback.c)
// Handles are the SDK's (nn::hid::utility, sdk+0x4a6e00 and sdk+0x4a6ab0): byte 0 the style index (3 full-key,
// 4 handheld), byte 1 the controller id, byte 2 the device (vibration 0 left / 1 right, six-axis 2). Every call under
// the public functions goes through nn::hid::detail (sdk's PLT), answered here instead of by the hid service, including
// the batches VibrationTarget queues through detail::EnqueueVibrationValue into detail::SendVibrationValues.
// VibrationValue {amplitude low, frequency low, amplitude high, frequency high}: 0..1 and Hz, neutral 0/160/0/320.
typedef struct { float amp_low, freq_low, amp_high, freq_high; } VibrationValue;
typedef struct { uint32_t type, position; } VibrationDeviceInfo;   // type 1 linear resonant actuator; position 1 left, 2 right
_Static_assert(sizeof(VibrationValue) == 16 && sizeof(VibrationDeviceInfo) == 8, "vibration ABI");

enum { STYLE_INDEX_FULL_KEY = 3, STYLE_INDEX_HANDHELD = 4, SIX_AXIS_DEVICE = 2, RUMBLE_MS = 250 };
#define RUMBLE_STALE_NS 250000000ull     // a side the game has not updated for 250 ms is silent
#define RUMBLE_REFRESH_NS 50000000ull    // an unchanged value is sent again every 50 ms (its 250 ms start over)
#define RUMBLE_MIN_GAP_NS 10000000ull    // a changed value at most every 10 ms (the game streams every few ms)
static const VibrationValue vib_neutral = {0, 160, 0, 320};

static int env_zero(const char *name) {
    const char *e = cfg_get(name);
    return e && !strcmp(e, "0");
}

static void once(volatile int32_t *flag, const char *fmt, uint32_t v) {
    if (!plat_xchg32(flag, 1)) rt_log(fmt, v);
}

// TOTK_RUMBLE_STRENGTH (percent, 0..300, default 130; 0 = off) and TOTK_RUMBLE_CURVE (exponent on the amplitude, 0.2..1.5,
// default 0.6). HD rumble is strong at low amplitude, an ERM motor hardly turns there, so weak values are lifted by the
// curve (0.1 -> 0.25) and the strength scales the result (saturating at 65535; 0.66 and up reach full drive at 130 %).
// Both are live settings (the menu): read again whenever cfg_version changes.
static float rumble_strength = 1.3f, rumble_curve = 0.6f;
static volatile int32_t rumble_cfg_logged;
static unsigned rumble_cfg_seen = ~0u;
static float cfg_float(const char *name, float lo, float hi) {
    double v = cfg_double(name);
    return (float)(v < lo ? lo : v > hi ? hi : v);
}
static void rumble_cfg(void) {
    unsigned v = cfg_version();
    if (v == rumble_cfg_seen) return;
    rumble_cfg_seen = v;
    rumble_strength = cfg_float("TOTK_RUMBLE_STRENGTH", 0, 300) / 100;
    rumble_curve = cfg_float("TOTK_RUMBLE_CURVE", 0.2f, 1.5f);
    int trig = input_rumble_triggers_enabled();
    if (!plat_xchg32(&rumble_cfg_logged, 1))
        rt_log("hid: rumble strength %.0f%%, curve %.2f, triggers %s", rumble_strength * 100, rumble_curve, trig ? "on" : "off");
}
static uint16_t motor(float a) { return rumble_motor(a, rumble_curve, rumble_strength); }

static uint32_t pad_style_index(void) { return style_to_index(pad_style()); }

// The SDK's id check (utility functions): players 1-8, other (0x10) and handheld (0x20); the public functions abort
// on another id.
static uint32_t handle_id(Cpu *c, const char *fn) {
    uint32_t id = *need_id(c, 2, fn);
    if (id >= 8 && id != 0x10 && id != 0x20) rt_fatal("nn::hid::%s: invalid controller id %#x", fn, id);
    return id;
}

// int GetVibrationDeviceHandles(VibrationDeviceHandle *out, int count, const NpadIdType &id, NpadStyleSet style)
static void ov_vibration_handles(Cpu *c) {
    uint32_t *out = arg_p(c, 0), style = (uint32_t)arg_x(c, 3);
    int max = (int)arg_x(c, 1);
    uint32_t id = handle_id(c, "GetVibrationDeviceHandles");
    if (!out || max <= 0 || !style)
        rt_fatal("nn::hid::GetVibrationDeviceHandles: invalid arguments (count %d, styles %#x)", max, style);
    static volatile int32_t asked;
    rumble_cfg();
    if (env_zero("TOTK_RUMBLE")) { once(&asked, "hid: no vibration devices (TOTK_RUMBLE=%u)", 0); ret_x(c, 0); return; }
    plat_mutex_lock_shared(&lock);
    uint32_t active = npad_active_style(id);
    plat_mutex_unlock_shared(&lock);
    uint32_t pick = (style & active) ? (style & active) : style;
    static const struct { uint8_t bit, n; uint32_t base; } t[] = {
        {0, 2, 0x3}, {1, 2, 0x4}, {2, 2, 0x5}, {3, 1, 0x6}, {4, 1, 0x10007}, {5, 1, 0x8}, {10, 1, 0xd}, {8, 2, 0x4},
        {30, 2, 0x21}, {29, 2, 0x20}};
    int n = 0;
    for (size_t i = 0; i < sizeof t / sizeof *t && !n; i++) {
        if (!(pick >> t[i].bit & 1)) continue;
        n = t[i].n < max ? t[i].n : max;
        for (int k = 0; k < n; k++) out[k] = (t[i].base | id << 8) + ((uint32_t)k << 16);
    }
    once(&asked, "hid: the game asked for its vibration devices (first handle %#x)", n ? out[0] : 0);
    ret_x(c, (uint64_t)n);
}

// int GetSixAxisSensorHandles(SixAxisSensorHandle *out, int count, const NpadIdType &id, NpadStyleSet style)
static void ov_six_axis_handles(Cpu *c) {
    uint32_t *out = arg_p(c, 0), style = (uint32_t)arg_x(c, 3);
    int max = (int)arg_x(c, 1);
    uint32_t id = handle_id(c, "GetSixAxisSensorHandles");
    if (!out || max <= 0) rt_fatal("nn::hid::GetSixAxisSensorHandles: invalid arguments (count %d)", max);
    static volatile int32_t asked;
    plat_mutex_lock_shared(&lock);
    uint32_t active = npad_active_style(id);
    plat_mutex_unlock_shared(&lock);
    uint32_t pick = (style & active) ? (style & active) : style;
    uint32_t base = 0, n = 1;
    if (pick & 1) base = 0x20003;
    else if (pick & 2) base = 0x20004;
    else if (pick & 4) base = 0x5, n = 2;
    else if (pick & 8) base = 0x6;
    else if (pick & 0x10) base = 0x10007;
    else if (pick & 0x20) base = 0x20003;
    else if (pick & 0xe80) n = 0;
    else if (pick & 0x100) base = 0x20004;
    else if ((pick & 0x60000040) == 0x40) base = 0x20003;
    else n = 0;
    if (n > (uint32_t)max) n = (uint32_t)max;
    for (uint32_t k = 0; k < n; k++) out[k] = (base | id << 8) + (k << 16);
    once(&asked, "hid: the game asked for its six-axis sensor (first handle %#x)", n ? out[0] : 0);
    ret_x(c, n);
}

static const uint32_t *need_handle(const void *p, const char *fn) {
    if (!p) rt_fatal("nn::hid::detail::%s: null handle", fn);
    return p;
}

static int vibration_target(const uint32_t *h, const char *fn, int *player, int *side) {
    uint32_t v = *need_handle(h, fn);
    uint32_t st = v & 0xffu, id = (v >> 8) & 0xffu, dev = (v >> 16) & 0xffu;
    int p = npad_to_player(id);
    if (p < 0 || st < 3 || st > 7) return 0;
    int s = (st == 7) ? 1 : (dev < 2 ? (int)dev : -1);
    if (s < 0) return 0;
    if (p > 0 && joycon_player_style(p) == 0) return 0;
    if (player) *player = p;
    if (side) *side = s;
    return 1;
}

static int vibration_side(const uint32_t *h, const char *fn) {
    int p = -1, s = -1;
    if (vibration_target(h, fn, &p, &s)) return s;
    uint32_t v = *need_handle(h, fn);
    static volatile int32_t warned;
    once(&warned, "hid: vibration call for handle %#x, not a device of a connected controller: ignored", v);
    (void)pad_style_index;
    return -1;
}

static PlatMutex vib_lock = PLAT_MUTEX_INIT;
static VibrationValue vib_last[2] = {{0, 160, 0, 320}, {0, 160, 0, 320}};   // vib_lock
static uint64_t vib_at[2], sent_at;                                          // vib_lock
static uint16_t sent_low, sent_high;                                         // vib_lock
static int vib_allowed = 1;                                                  // vib_lock: PermitVibration

static float clamp01(float v) { return v > 0 ? (v < 1 ? v : 1) : 0; }   // NaN -> 0
// Share of a band at f Hz that goes to the light (high-frequency, right) motor: none at 160 Hz and below (the low band's
// neutral frequency), all at 320 Hz and above (the high band's), log scale between.
static float light_share(float f) { return f > 160 ? (f < 320 ? log2f(f / 160) : 1) : 0; }

// Switch HD rumble -> SDL_RumbleGamepad. Each side's two bands split by frequency between the heavy (low) and the light
// (high) motor (light_share) and add up as energy per motor (root of the summed squares); the stronger side drives each
// motor. A side the game has not updated for 250 ms is silent. Each send lasts 250 ms and is renewed every 50 ms while
// the game streams, so the pad stops by itself when the game stops sending (pause, freeze). Under vib_lock.
static void rumble_update(uint64_t now) {
    float heavy = 0, light = 0;
    rumble_cfg();
    for (int s = 0; s < 2 && vib_allowed; s++) {
        if (!vib_at[s] || now - vib_at[s] > RUMBLE_STALE_NS) continue;
        const VibrationValue *v = &vib_last[s];
        float al = clamp01(v->amp_low), ah = clamp01(v->amp_high), sl = light_share(v->freq_low), sh = light_share(v->freq_high);
        float h = sqrtf(al * (1 - sl) * al * (1 - sl) + ah * (1 - sh) * ah * (1 - sh));
        float l = sqrtf(al * sl * al * sl + ah * sh * ah * sh);
        if (h > heavy) heavy = h;
        if (l > light) light = l;
    }
    uint16_t lo = motor(clamp01(heavy)), hi = motor(clamp01(light));
    int changed = lo != sent_low || hi != sent_high, on = lo || hi;
    if (changed ? on && now - sent_at < RUMBLE_MIN_GAP_NS : !on || now - sent_at < RUMBLE_REFRESH_NS) return;
    input_rumble(lo, hi, on ? RUMBLE_MS : 0);
    sent_low = lo, sent_high = hi, sent_at = now;
}

static void play_for(int player, int side, const VibrationValue *v) {
    if (!v) rt_fatal("nn::hid::detail::SendVibrationValue: null value");
    if (side < 0 || player < 0) return;
    VibrationValue c = *v;
    if (!isfinite(c.amp_low) || !isfinite(c.amp_high) || !isfinite(c.freq_low) || !isfinite(c.freq_high)) c = vib_neutral;
    static volatile int32_t first, first_on;
    if (!plat_xchg32(&first, 1))
        rt_log("hid: first vibration value from the game: P%d %s %.2f@%.0f Hz %.2f@%.0f Hz", player + 1,
               side ? "right" : "left", (double)c.amp_low, (double)c.freq_low, (double)c.amp_high, (double)c.freq_high);
    if ((c.amp_low > 0.01f || c.amp_high > 0.01f) && !plat_xchg32(&first_on, 1))
        rt_log("hid: first non-zero vibration from the game: P%d %s %.2f@%.0f Hz %.2f@%.0f Hz", player + 1,
               side ? "right" : "left", (double)c.amp_low, (double)c.freq_low, (double)c.amp_high, (double)c.freq_high);
    uint64_t now = kern_now_ns();
    plat_mutex_lock(&vib_lock);
    if (vib_allowed) {
        rumble_cfg();
        joycon_send_rumble(player, side, c.amp_low * rumble_strength, c.freq_low,
                           c.amp_high * rumble_strength, c.freq_high);
    } else {
        joycon_send_rumble(player, side, 0.0f, 160.0f, 0.0f, 320.0f);
    }
    if (player == 0) {
        vib_last[side] = c;
        vib_at[side] = now;
        rumble_update(now);
    }
    plat_mutex_unlock(&vib_lock);
}

// Result detail::InitializeVibrationDevice(const VibrationDeviceHandle &)
static void ov_vib_init(Cpu *c) {
    static volatile int32_t logged;
    const uint32_t *h = arg_p(c, 0);
    if (vibration_side(h, "InitializeVibrationDevice") >= 0)
        once(&logged, "hid: the game initialized its vibration devices (handle %#x)", *h);
    ret_x(c, 0);
}

// Result detail::SendVibrationValue(const VibrationDeviceHandle &, const VibrationValue &)
static void ov_vib_send(Cpu *c) {
    int p = -1, s = -1;
    if (vibration_target(arg_p(c, 0), "SendVibrationValue", &p, &s))
        play_for(p, s, arg_p(c, 1));
    ret_x(c, 0);
}

// Result detail::SendVibrationValues(const VibrationDeviceHandle *, const VibrationValue *, int count)
static void ov_vib_send_many(Cpu *c) {
    const uint32_t *h = arg_p(c, 0);
    const VibrationValue *v = arg_p(c, 1);
    int n = (int)arg_x(c, 2);
    if (n < 0 || (n && (!h || !v))) rt_fatal("nn::hid::detail::SendVibrationValues: invalid batch (%d values)", n);
    for (int i = 0; i < n; i++) {
        int p = -1, s = -1;
        if (vibration_target(&h[i], "SendVibrationValues", &p, &s))
            play_for(p, s, &v[i]);
    }
    ret_x(c, 0);
}

// Result detail::GetActualVibrationValue(VibrationValue *out, const VibrationDeviceHandle &): the last value sent
static void ov_vib_actual(Cpu *c) {
    VibrationValue *out = arg_p(c, 0);
    if (!out) rt_fatal("nn::hid::detail::GetActualVibrationValue: null output");
    int side = vibration_side(arg_p(c, 1), "GetActualVibrationValue");
    VibrationValue v = vib_neutral;
    plat_mutex_lock_shared(&vib_lock);
    if (side >= 0) v = vib_last[side];
    plat_mutex_unlock_shared(&vib_lock);
    *out = v;
    ret_x(c, 0);
}

// Result detail::GetVibrationDeviceInfo(VibrationDeviceInfo *out, const VibrationDeviceHandle &)
static void ov_vib_info(Cpu *c) {
    VibrationDeviceInfo *out = arg_p(c, 0);
    if (!out) rt_fatal("nn::hid::detail::GetVibrationDeviceInfo: null output");
    int side = vibration_side(arg_p(c, 1), "GetVibrationDeviceInfo");
    *out = side >= 0 ? (VibrationDeviceInfo){1, (uint32_t)side + 1} : (VibrationDeviceInfo){0, 0};
    ret_x(c, 0);
}

// Result detail::IsVibrationDeviceMounted(bool *out, const VibrationDeviceHandle &)
static void ov_vib_mounted(Cpu *c) {
    uint8_t *out = arg_p(c, 0);
    if (!out) rt_fatal("nn::hid::detail::IsVibrationDeviceMounted: null output");
    *out = vibration_side(arg_p(c, 1), "IsVibrationDeviceMounted") >= 0;
    ret_x(c, 0);
}

// Result detail::IsVibrationPermitted(bool *out)
static void ov_vib_permitted(Cpu *c) {
    uint8_t *out = arg_p(c, 0);
    if (!out) rt_fatal("nn::hid::detail::IsVibrationPermitted: null output");
    plat_mutex_lock_shared(&vib_lock);
    *out = (uint8_t)vib_allowed;
    plat_mutex_unlock_shared(&vib_lock);
    ret_x(c, 0);
}

// Result detail::PermitVibration(bool): off silences the gamepad at once
static void ov_vib_permit(Cpu *c) {
    int on = (arg_x(c, 0) & 0xff) != 0;
    plat_mutex_lock(&vib_lock);
    vib_allowed = on;
    rumble_update(kern_now_ns());
    plat_mutex_unlock(&vib_lock);
    ret_x(c, 0);
}

static volatile int32_t six_running;   // StartSixAxisSensor .. StopSixAxisSensor

static int six_slot_dev(uint32_t h, int *slot, int *player, int *dev) {
    uint32_t st = h & 0xffu, id = (h >> 8) & 0xffu, raw_dev = (h >> 16) & 0xffu;
    int sl = id < 8 ? (int)id : id == 0x10u ? 8 : id == 0x20u ? 9 : -1;
    if (sl < 0 || st < 3 || st > 7 || raw_dev > 3) return 0;
    int p = npad_to_player(id);
    int d = (st == 7) ? 1 : (raw_dev == 2 ? 0 : raw_dev == 3 ? 1 : (int)(raw_dev & 1u));
    if (slot) *slot = sl;
    if (player) *player = p;
    if (dev) *dev = d;
    return 1;
}

static int six_axis_ours(const uint32_t *h, const char *fn) {
    uint32_t v = *need_handle(h, fn);
    int slot = -1, p = -1, dev = -1;
    if (six_slot_dev(v, &slot, &p, &dev) && p >= 0) {
        if (p == 0 || joycon_player_style(p) != 0) return 1;
    }
    static volatile int32_t warned;
    once(&warned, "hid: six-axis call for handle %#x, not a connected controller's sensor: reads at rest", v);
    return 0;
}

// Result StartSixAxisSensor(const SixAxisSensorHandle &)
static void ov_six_start(Cpu *c) {
    const uint32_t *h = arg_p(c, 0);
    if (six_axis_ours(h, "StartSixAxisSensor")) {
        static volatile int32_t logged;
        once(&logged, "hid: the game started the six-axis sensor (handle %#x)", *h);
        int slot, p, dev;
        if (six_slot_dev(*h, &slot, &p, &dev) && p >= 0) {
            joycon_restart_six_axis(p, dev);
            if (p == 0) kbm_motion_restart(dev);
        }
        motion_restart(motion_global());
        input_motion_start();
        plat_xchg32(&six_running, 1);
    }
    ret_x(c, 0);
}

// Result StopSixAxisSensor(const SixAxisSensorHandle &)
static void ov_six_stop(Cpu *c) {
    (void)c;
    ret_x(c, 0);
}

// All max entries, newest first.
static int six_fill(MotionState *s, int max, const uint32_t *h, const char *fn) {
    if (max <= 0) return 0;
    if (!s) rt_fatal("nn::hid::%s: null output", fn);
    int slot = -1, p = -1, dev = 0;
    int ok = six_axis_ours(h, fn) && six_slot_dev(*h, &slot, &p, &dev) && input_motion_enabled();
    uint64_t now = kern_now_ns();
    int live = 0;
    if (ok && p >= 0) {
        live = joycon_read_six_axis(p, dev, s, max, now);
        if (!live && p == 0) {
            int n = motion_read(motion_global(), s, max, now);
            if (n > 0) {
                for (int i = n; i < max; i++) s[i] = s[n - 1];
                live = 1;
            } else {
                live = kbm_motion_read(dev, s, max, now);
            }
        }
    }
    if (!live) {
        static const MotionState rest = {.acc = {0, 0, -1}, .dir = {1, 0, 0, 0, 1, 0, 0, 0, 1}};
        for (int i = 0; i < max; i++) s[i] = rest;
    }
    static volatile int32_t first_rest, first_live;
    if (!live && six_running) once(&first_rest, "hid: six-axis read with no motion samples: at rest (%u states)", (uint32_t)max);
    if (live) once(&first_live, "hid: first six-axis read with live motion (%u samples)", (uint32_t)max);
    return max;
}

// Result detail::GetSixAxisSensorStates(int *count, SixAxisSensorState *out, int max, const SixAxisSensorHandle &)
static void ov_six_states(Cpu *c) {
    int32_t *count = arg_p(c, 0);
    if (!count) rt_fatal("nn::hid::detail::GetSixAxisSensorStates: null count");
    *count = six_fill(arg_p(c, 1), (int)arg_x(c, 2), arg_p(c, 3), "detail::GetSixAxisSensorStates");
    ret_x(c, 0);
}

// int nn::hid::GetSixAxisSensorStates(SixAxisSensorState *out, int max, const SixAxisSensorHandle &)
static void ov_six_states_pub(Cpu *c) {
    int n = six_fill(arg_p(c, 0), (int)arg_x(c, 1), arg_p(c, 2), "GetSixAxisSensorStates");
    ret_x(c, (uint64_t)(uint32_t)n);
}

// Result detail::GetSixAxisSensorState(SixAxisSensorState *out, const SixAxisSensorHandle &)
static void ov_six_state(Cpu *c) {
    six_fill(arg_p(c, 0), 1, arg_p(c, 1), "GetSixAxisSensorState");
    ret_x(c, 0);
}

// ---- Additional public nn::hid APIs for NS Sports
typedef struct { float range, offset_min[3], offset_max[3], scale_min[9], scale_max[9]; } SixIcHalf;
typedef struct { SixIcHalf gyro, acc; } SixIcInformation;
_Static_assert(sizeof(SixIcInformation) == 0xc8, "SixAxisSensorIcInformation");

#define SIX_IC_SCALE_MIN {0.95f, -0.003f, -0.003f, -0.003f, 0.95f, -0.003f, -0.003f, -0.003f, 0.95f}
#define SIX_IC_SCALE_MAX {1.05f, 0.003f, 0.003f, 0.003f, 1.05f, 0.003f, 0.003f, 0.003f, 1.05f}
static const SixIcInformation SIX_IC_INFO = {
    .gyro = {2000.0f, {-10.0f, -10.0f, -10.0f}, {10.0f, 10.0f, 10.0f}, SIX_IC_SCALE_MIN, SIX_IC_SCALE_MAX},
    .acc = {8.0f, {-0.0612f, -0.0612f, -0.0612f}, {0.0612f, 0.0612f, 0.0612f}, SIX_IC_SCALE_MIN, SIX_IC_SCALE_MAX},
};

enum { SIX_CAL_SIZE = 0x744 };
static PlatMutex six_cal_lock = PLAT_MUTEX_INIT;
static uint8_t six_cal_buf[10][3][SIX_CAL_SIZE];
static uint8_t six_cal_set[10][3];
static uint32_t home_protect_mask;

static void ov_hid_noop(Cpu *c) { (void)c; }
static void ov_hid_zero(Cpu *c) { ret_x(c, 0); }

static void ov_npad_color1(Cpu *c) {
    uint32_t *out = arg_p(c, 0);
    uint32_t id = *need_id(c, 1, "GetNpadControllerColor");
    int p = npad_to_player(id);
    if (p < 0) { ret_x(c, RES_INVALID_NPAD); return; }
    uint32_t lb = 0, lbt = 0;
    joycon_player_colors(p, &lb, &lbt, NULL, NULL);
    if (out) { out[0] = lb; out[1] = lbt; }
    ret_x(c, 0);
}

static void ov_npad_color2(Cpu *c) {
    uint32_t *left = arg_p(c, 0), *right = arg_p(c, 1);
    uint32_t id = *need_id(c, 2, "GetNpadControllerColor");
    int p = npad_to_player(id);
    if (p < 0) { ret_x(c, RES_INVALID_NPAD); return; }
    uint32_t lb = 0, lbt = 0, rb = 0, rbt = 0;
    joycon_player_colors(p, &lb, &lbt, &rb, &rbt);
    if (left) { left[0] = lb; left[1] = lbt; }
    if (right) { right[0] = rb; right[1] = rbt; }
    ret_x(c, 0);
}

static void ov_six_ic_info(Cpu *c) {
    void *out = arg_p(c, 0);
    const uint32_t *h = arg_p(c, 1);
    int slot, p, dev;
    if (!out || !h || !six_slot_dev(*h, &slot, &p, &dev)) { ret_x(c, RES_INVALID_NPAD); return; }
    memcpy(out, &SIX_IC_INFO, sizeof SIX_IC_INFO);
    ret_x(c, 0);
}

static void ov_six_cal_load(Cpu *c) {
    void *out = arg_p(c, 0);
    const uint32_t *h = arg_p(c, 1);
    int slot, p, dev;
    if (!out || !h || !six_slot_dev(*h, &slot, &p, &dev)) { ret_x(c, RES_INVALID_NPAD); return; }
    int d = (dev == 2 ? 0 : dev);
    plat_mutex_lock(&six_cal_lock);
    if (six_cal_set[slot][d]) memcpy(out, six_cal_buf[slot][d], SIX_CAL_SIZE);
    else memset(out, 0, SIX_CAL_SIZE);
    plat_mutex_unlock(&six_cal_lock);
    ret_x(c, 0);
}

static void ov_six_cal_store(Cpu *c) {
    const uint32_t *h = arg_p(c, 0);
    const void *in = arg_p(c, 1);
    int slot, p, dev;
    if (!in || !h || !six_slot_dev(*h, &slot, &p, &dev)) { ret_x(c, RES_INVALID_NPAD); return; }
    int d = (dev == 2 ? 0 : dev);
    plat_mutex_lock(&six_cal_lock);
    memcpy(six_cal_buf[slot][d], in, SIX_CAL_SIZE);
    six_cal_set[slot][d] = 1;
    plat_mutex_unlock(&six_cal_lock);
    ret_x(c, 0);
}

static void ov_home_protect_set(Cpu *c) {
    const uint32_t *id = arg_p(c, 0);
    int on = (arg_x(c, 1) & 0xffu) != 0;
    if (!id) return;
    uint32_t bit = id_bit(*id);
    plat_mutex_lock(&lock);
    if (on) home_protect_mask |= bit;
    else home_protect_mask &= ~bit;
    plat_mutex_unlock(&lock);
}

static void ov_home_protect_get(Cpu *c) {
    const uint32_t *id = arg_p(c, 0);
    plat_mutex_lock_shared(&lock);
    uint32_t m = home_protect_mask;
    plat_mutex_unlock_shared(&lock);
    ret_x(c, id && (m & id_bit(*id)) ? 1 : 0);
}

static uint64_t six_passthrough_mask;

// void / Result EnableSixAxisSensorPassthrough(const SixAxisSensorHandle &, bool enable)
static void ov_six_set_passthrough(Cpu *c) {
    const uint32_t *h = arg_p(c, 0);
    int on = (arg_x(c, 1) & 1) != 0;
    int slot = -1, p = -1, dev = 0;
    if (h && six_slot_dev(*h, &slot, &p, &dev) && slot >= 0 && slot < 16 && (dev == 0 || dev == 1)) {
        uint64_t bit = 1ull << (slot * 2 + dev);
        plat_mutex_lock(&lock);
        if (on) six_passthrough_mask |= bit;
        else six_passthrough_mask &= ~bit;
        plat_mutex_unlock(&lock);
    }
    ret_x(c, 0);
}

// bool IsSixAxisSensorPassthroughEnabled(const SixAxisSensorHandle &)
static void ov_six_is_passthrough(Cpu *c) {
    const uint32_t *h = arg_p(c, 0);
    int slot = -1, p = -1, dev = 0, on = 0;
    if (h && six_slot_dev(*h, &slot, &p, &dev) && slot >= 0 && slot < 16 && (dev == 0 || dev == 1)) {
        uint64_t bit = 1ull << (slot * 2 + dev);
        plat_mutex_lock_shared(&lock);
        on = (six_passthrough_mask & bit) != 0;
        plat_mutex_unlock_shared(&lock);
    }
    ret_x(c, (uint64_t)on);
}

HOST_OVERRIDE("_ZN2nn3hid14InitializeNpadEv", ov_initialize_npad);
HOST_OVERRIDE("_ZN2nn3hid22SetSupportedNpadIdTypeEPKjm", ov_supported_ids);
HOST_OVERRIDE("_ZN2nn3hid24SetSupportedNpadStyleSetENS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE", ov_supported_styles);
HOST_OVERRIDE("_ZN2nn3hid24GetSupportedNpadStyleSetEv", ov_get_supported_styles);
HOST_OVERRIDE("_ZN2nn3hid15GetNpadStyleSetERKj", ov_style_set);
HOST_OVERRIDE("_ZN2nn3hid13GetNpadStatesEPNS0_16NpadFullKeyStateEiRKj", ov_states_full_key);
HOST_OVERRIDE("_ZN2nn3hid13GetNpadStatesEPNS0_16NpadJoyDualStateEiRKj", ov_states_joy_dual);
HOST_OVERRIDE("_ZN2nn3hid13GetNpadStatesEPNS0_16NpadJoyLeftStateEiRKj", ov_states_joy_left);
HOST_OVERRIDE("_ZN2nn3hid13GetNpadStatesEPNS0_17NpadJoyRightStateEiRKj", ov_states_joy_right);
HOST_OVERRIDE("_ZN2nn3hid13GetNpadStatesEPNS0_17NpadHandheldStateEiRKj", ov_states_handheld);
HOST_OVERRIDE("_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKj", ov_state_full_key);
HOST_OVERRIDE("_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj", ov_state_joy_dual);
HOST_OVERRIDE("_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyLeftStateERKj", ov_state_joy_left);
HOST_OVERRIDE("_ZN2nn3hid12GetNpadStateEPNS0_17NpadJoyRightStateERKj", ov_state_joy_right);
HOST_OVERRIDE("_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj", ov_state_handheld);
HOST_OVERRIDE("_ZN2nn3hid18SetNpadJoyHoldTypeENS0_15NpadJoyHoldTypeE", ov_hold_type);
HOST_OVERRIDE("_ZN2nn3hid18GetNpadJoyHoldTypeEv", ov_get_hold_type);
HOST_OVERRIDE("_ZN2nn3hid20GetNpadJoyAssignmentERKj", ov_get_joy_assign);
HOST_OVERRIDE("_ZN2nn3hid30SetNpadJoyAssignmentModeSingleERKj", ov_set_joy_single);
HOST_OVERRIDE("_ZN2nn3hid28SetNpadJoyAssignmentModeDualERKj", ov_set_joy_dual);
HOST_OVERRIDE("_ZN2nn3hid18SwapNpadAssignmentERKjS2_", ov_swap_npad);
HOST_OVERRIDE("_ZN2nn3hid14DisconnectNpadERKj", ov_disconnect);
HOST_OVERRIDE("_ZN2nn3hid21ShowControllerSupportEPNS0_27ControllerSupportResultInfoERKNS0_20ControllerSupportArgE",
              ov_controller_support);
HOST_OVERRIDE("_ZN2nn3hid21ShowControllerSupportERKNS0_20ControllerSupportArgE", ov_controller_support_nores);
HOST_OVERRIDE("_ZN2nn3hid28ShowControllerFirmwareUpdateERKNS0_27ControllerFirmwareUpdateArgE", ov_hid_zero);
HOST_OVERRIDE("_ZN2nn3hid22GetNpadControllerColorEPNS0_19NpadControllerColorERKj", ov_npad_color1);
HOST_OVERRIDE("_ZN2nn3hid22GetNpadControllerColorEPNS0_19NpadControllerColorES2_RKj", ov_npad_color2);
HOST_OVERRIDE("_ZN2nn3hid15InitializeMouseEv", ov_initialize_mouse);
HOST_OVERRIDE("_ZN2nn3hid18InitializeKeyboardEv", ov_initialize_keyboard);
HOST_OVERRIDE("_ZN2nn3hid14GetMouseStatesEPNS0_10MouseStateEi", ov_mouse_states);
HOST_OVERRIDE("_ZN2nn3hid16GetKeyboardStateEPNS0_13KeyboardStateE", ov_keyboard_state);
HOST_OVERRIDE("_ZN2nn3hid25GetVibrationDeviceHandlesEPNS0_21VibrationDeviceHandleEiRKjNS_4util10BitFlagSetILi32ENS0_"
              "12NpadStyleTagEEE", ov_vibration_handles);
HOST_OVERRIDE("_ZN2nn3hid23GetSixAxisSensorHandlesEPNS0_19SixAxisSensorHandleEiRKjNS_4util10BitFlagSetILi32ENS0_"
              "12NpadStyleTagEEE", ov_six_axis_handles);
HOST_OVERRIDE("_ZN2nn3hid25InitializeVibrationDeviceERKNS0_21VibrationDeviceHandleE", ov_vib_init);
HOST_OVERRIDE("_ZN2nn3hid18SendVibrationValueERKNS0_21VibrationDeviceHandleERKNS0_14VibrationValueE", ov_vib_send);
HOST_OVERRIDE("_ZN2nn3hid6detail25InitializeVibrationDeviceERKNS0_21VibrationDeviceHandleE", ov_vib_init);
HOST_OVERRIDE("_ZN2nn3hid6detail18SendVibrationValueERKNS0_21VibrationDeviceHandleERKNS0_14VibrationValueE", ov_vib_send);
HOST_OVERRIDE("_ZN2nn3hid6detail19SendVibrationValuesEPKNS0_21VibrationDeviceHandleEPKNS0_14VibrationValueEi",
              ov_vib_send_many);
HOST_OVERRIDE("_ZN2nn3hid6detail23GetActualVibrationValueEPNS0_14VibrationValueERKNS0_21VibrationDeviceHandleE",
              ov_vib_actual);
HOST_OVERRIDE("_ZN2nn3hid6detail22GetVibrationDeviceInfoEPNS0_19VibrationDeviceInfoERKNS0_21VibrationDeviceHandleE",
              ov_vib_info);
HOST_OVERRIDE("_ZN2nn3hid6detail24IsVibrationDeviceMountedEPbRKNS0_21VibrationDeviceHandleE", ov_vib_mounted);
HOST_OVERRIDE("_ZN2nn3hid6detail20IsVibrationPermittedEPb", ov_vib_permitted);
HOST_OVERRIDE("_ZN2nn3hid6detail15PermitVibrationEb", ov_vib_permit);
HOST_OVERRIDE("_ZN2nn3hid18StartSixAxisSensorERKNS0_19SixAxisSensorHandleE", ov_six_start);
HOST_OVERRIDE("_ZN2nn3hid17StopSixAxisSensorERKNS0_19SixAxisSensorHandleE", ov_six_stop);
HOST_OVERRIDE("_ZN2nn3hid22GetSixAxisSensorStatesEPNS0_18SixAxisSensorStateEiRKNS0_19SixAxisSensorHandleE",
              ov_six_states_pub);
HOST_OVERRIDE("_ZN2nn3hid25SetGyroscopeZeroDriftModeERKNS0_19SixAxisSensorHandleERKNS0_22GyroscopeZeroDriftModeE",
              ov_hid_noop);
HOST_OVERRIDE("_ZN2nn3hid29GetSixAxisSensorIcInformationEPNS0_26SixAxisSensorIcInformationERKNS0_19SixAxisSensorHandleE",
              ov_six_ic_info);
HOST_OVERRIDE("_ZN2nn3hid34IsSixAxisSensorDeviceNewlyAssignedERKNS0_19SixAxisSensorHandleE", ov_hid_zero);
HOST_OVERRIDE("_ZN2nn3hid39ResetIsSixAxisSensorDeviceNewlyAssignedERKNS0_19SixAxisSensorHandleE", ov_hid_noop);
HOST_OVERRIDE("_ZN2nn3hid37LoadSixAxisSensorCalibrationParameterEPNS0_33SixAxisSensorCalibrationParameterERKNS0_19SixAxisSensorHandleE",
              ov_six_cal_load);
HOST_OVERRIDE("_ZN2nn3hid38StoreSixAxisSensorCalibrationParameterERKNS0_19SixAxisSensorHandleERKNS0_33SixAxisSensorCalibrationParameterE",
              ov_six_cal_store);
HOST_OVERRIDE("_ZN2nn3hid41IsFirmwareUpdateAvailableForSixAxisSensorERKNS0_19SixAxisSensorHandleE", ov_hid_zero);
HOST_OVERRIDE("_ZN2nn3hid41EnableUnintendedHomeButtonInputProtectionERKjb", ov_home_protect_set);
HOST_OVERRIDE("_ZN2nn3hid44IsUnintendedHomeButtonInputProtectionEnabledERKj", ov_home_protect_get);
HOST_OVERRIDE("_ZN2nn3hid30EnableSixAxisSensorPassthroughERKNS0_19SixAxisSensorHandleEb", ov_six_set_passthrough);
HOST_OVERRIDE("_ZN2nn3hid6detail30EnableSixAxisSensorPassthroughERKNS0_19SixAxisSensorHandleEb", ov_six_set_passthrough);
HOST_OVERRIDE("_ZN2nn3hid33IsSixAxisSensorPassthroughEnabledERKNS0_19SixAxisSensorHandleE", ov_six_is_passthrough);
HOST_OVERRIDE("_ZN2nn3hid6detail33IsSixAxisSensorPassthroughEnabledERKNS0_19SixAxisSensorHandleE", ov_six_is_passthrough);
HOST_OVERRIDE("_ZN2nn3hid6detail18StartSixAxisSensorERKNS0_19SixAxisSensorHandleE", ov_six_start);
HOST_OVERRIDE("_ZN2nn3hid6detail17StopSixAxisSensorERKNS0_19SixAxisSensorHandleE", ov_six_stop);
HOST_OVERRIDE("_ZN2nn3hid6detail22GetSixAxisSensorStatesEPiPNS0_18SixAxisSensorStateEiRKNS0_19SixAxisSensorHandleE",
              ov_six_states);
HOST_OVERRIDE("_ZN2nn3hid6detail21GetSixAxisSensorStateEPNS0_18SixAxisSensorStateERKNS0_19SixAxisSensorHandleE",
              ov_six_state);
