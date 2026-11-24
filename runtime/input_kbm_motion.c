// Keyboard + mouse synthetic Joy-Con motion & scripted input (NSS_KEYS) fallback for Player 1.
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "input.h"
#include "input_joycon.h"
#include "input_map.h"
#include "input_wsys.h"
#include "platform/platform.h"
#include "ui/menu.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define KBM_STEP_MAX 256
#define KBM_STEP_NS  5000000ull   // 200 Hz synthetic IMU step

enum {
    BTN_LSL = 1ull << 24,
    BTN_LSR = 1ull << 25,
    BTN_RSL = 1ull << 26,
    BTN_RSR = 1ull << 27,
    MOTION_SWING_R = 1u << 0,
    MOTION_SWING_L = 1u << 1,
    MOTION_SWING_UP = 1u << 2,
    MOTION_SHAKE = 1u << 3,
    MOTION_BOWL = 1u << 4,
};

typedef struct {
    double at, hold;
    uint64_t btn;
    int32_t lx, ly, rx, ry;
    uint32_t motion_flags;
} ScriptStep;

static PlatMutex kbm_lock = PLAT_MUTEX_INIT;
static MotionFusion *kbm_fusion[2];
static uint64_t kbm_last_ns, kbm_last_sample_ns;
static int kbm_inited, kbm_script_count;
static ScriptStep kbm_steps[KBM_STEP_MAX];

static float ptr_last_x = 0.5f, ptr_last_y = 0.5f;
static int ptr_have;
static float kbm_pitch, kbm_yaw;
static uint64_t swing_until_ns;
static uint32_t swing_kind;
static int prev_lmb, prev_rmb, prev_space, prev_e, prev_q, prev_r;

static int eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static void parse_script_token(const char *tok, ScriptStep *st) {
    if (!tok || !*tok) return;
    if ((tok[0] == 'L' || tok[0] == 'R' || tok[0] == 'l' || tok[0] == 'r') && strchr(tok + 1, '/')) {
        char *slash = NULL;
        float x = strtof(tok + 1, &slash);
        if (slash && *slash == '/') {
            float y = strtof(slash + 1, NULL);
            if (x > 1.0f) x = 1.0f; else if (x < -1.0f) x = -1.0f;
            if (y > 1.0f) y = 1.0f; else if (y < -1.0f) y = -1.0f;
            int32_t ix = (int32_t)lrintf(x * 32767.0f), iy = (int32_t)lrintf(y * 32767.0f);
            if (tok[0] == 'L' || tok[0] == 'l') { st->lx = ix; st->ly = iy; }
            else { st->rx = ix; st->ry = iy; }
            return;
        }
    }
    if (eq_ci(tok, "A")) st->btn |= 1ull << B_A;
    else if (eq_ci(tok, "B")) st->btn |= 1ull << B_B;
    else if (eq_ci(tok, "X")) st->btn |= 1ull << B_X;
    else if (eq_ci(tok, "Y")) st->btn |= 1ull << B_Y;
    else if (eq_ci(tok, "LS")) st->btn |= 1ull << B_STICK_L;
    else if (eq_ci(tok, "RS")) st->btn |= 1ull << B_STICK_R;
    else if (eq_ci(tok, "L")) st->btn |= 1ull << B_L;
    else if (eq_ci(tok, "R")) st->btn |= 1ull << B_R;
    else if (eq_ci(tok, "ZL")) st->btn |= 1ull << B_ZL;
    else if (eq_ci(tok, "ZR")) st->btn |= 1ull << B_ZR;
    else if (eq_ci(tok, "PLUS")) st->btn |= 1ull << B_PLUS;
    else if (eq_ci(tok, "MINUS")) st->btn |= 1ull << B_MINUS;
    else if (eq_ci(tok, "LEFT")) st->btn |= 1ull << B_LEFT;
    else if (eq_ci(tok, "UP")) st->btn |= 1ull << B_UP;
    else if (eq_ci(tok, "RIGHT")) st->btn |= 1ull << B_RIGHT;
    else if (eq_ci(tok, "DOWN")) st->btn |= 1ull << B_DOWN;
    else if (eq_ci(tok, "LSL")) st->btn |= BTN_LSL;
    else if (eq_ci(tok, "LSR")) st->btn |= BTN_LSR;
    else if (eq_ci(tok, "RSL")) st->btn |= BTN_RSL;
    else if (eq_ci(tok, "RSR")) st->btn |= BTN_RSR;
    else if (eq_ci(tok, "SL")) st->btn |= BTN_LSL | BTN_RSL;
    else if (eq_ci(tok, "SR")) st->btn |= BTN_LSR | BTN_RSR;
    else if (eq_ci(tok, "SWING_R")) st->motion_flags |= MOTION_SWING_R;
    else if (eq_ci(tok, "SWING_L")) st->motion_flags |= MOTION_SWING_L;
    else if (eq_ci(tok, "SWING_UP")) st->motion_flags |= MOTION_SWING_UP;
    else if (eq_ci(tok, "SHAKE")) st->motion_flags |= MOTION_SHAKE;
    else if (eq_ci(tok, "BOWL_RELEASE")) st->motion_flags |= MOTION_BOWL;
}

