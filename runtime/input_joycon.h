// Bluetooth Joy-Con (L/R) & Pro Controller HID + KBM motion interfaces for NS Sports.
#pragma once
#include <stdint.h>
#include "input.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    JC_MAX_PLAYERS = 4,
    JC_MAX_SLOTS = 8,
};

enum {
    JC_KIND_NONE = 0,
    JC_KIND_LEFT = 1,    // 0x057E:0x2006
    JC_KIND_RIGHT = 2,   // 0x057E:0x2007
    JC_KIND_PRO = 3,     // 0x057E:0x2009
};

enum {
    JC_MODE_AUTO = 0,
    JC_MODE_DUAL = 1,
    JC_MODE_SINGLE = 2,
};

typedef struct {
    int connected;
    int kind;              // JC_KIND_*
    int player;            // 0..3 or -1
    int battery;           // 0..100 percent
    int imu_ok;            // streaming IMU samples
    int spi_cal_ok;        // factory/user SPI cal loaded
    uint16_t pid;
    uint32_t body_color;   // NpadControllerColor RGBA8888 (R | G<<8 | B<<16 | A<<24)
    uint32_t btn_color;    // NpadControllerColor sub RGBA8888
    float acc_g[3];        // latest Switch-frame accel (G)
    float gyro_deg[3];     // latest Switch-frame gyro (deg/s)
    char serial[32];
} JoyconSlotStatus;

void joycon_init(void);
void joycon_shutdown(void);
int joycon_driver_mode(void);        // 0=off, 1=hidapi, 2=sdl
int joycon_pairing_mode(void);       // JC_MODE_AUTO / DUAL / SINGLE
void joycon_set_game_single_hint(int single);
void joycon_rescan(void);
void joycon_recenter_all(void);
void joycon_calibrate_player(int player);

int joycon_get_slot_status(int slot, JoyconSlotStatus *out);
int joycon_connected_count(void);

// Player-level Joy-Con/Pro assignment (0 = none attached on player).
uint32_t joycon_player_style(int player);
int joycon_player_read(int player, uint32_t style_mask, uint64_t *buttons,
                       int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry, int hold_type);
void joycon_player_colors(int player, uint32_t *left_body, uint32_t *left_btn,
                          uint32_t *right_body, uint32_t *right_btn);

// Six-axis sensor read for physical Joy-Con/Pro: dev 0 = left/pro, dev 1 = right.
// Returns 1 if a live physical Joy-Con/Pro IMU owns (player, dev) and filled out[], else 0.
int joycon_read_six_axis(int player, int dev, MotionState *out, int max, uint64_t now_ns);
void joycon_restart_six_axis(int player, int dev);

// HD rumble: dev 0 = left motor/controller, dev 1 = right motor/controller.
void joycon_send_rumble(int player, int dev, float amp_low, float freq_low, float amp_high, float freq_high);

// ---- input_kbm_motion.c (Keyboard + Mouse motion + NSS_KEYS scripted actions for Player 1 fallback)
void kbm_motion_init(void);
void kbm_motion_tick(uint64_t now_ns);
void kbm_motion_recenter(void);
void kbm_motion_poll(uint64_t *buttons, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry);
int kbm_motion_read(int dev, MotionState *out, int max, uint64_t now_ns);
void kbm_motion_restart(int dev);
int kbm_motion_script_active(void);

#ifdef __cplusplus
}
#endif
