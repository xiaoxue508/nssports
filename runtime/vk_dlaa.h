#pragma once
#include "vk.h"

#define DLAA_SCENE_MIN_W 320u

typedef struct VkExec VkExec;
typedef struct VkExecJob VkExecJob;

int vk_dlaa_wanted(void);
void vk_dlaa_instance_exts(const char **ext, uint32_t *n, uint32_t max);
void vk_dlaa_device_exts(const VkExtensionProperties *have, uint32_t nhave, const char **ext, uint32_t *n, uint32_t max);
void vk_dlaa_init(void);
int vk_dlaa_available(void);
int vk_objmv_wanted(void);
int vk_fg_objects_on(void);
void vk_dlaa_pass(VkExec *ex);
int vk_dlaa_plan(const VkExec *ex, VkExecJob *j);
void vk_dlaa_exec(VkExec *ex, const VkExecJob *j);
void vk_dlaa_frame(void);
extern int vk_dlaa_fg_on;
void vk_dlaa_hud(VkExec *ex);
void vk_dlaa_sub_end(VkExec *ex);
void vk_dlaa_draw(VkExec *ex);
void vk_dlaa_scan_draw(const VkExec *ex);
void vk_dlaa_viewport(VkExec *ex, VkViewport *v);
void vk_mvsurvey_draw(VkExec *ex);
void vk_mvsurvey_frame(void);
void vk_mvprev_draw(VkExec *ex);
int vk_mvobj_execute(VkExec *ex, uint64_t cam, VkImageView mv, VkImageView depth, VkFormat depth_fmt, uint32_t w,
                     uint32_t h);
int vk_dlaa_prev_camera(VkDescriptorBufferInfo *out, float jitter[2]);
void vk_dlaa_camera_copied(uint64_t lr, uint64_t dst, uint64_t off);
void vk_dlaa_game_present(void);
void vk_mvcheck_camera(VkCommandBuffer cb, VkImage mv, uint32_t w, uint32_t h, float jx, float jy);
void vk_mvcheck_final(VkCommandBuffer cb, uint64_t fence, VkImage mv, VkImage cimg, VkFormat cfmt, uint32_t level,
                      uint32_t layer, uint32_t w, uint32_t h, float jx, float jy);
const char *vk_mvobj_frame_masks(void);
void vk_mvcheck_output(VkCommandBuffer cb, VkImage out, uint32_t ow, uint32_t oh);
int vk_dlaa_skip_draw(VkExec *ex, const GfxShader *fs);