static void parse_script(void) {
    const char *raw = cfg_get("TOTK_KEYS");
    kbm_script_count = 0;
    if (!raw || !*raw) return;
    char *copy = strdup(raw);
    if (!copy) return;
    char *cur = copy;
    while (cur && *cur && kbm_script_count < KBM_STEP_MAX) {
        char *comma = strchr(cur, ',');
        if (comma) *comma = 0;
        while (*cur && isspace((unsigned char)*cur)) cur++;
        char *colon = strchr(cur, ':');
        if (colon) {
            *colon = 0;
            double at = atof(cur);
            char *spec = colon + 1;
            double hold = 0.18;
            char *tilde = strchr(spec, '~');
            if (tilde) {
                *tilde = 0;
                hold = atof(tilde + 1);
                if (hold <= 0.01) hold = 0.05;
            }
            ScriptStep st = {.at = at, .hold = hold};
            char *p = spec;
            while (p && *p) {
                char *plus = strchr(p, '+');
                if (plus) *plus = 0;
                while (*p && isspace((unsigned char)*p)) p++;
                char *end = p + strlen(p);
                while (end > p && isspace((unsigned char)end[-1])) *--end = 0;
                parse_script_token(p, &st);
                p = plus ? plus + 1 : NULL;
            }
            kbm_steps[kbm_script_count++] = st;
        }
        cur = comma ? comma + 1 : NULL;
    }
    free(copy);
    if (kbm_script_count) rt_log("kbm_motion: loaded %d scripted NSS_KEYS steps", kbm_script_count);
}

static void eval_script_locked(uint64_t now_ns, uint64_t *btn, int32_t *lx, int32_t *ly,
                               int32_t *rx, int32_t *ry, uint32_t *mflags, double *m_phase) {
    *btn = 0;
    *lx = *ly = *rx = *ry = 0;
    *mflags = 0;
    *m_phase = 0.0;
    if (!kbm_script_count) return;
    uint64_t t0 = rt_guest_start_ns ? rt_guest_start_ns : now_ns;
    double elapsed = now_ns >= t0 ? (double)(now_ns - t0) / 1e9 : 0.0;
    for (int i = 0; i < kbm_script_count; i++) {
        const ScriptStep *s = &kbm_steps[i];
        if (elapsed >= s->at && elapsed < s->at + s->hold) {
            *btn |= s->btn;
            if (s->lx || s->ly) { *lx = s->lx; *ly = s->ly; }
            if (s->rx || s->ry) { *rx = s->rx; *ry = s->ry; }
            *mflags |= s->motion_flags;
            *m_phase = (elapsed - s->at) / s->hold;
        }
    }
}

static void init_locked(uint64_t now_ns) {
    if (kbm_inited) return;
    kbm_inited = 1;
    for (int d = 0; d < 2; d++) kbm_fusion[d] = motion_new();
    kbm_last_ns = now_ns;
    kbm_last_sample_ns = now_ns;
    parse_script();
}

void kbm_motion_init(void) {
    uint64_t now = kern_now_ns();
    plat_mutex_lock(&kbm_lock);
    init_locked(now);
    plat_mutex_unlock(&kbm_lock);
}

int kbm_motion_script_active(void) { return kbm_script_count > 0; }

void kbm_motion_recenter(void) {
    plat_mutex_lock(&kbm_lock);
    kbm_pitch = 0.0f;
    kbm_yaw = 0.0f;
    ptr_have = 0;
    swing_until_ns = 0;
    for (int d = 0; d < 2; d++)
        if (kbm_fusion[d]) motion_restart(kbm_fusion[d]);
    plat_mutex_unlock(&kbm_lock);
}

