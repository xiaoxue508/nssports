// Clean no-op stubs for TotK-only features omitted from Nintendo Switch Sports Recomp
// (DLAA/FSR3/FG, HDR/color grading/contact shadows, resolution/freecam/quicksave/tweaks,
// mods/cheats/restbl/amiibo/nfp, discord/playtime, plugins, and pruned probes).
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "amiibo.h"
#include "cfg.h"
#include "cheats.h"
#include "gameplay_tweaks.h"
#include "gfx.h"
#include "host.h"
#include "ov.h"
#include "input.h"
#include "mods.h"
#include "nfp.h"
#include "restbl.h"
#include "ui/amiibo_page.h"
#include "vk.h"
#include "vk_dlaa.h"
#include "vk_exec.h"
#include "vk_fg.h"
#include "vk_fsr3.h"

// ---- Shared globals expected by kept Vulkan / exec files
int vk_dlaa_fg_on = 0;
int vk_hdr_track = 0;
const VkTexImage *vk_dlaa_swap_img = NULL;
VkImageView vk_dlaa_swap_view = VK_NULL_HANDLE;

// ---- vk_dlaa.h / vk_mv* / vk_mvprev*
int vk_dlaa_wanted(void) { return 0; }
void vk_dlaa_instance_exts(const char **ext, uint32_t *n, uint32_t max) { (void)ext; (void)n; (void)max; }
void vk_dlaa_device_exts(const VkExtensionProperties *have, uint32_t nhave, const char **ext, uint32_t *n, uint32_t max) {
    (void)have; (void)nhave; (void)ext; (void)n; (void)max;
}
void vk_dlaa_init(void) {}
int vk_dlaa_available(void) { return 0; }
int vk_objmv_wanted(void) { return 0; }
int vk_fg_objects_on(void) { return 0; }
void vk_dlaa_pass(VkExec *ex) { (void)ex; }
int vk_dlaa_plan(const VkExec *ex, VkExecJob *j) { (void)ex; (void)j; return 0; }
void vk_dlaa_exec(VkExec *ex, const VkExecJob *j) { (void)ex; (void)j; }
void vk_dlaa_frame(void) {}
void vk_dlaa_hud(VkExec *ex) { (void)ex; }
void vk_dlaa_sub_end(VkExec *ex) { (void)ex; }
void vk_dlaa_draw(VkExec *ex) { (void)ex; }
void vk_dlaa_scan_draw(const VkExec *ex) { (void)ex; }
void vk_dlaa_viewport(VkExec *ex, VkViewport *v) { (void)ex; (void)v; }
void vk_mvsurvey_draw(VkExec *ex) { (void)ex; }
void vk_mvsurvey_frame(void) {}
void vk_mvprev_draw(VkExec *ex) { (void)ex; }
int vk_dlaa_prev_camera(VkDescriptorBufferInfo *out, float jitter[2]) { (void)out; (void)jitter; return 0; }
void vk_dlaa_game_present(void) {}
void vk_mvcheck_camera(VkCommandBuffer cb, VkImage mv, uint32_t w, uint32_t h, float jx, float jy) {
    (void)cb; (void)mv; (void)w; (void)h; (void)jx; (void)jy;
}
void vk_mvcheck_final(VkCommandBuffer cb, uint64_t fence, VkImage mv, VkImage cimg, VkFormat cfmt, uint32_t level,
                      uint32_t layer, uint32_t w, uint32_t h, float jx, float jy) {
    (void)cb; (void)fence; (void)mv; (void)cimg; (void)cfmt; (void)level; (void)layer; (void)w; (void)h; (void)jx; (void)jy;
}
void vk_mvcheck_output(VkCommandBuffer cb, VkImage out, uint32_t ow, uint32_t oh) {
    (void)cb; (void)out; (void)ow; (void)oh;
}
int vk_dlaa_skip_draw(VkExec *ex, const GfxShader *fs) { (void)ex; (void)fs; return 0; }

