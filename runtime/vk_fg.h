#pragma once
#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { VK_FG_OFF, VK_FG_FSR3 } VkFgEngine;
VkFgEngine vk_fg_engine(void);
void vk_fg_init(void);
int vk_fg_active(void);
void vk_fg_release(void);
int vk_fg_wanted(int dlss);
void vk_fg_menu(int open);

typedef struct {
    VkImage depth, mv;
    uint32_t w, h;
    uint32_t ow, oh;
    float near_m, far_m, fov_y;
    int inverted, infinite;
    float pos[3], up[3], right[3], fwd[3];
    int reset;
} VkFgScene;

int vk_fg_camera(const float *rows, int minus_w, const float dr[2], VkFgScene *s);
int vk_fg_prepare(VkCommandBuffer cb, const VkFgScene *s);
int vk_fg_hudless(VkCommandBuffer cb, VkImage src, VkFormat fmt, uint32_t level, uint32_t layer, uint32_t w, uint32_t h);
int vk_fg_generate(VkCommandBuffer cb, uint64_t fence, uint64_t frame, VkImage win, VkFormat fmt, uint32_t w,
                   uint32_t h);
enum { FG_T_MV0, FG_T_MV1, FG_T_PREP0, FG_T_PREP1, FG_T_HL0, FG_T_HL1, FG_T_FI0, FG_T_FI1, FG_T_N };
void vk_fg_mark(VkCommandBuffer cb, int which);
void vk_fg_cpu(uint64_t ns);
void vk_fg_frame(void);
void vk_fg_exit_log(void);
int vk_fg_present_swap(VkImage *img, VkFormat *fmt);
VkImage vk_fg_present_gen(VkImage presented);

#ifdef __cplusplus
}
#endif