static void synthesize_sample(double phase, uint32_t mflags, float pitch, float yaw,
                              float dyaw_dt, float dpitch_dt, double acc[3], double rot[3]) {
    /* Resting upright-tilted pose: base gravity aligned with (0, 0, -1) pitched gently by kbm_pitch */
    acc[0] = (double)(-sinf(yaw) * 0.25f);
    acc[1] = (double)(-sinf(pitch));
    acc[2] = (double)(-cosf(pitch));
    double n = sqrt(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
    if (n > 1e-4) {
        acc[0] /= n;
        acc[1] /= n;
        acc[2] /= n;
    } else {
        acc[0] = 0.0; acc[1] = 0.0; acc[2] = -1.0;
    }
    rot[0] = (double)dpitch_dt;
    rot[1] = 0.0;
    rot[2] = (double)dyaw_dt;

    if (mflags & MOTION_SWING_R) {
        double s = sin(phase * M_PI);
        rot[2] += 11.5 * s;
        rot[0] += 4.5 * s;
        acc[0] += 2.6 * s;
        acc[1] += 1.4 * s;
    }
    if (mflags & MOTION_SWING_L) {
        double s = sin(phase * M_PI);
        rot[2] -= 11.5 * s;
        rot[0] += 4.0 * s;
        acc[0] -= 2.6 * s;
        acc[1] += 1.4 * s;
    }
    if (mflags & MOTION_SWING_UP) {
        double s = sin(phase * M_PI);
        rot[0] -= 12.0 * s;
        acc[1] += 2.8 * s;
        acc[2] += 1.2 * s;
    }
    if (mflags & MOTION_SHAKE) {
        double s = sin(phase * 4.0 * M_PI);
        acc[0] += 2.4 * s;
        rot[2] += 7.5 * s;
    }
    if (mflags & MOTION_BOWL) {
        /* Backswing then crisp forward release arc */
        double s = phase < 0.35 ? -sin(phase / 0.35 * M_PI) * 0.6 : sin((phase - 0.35) / 0.65 * M_PI);
        rot[0] += 10.5 * s;
        acc[1] += 2.2 * s;
        acc[2] += (phase >= 0.35 ? 1.5 * s : -0.6);
    }
}

void kbm_motion_tick(uint64_t now_ns) {
    plat_mutex_lock(&kbm_lock);
    init_locked(now_ns);
    if (now_ns <= kbm_last_ns) {
        plat_mutex_unlock(&kbm_lock);
        return;
    }
    uint64_t dt_ns = now_ns - kbm_last_ns;
    if (dt_ns > 100000000ull) dt_ns = 100000000ull;
    double dt_s = (double)dt_ns / 1e9;
    kbm_last_ns = now_ns;

    float sens = 1.0f;
    const char *sv = cfg_get("TOTK_KBM_MOTION_SENS");
    if (sv && *sv) {
        char *end = NULL;
        float v = strtof(sv, &end);
        if (end != sv && isfinite(v)) sens = v < 0.1f ? 0.1f : v > 5.0f ? 5.0f : v;
    }
    float dyaw_dt = 0.0f, dpitch_dt = 0.0f;

    if (ikb_foreground() && !menu_is_open()) {
        int lmb = ikb_down(KEY_MOUSE_L);
        int rmb = ikb_down(KEY_MOUSE_R);
        int spc = ikb_down(KEY_SPACE);
        int ke = ikb_down(KEY_E);
        int kq = ikb_down(KEY_Q);
        int kr = ikb_down(KEY_R);

        if ((kr && !prev_r)) {
            kbm_pitch = 0.0f;
            kbm_yaw = 0.0f;
            ptr_have = 0;
            swing_until_ns = 0;
            for (int d = 0; d < 2; d++)
                if (kbm_fusion[d]) motion_restart(kbm_fusion[d]);
        }
        if ((lmb && !prev_lmb) || (ke && !prev_e)) {
            swing_kind = MOTION_SWING_R;
            swing_until_ns = now_ns + 220000000ull;
        } else if ((rmb && !prev_rmb) || (kq && !prev_q)) {
            swing_kind = MOTION_SWING_L;
            swing_until_ns = now_ns + 220000000ull;
        } else if (spc && !prev_space) {
            swing_kind = MOTION_SWING_UP;
            swing_until_ns = now_ns + 220000000ull;
        }
        prev_lmb = lmb;
        prev_rmb = rmb;
        prev_space = spc;
        prev_e = ke;
        prev_q = kq;
        prev_r = kr;

        float nx = 0.5f, ny = 0.5f;
        if (ikb_pointer(&nx, &ny)) {
            if (ptr_have) {
                float dx = (nx - ptr_last_x) * sens;
                float dy = (ny - ptr_last_y) * sens;
                kbm_yaw -= dx * 1.4f;
                kbm_pitch -= dy * 1.1f;
                if (kbm_yaw > 0.9f) kbm_yaw = 0.9f; else if (kbm_yaw < -0.9f) kbm_yaw = -0.9f;
                if (kbm_pitch > 0.9f) kbm_pitch = 0.9f; else if (kbm_pitch < -0.9f) kbm_pitch = -0.9f;
                if (dt_s > 1e-4) {
                    dyaw_dt = -(dx * 1.4f) / (float)dt_s;
                    dpitch_dt = -(dy * 1.1f) / (float)dt_s;
                }
            }
            ptr_last_x = nx;
            ptr_last_y = ny;
            ptr_have = 1;
        } else {
            ptr_have = 0;
        }
    } else {
        ptr_have = 0;
    }

    uint64_t sb;
    int32_t slx, sly, srx, sry;
    uint32_t smf = 0;
    double sphase = 0.0;
    eval_script_locked(now_ns, &sb, &slx, &sly, &srx, &sry, &smf, &sphase);

    uint32_t active_m = smf;
    double phase = sphase;
    if (!active_m && swing_until_ns > now_ns) {
        active_m = swing_kind;
        phase = 1.0 - (double)(swing_until_ns - now_ns) / 220000000.0;
        if (phase < 0.0) phase = 0.0; else if (phase > 1.0) phase = 1.0;
    }

    while (kbm_last_sample_ns + KBM_STEP_NS <= now_ns) {
        kbm_last_sample_ns += KBM_STEP_NS;
        double acc[3], rot[3];
        synthesize_sample(phase, active_m, kbm_pitch, kbm_yaw, dyaw_dt, dpitch_dt, acc, rot);
        for (int d = 0; d < 2; d++)
            if (kbm_fusion[d]) motion_feed(kbm_fusion[d], acc, rot, kbm_last_sample_ns);
    }
    if (kbm_last_sample_ns < now_ns) {
        kbm_last_sample_ns = now_ns;
        double acc[3], rot[3];
        synthesize_sample(phase, active_m, kbm_pitch, kbm_yaw, dyaw_dt, dpitch_dt, acc, rot);
        for (int d = 0; d < 2; d++)
            if (kbm_fusion[d]) motion_feed(kbm_fusion[d], acc, rot, now_ns);
    }
    plat_mutex_unlock(&kbm_lock);
}

void kbm_motion_poll(uint64_t *buttons, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry) {
    uint64_t now = kern_now_ns();
    plat_mutex_lock(&kbm_lock);
    init_locked(now);
    uint64_t sb = 0;
    int32_t slx = 0, sly = 0, srx = 0, sry = 0;
    uint32_t smf = 0;
    double sphase = 0.0;
    eval_script_locked(now, &sb, &slx, &sly, &srx, &sry, &smf, &sphase);
    plat_mutex_unlock(&kbm_lock);

    *buttons |= sb;
    if (slx || sly) { *lx = slx; *ly = sly; }
    if (srx || sry) { *rx = srx; *ry = sry; }

    /* Optional rail buttons / confirm shortcuts when playing on KBM */
    if (ikb_foreground() && !menu_is_open()) {
        if (ikb_down(KEY_Z)) *buttons |= BTN_LSL | BTN_RSL;
        if (ikb_down(KEY_C)) *buttons |= BTN_LSR | BTN_RSR;
        if (ikb_down(KEY_ENTER)) *buttons |= (1ull << B_A);
        if (ikb_down(KEY_BACKSPACE)) *buttons |= (1ull << B_B);
    }
}

int kbm_motion_read(int dev, MotionState *out, int max, uint64_t now_ns) {
    kbm_motion_tick(now_ns);
    int d = (dev == 1) ? 1 : 0;
    plat_mutex_lock(&kbm_lock);
    MotionFusion *f = kbm_fusion[d];
    plat_mutex_unlock(&kbm_lock);
    if (!f) return 0;
    int n = motion_read(f, out, max, now_ns);
    if (n <= 0) return 0;
    for (int i = n; i < max; i++) out[i] = out[n - 1];
    return 1;
}

void kbm_motion_restart(int dev) {
    int d = (dev == 1) ? 1 : 0;
    plat_mutex_lock(&kbm_lock);
    if (kbm_fusion[d]) motion_restart(kbm_fusion[d]);
    plat_mutex_unlock(&kbm_lock);
}