uint64_t vk_mvprev_step(int stage, int index, uint64_t size) { (void)stage; (void)index; (void)size; return 0; }
int vk_mvprev_on(void) { return 0; }
int vk_mvprev_checking(void) { return 0; }
int vk_mvprev_kept(int index) { (void)index; return 0; }
void vk_mvprev_want(uint32_t index_mask) { (void)index_mask; }
int vk_mvprev_find(int index, uint64_t addr, uint64_t size, uint64_t *prev, int *how) {
    (void)index; (void)addr; (void)size; (void)prev; (void)how;
    return 0;
}
void vk_mvprev_frame(void) {}

// ---- vk_fg.h / vk_fsr3.h
VkFgEngine vk_fg_engine(void) { return VK_FG_OFF; }
void vk_fg_init(void) {}
int vk_fg_active(void) { return 0; }
void vk_fg_release(void) {}
int vk_fg_wanted(int dlss) { (void)dlss; return 0; }
void vk_fg_menu(int open) { (void)open; }
int vk_fg_camera(const float *rows, int minus_w, const float dr[2], VkFgScene *s) {
    (void)rows; (void)minus_w; (void)dr; (void)s;
    return 0;
}
int vk_fg_prepare(VkCommandBuffer cb, const VkFgScene *s) { (void)cb; (void)s; return 0; }
int vk_fg_hudless(VkCommandBuffer cb, VkImage src, VkFormat fmt, uint32_t level, uint32_t layer, uint32_t w, uint32_t h) {
    (void)cb; (void)src; (void)fmt; (void)level; (void)layer; (void)w; (void)h;
    return 0;
}
int vk_fg_generate(VkCommandBuffer cb, uint64_t fence, uint64_t frame, VkImage win, VkFormat fmt, uint32_t w, uint32_t h) {
    (void)cb; (void)fence; (void)frame; (void)win; (void)fmt; (void)w; (void)h;
    return 0;
}
void vk_fg_mark(VkCommandBuffer cb, int which) { (void)cb; (void)which; }
void vk_fg_cpu(uint64_t ns) { (void)ns; }
void vk_fg_frame(void) {}
void vk_fg_exit_log(void) {}
int vk_fg_present_swap(VkImage *img, VkFormat *fmt) { (void)img; (void)fmt; return 0; }
VkImage vk_fg_present_gen(VkImage presented) { (void)presented; return VK_NULL_HANDLE; }

int fsr3_dispatch(VkCommandBuffer cb, const Fsr3Frame *f) { (void)cb; (void)f; return 0; }
void fsr3_release(void) {}
int vk_fsr3_blob(uint32_t pass, uint32_t options, void *out) { (void)pass; (void)options; (void)out; return 0; }

// ---- vk_hdr / vk_color / vk_contact (vk.h, vk_exec.h, gfx.h)
void vk_hdr_shader_swap(uint32_t **spirv, uint64_t *words) { (void)spirv; (void)words; }
VkImage vk_color_record(VkCommandBuffer cb, unsigned slot, VkImage src, VkFormat fmt, const int crop[4]) {
    (void)cb; (void)slot; (void)src; (void)fmt; (void)crop;
    return VK_NULL_HANDLE;
}
void vk_color_release(void) {}
int vk_hdr_wanted(void) { return 0; }
void vk_hdr_off(void) {}
int vk_hdr_surface_format(const VkSurfaceFormatKHR *f, uint32_t n, VkSurfaceFormatKHR *out) {
    (void)f; (void)n; (void)out;
    return 0;
}
void vk_hdr_metadata(VkSwapchainKHR sc, int new_sc) { (void)sc; (void)new_sc; }
void vk_hdr_set_active(int on) { (void)on; }
int vk_hdr_active(void) { return 0; }
VkImage vk_hdr_encode(VkCommandBuffer cb, unsigned slot, VkImage src, VkFormat fmt, const int crop[4]) {
    (void)cb; (void)slot; (void)src; (void)fmt; (void)crop;
    return VK_NULL_HANDLE;
}
VkImageView vk_hdr_ui_target(VkCommandBuffer cb, uint32_t w, uint32_t h) { (void)cb; (void)w; (void)h; return VK_NULL_HANDLE; }
void vk_hdr_ui_compose(VkCommandBuffer cb, VkImageView dst_view, VkFormat fmt, uint32_t w, uint32_t h) {
    (void)cb; (void)dst_view; (void)fmt; (void)w; (void)h;
}
void vk_hdr_to_srgb8(const uint8_t *in, uint8_t *out, size_t px, int a2r) {
    (void)in; (void)out; (void)px; (void)a2r;
}
void vk_hdr_release(void) {}
void vk_hdr_support(float out[4]) { if (out) memset(out, 0, sizeof(float) * 4); }
void vk_hdr_present_source(const VkTexImage *win, VkImage *image, VkFormat *format) {
    (void)win; (void)image; (void)format;
}
void vk_hdr_image_gone(VkImage image) { (void)image; }
void vk_hdr_draw(VkExec *ex, const VkPipeInfo *info) { (void)ex; (void)info; }

int vk_contact_strength(void) { return 0; }
void vk_contact_release(void) {}
void vk_contact_pass_begin(VkExec *ex) { (void)ex; }
void vk_contact_flush(VkExec *ex) { (void)ex; }
int vk_contact_plan(const VkExec *ex, int pass, VkExecJob *j) { (void)ex; (void)pass; (void)j; return 0; }
void vk_contact_exec(VkExec *ex, const VkExecJob *j) { (void)ex; (void)j; }

// ---- Resolution / camera speed / frame_step (host.h)
float cam_speed_wanted(void) { return 1.0f; }
float cam_speed_applied(void) { return 1.0f; }
int cam_pack_build(const void *in, size_t n, const void *zsdic, size_t zsdic_n, void **out, size_t *out_n) {
    (void)in; (void)n; (void)zsdic; (void)zsdic_n; (void)out; (void)out_n;
    return 0;
}
void cam_pack_served(void) {}

uint64_t res_grass_heap_extra_kib(void) { return 0; }
uint64_t res_grass_heap_extra_bytes(void) { return 0; }
uint64_t res_heap_extra_bytes(void) { return 0; }
uint64_t res_heap_extra_kib(void) { return 0; }
uint64_t res_tex_heap_extra_bytes(void) { return 0; }
int res_heapdef_tex_grown(const void *in, size_t n) { (void)in; (void)n; return 0; }
int res_heapdef_plain(const void *in, size_t n) { (void)in; (void)n; return 0; }
int res_heapdef_build(const void *in, size_t n, void **out, size_t *out_n) {
    (void)in; (void)n; (void)out; (void)out_n;
    return 0;
}
void res_heapdef_served(void) {}
void res_init(void) {}
float res_dlss_lod_bias(void) { return 0.0f; }
int res_dlss_mode(const char *mode) { (void)mode; return 0; }
int res_dlss_dll(void) { return 0; }
void res_fn_hit(uint64_t addr, uint64_t x0) { (void)addr; (void)x0; }
void res_present(Cpu *c) { (void)c; }
void res_wanted(unsigned out[2]) {
    if (ov_handheld()) {
        out[0] = 1280;
        out[1] = 720;
    } else {
        out[0] = 1920;
        out[1] = 1080;
    }
}
void res_asked(unsigned out[2]) { res_wanted(out); }
void res_sizes(unsigned out[2], unsigned scene[2]) {
    res_wanted(out);
    if (scene) {
        const char *v = cfg_value("TOTK_RENDER_SCALE");
        int pct = v && strcmp(v, "game") ? atoi(v) : 100;
        if (pct == 67) {
            scene[0] = (out[0] * 2u) / 3u;
            scene[1] = (out[1] * 2u) / 3u;
        } else if (pct >= 50 && pct <= 200) {
            scene[0] = (out[0] * (unsigned)pct + 50u) / 100u;
            scene[1] = (out[1] * (unsigned)pct + 50u) / 100u;
        } else {
            scene[0] = out[0];
            scene[1] = out[1];
        }
        if (scene[0] < 1u) scene[0] = 1u;
        if (scene[1] < 1u) scene[1] = 1u;
    }
}
int res_is_16x9(unsigned w, unsigned h) {
    if (!h) return 1;
    double r = (double)w / (double)h;
    double err = r / (16.0 / 9.0) - 1.0;
    return err >= -0.01 && err <= 0.01;
}
int res_wants_wide_overlay(const unsigned out[2]) { (void)out; return 0; }
void res_fov_live(void) {}
void res_dynres_live(void) {}

void frame_step_init(void) {}
int frame_step_realtime(void) { return 0; }
uint64_t frame_step_cap_ns(void) { return 1000000000ull / 60; }
double frame_step_limit_hz(void) { return 60.0; }
int64_t frame_step_world_us(void) { return -1; }
uint64_t frame_step_ticks(void) { return __atomic_load_n(&gfx_counters.frames, __ATOMIC_RELAXED); }
void frame_step_present(uint64_t display_ns, int interval) { (void)display_ns; (void)interval; }

// ---- Mods / cheats / restbl / amiibo / nfp
void mods_conflict(int winner, int loser, ModPart part, uint32_t count) {
    (void)winner; (void)loser; (void)part; (void)count;
}
int mods_conflict_count(void) { return 0; }
const ModConflict *mods_conflict_at(int i) { (void)i; return NULL; }
void mods_conflicts_log(ModPart part) { (void)part; }
const ModInfo *mods_by_id(int id) { (void)id; return NULL; }
void mods_scan(void) {}
void mods_peek_romfs(void (*cb)(const char *name, const char *romfs, void *ctx), void *ctx) {
    (void)cb; (void)ctx;
}
const char *mods_dir(void) { return ""; }
int mods_count(void) { return 0; }
ModInfo *mods_at(int i) { (void)i; return NULL; }
void mods_set_enabled(int i, int on) { (void)i; (void)on; }
void mods_set_no_code(int i, int on) { (void)i; (void)on; }
void mods_move(int i, int delta) { (void)i; (void)delta; }
int mods_changed(void) { return 0; }
void mods_exefs_switch(void) {}
void mods_exefs_apply(uint64_t base, uint64_t size, uint64_t code_end,
                      int (*store)(uint64_t addr, const uint8_t *bytes, int n)) {
    (void)base; (void)size; (void)code_end; (void)store;
}
long mods_exefs_data(const ModInfo *m, uint64_t base, uint64_t size, uint64_t code_end,
                     int (*store)(uint64_t addr, const uint8_t *bytes, int n)) {
    (void)m; (void)base; (void)size; (void)code_end; (void)store;
    return 0;
}
void mods_probe_cache(const char *status, const char *data) { (void)status; (void)data; }
void mods_rescan(void) {}
int mods_exefs_pending(uint32_t *key) { (void)key; return 0; }
void mods_plugin_off(int set, const char *why) { (void)set; (void)why; }
void mods_probe_plugins(const char *status, const char *stale) { (void)status; (void)stale; }
const char *mods_main_build_id(void) { return ""; }

void cheats_init(void) {}
int cheats_count(void) { return 0; }
int cheats_rebuild_pending(void) { return 0; }
void cheats_rebuild_allow(void) {}
const CheatInfo *cheats_info(int i) { (void)i; return NULL; }
int cheats_on(int i) { (void)i; return 0; }
void cheats_set(int i, int on) { (void)i; (void)on; }
const char *cheats_file(void) { return ""; }
void cheats_frame(uint64_t frame) { (void)frame; }
const char *cheats_buttons(uint32_t mask, char *buf, int n) {
    (void)mask;
    if (buf && n > 0) buf[0] = 0;
    return buf ? buf : "";
}
bool tab_cheats(float scale) { (void)scale; return false; }

Restbl *restbl_parse(const uint8_t *data, size_t n) { (void)data; (void)n; return NULL; }
void restbl_free(Restbl *t) { (void)t; }
const char *restbl_key(const char *path, char *buf, size_t n) {
    if (buf && n > 0) snprintf(buf, n, "%s", path ? path : "");
    return buf ? buf : "";
}
int restbl_get(const Restbl *t, const char *key, uint32_t *size) { (void)t; (void)key; (void)size; return 0; }
int restbl_raise(Restbl *t, const char *key, uint32_t size) { (void)t; (void)key; (void)size; return 0; }
int restbl_merge(Restbl *t, const Restbl *base, const Restbl *shipped) { (void)t; (void)base; (void)shipped; return 0; }
int restbl_take(Restbl *t, const Restbl *from, const char *const *keys, size_t n) {
    (void)t; (void)from; (void)keys; (void)n;
    return 0;
}
uint8_t *restbl_write(const Restbl *t, size_t *n) { (void)t; if (n) *n = 0; return NULL; }
uint32_t restbl_estimate(const char *key, uint64_t size) { (void)key; (void)size; return 0; }
uint32_t restbl_grow(const char *key, uint32_t entry, uint64_t old, uint64_t new_size, int scaled) {
    (void)key; (void)old; (void)new_size; (void)scaled;
    return entry;
}
int restbl_sarc_each(const uint8_t *p, size_t n, RestblMember *each, void *ctx) {
    (void)p; (void)n; (void)each; (void)ctx;
    return 0;
}
int restbl_pack_raise(Restbl *t, const Restbl *base, const uint8_t *mod, size_t mod_n, const uint8_t *orig,
                      size_t orig_n, RestblNote *note, void *ctx) {
    (void)t; (void)base; (void)mod; (void)mod_n; (void)orig; (void)orig_n; (void)note; (void)ctx;
    return 0;
}
int restbl_probe(void) { return 0; }

void amiibo_init(void) {}
void amiibo_set_device_present(bool present) { (void)present; }
bool amiibo_is_device_present(void) { return false; }
void amiibo_start_detection(void) {}
void amiibo_stop_detection(void) {}
bool amiibo_is_detecting(void) { return false; }
bool amiibo_mount(void) { return false; }
void amiibo_unmount(void) {}
bool amiibo_is_mounted(void) { return false; }
AmiiboDeviceState amiibo_get_device_state(void) { return AMIIBO_DEV_STATE_UNAVAILABLE; }
AmiiboDeviceState amiibo_get_reader_state(void) { return AMIIBO_DEV_STATE_UNAVAILABLE; }
bool amiibo_load_file(const char *path) { (void)path; return false; }
bool amiibo_scan_and_tap(const char *path) { (void)path; return false; }
void amiibo_tap_tag(void) {}
void amiibo_remove_tag(void) {}
void amiibo_tick(void) {}
void amiibo_set_auto_remove(bool enable) { (void)enable; }
bool amiibo_get_auto_remove(void) { return false; }
void amiibo_set_auto_remove_delay_s(float seconds) { (void)seconds; }
float amiibo_get_auto_remove_delay_s(void) { return 0.0f; }
bool amiibo_has_tag(void) { return false; }
const AmiiboInfo *amiibo_get_current_info(void) { return NULL; }
const uint8_t *amiibo_get_raw_data(void) { return NULL; }
void amiibo_get_tag_info(void *out_tag_info) { (void)out_tag_info; }
void amiibo_get_model_info(void *out_model_info) { (void)out_model_info; }
void amiibo_get_common_info(void *out_common_info) { (void)out_common_info; }
void amiibo_get_register_info(void *out_register_info) { (void)out_register_info; }
int amiibo_get_application_area(void *out_buf, uint32_t max_size) { (void)out_buf; (void)max_size; return 0; }
void tab_amiibo(float scale) { (void)scale; }
void amiibo_collect_filenames(void (*callback)(void *, const char *), void *ctx) { (void)callback; (void)ctx; }
void amiibo_request_font_rebuild(void) {}
bool amiibo_check_font_rebuild(void) { return false; }

void nfp_user_manager_handler(IpcSession *s, const IpcRequest *rq, IpcResponse *rs) {
    (void)s; (void)rq;
    if (rs) rs->result = IPC_R_UNKNOWN_COMMAND;
}

// ---- gameplay_tweaks / direct mouse / menu_cursor / menu_ui / quicksave / freecam (input.h)
int gameplay_tweaks_available(void) { return 0; }
int gameplay_tweaks_capture_needed(void) { return 0; }
int gameplay_tweaks_map_zoom(void) { return 0; }
int gameplay_tweaks_strip_wheel(uint64_t buttons) { (void)buttons; return 0; }
void gameplay_tweaks_init(void) {}
void gameplay_tweaks_reset(void) {}
void gameplay_tweaks_wheel(double delta, int c_down, int right_down) { (void)delta; (void)c_down; (void)right_down; }
void gameplay_tweaks_prepare_keys(uint8_t *down, uint64_t pad_buttons) { (void)down; (void)pad_buttons; }
void gameplay_tweaks_apply(uint64_t frame, int active, uint64_t *buttons,
                           int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry) {
    (void)frame; (void)active; (void)buttons; (void)lx; (void)ly; (void)rx; (void)ry;
}

int input_direct_mouse_active(void) { return 0; }
int input_direct_mouse_enabled(void) { return 0; }
void input_direct_mouse_radial(int32_t dx, int32_t dy, float sensitivity, float cursor[2], int32_t out[2]) {
    (void)dx; (void)dy; (void)sensitivity; (void)cursor;
    if (out) out[0] = out[1] = 0;
}
void input_direct_mouse_row(int32_t dx, float sensitivity, uint64_t now, InputMouseRow *row, int32_t out[2]) {
    (void)dx; (void)sensitivity; (void)now; (void)row;
    if (out) out[0] = out[1] = 0;
}
int input_direct_mouse_shake(void) { return 0; }

int menu_cursor_shortcut(void) { return 0; }
void menu_cursor_poll(uint64_t *buttons) { (void)buttons; }
int menu_cursor_pointer(void) { return 0; }
int menu_cursor_map(void) { return 0; }
int menu_cursor_dialogue(void) { return 0; }
int menu_cursor_menu(void) { return 0; }
int menu_cursor_strip(void) { return 0; }
int menu_cursor_camera(void) { return 0; }
int menu_ui_open(const char *layout) { (void)layout; return 0; }
int menu_ui_focus(char *screen, size_t n, uint64_t *node) { (void)screen; (void)n; (void)node; return 0; }
uint64_t menu_ui_button(const char *parts, const char *root, int *usable_out) {
    (void)parts; (void)root;
    if (usable_out) *usable_out = 0;
    return 0;
}
uint64_t menu_ui_first_save(void) { return 0; }
int menu_ui_focus_to(uint64_t node) { (void)node; return 0; }

int quicksave_on(void) { return 0; }
void quicksave_keys(uint8_t *down, int act) { (void)down; (void)act; }
void quicksave_request(int load) { (void)load; }
void quicksave_guest(Cpu *c) { (void)c; }
int quicksave_poll(uint64_t *buttons, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry) {
    (void)buttons; (void)lx; (void)ly; (void)rx; (void)ry;
    return 0;
}
int quicksave_busy(void) { return 0; }

int freecam_available(void) { return 0; }
int freecam_state(void) { return 0; }
void freecam_cycle(void) {}
void freecam_pad(const FreecamPad *p) { (void)p; }
void freecam_speed_click(int faster) { (void)faster; }
void freecam_mouse(double right_deg, double down_deg) { (void)right_deg; (void)down_deg; }

// ---- discord / playtime / plugins / omitted probes
void discord_init(void) {}
void playtime_init(void) {}
void playtime_save(void) {}
int playtime_get(uint64_t *total_s, uint64_t *session_s) {
    if (total_s) *total_s = 0;
    if (session_s) *session_s = 0;
    return 0;
}
void plugin_init_setup(void) {}
void plugin_init_run(Cpu *c, const RtLoaded *sdk) { (void)c; (void)sdk; }
void plugins_setup(void) {}
int plugins_start(Cpu *c, const RtLoaded *sdk) { (void)c; (void)sdk; return 0; }
int vk_upscaler_running(void) { return 0; }
int vk_upscaler_missing(int engine) { return engine != 0; }

