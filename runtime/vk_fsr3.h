#pragma once
#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    VkImage color;
    VkFormat color_format;
    VkImage depth;
    VkImage mv;
    VkImage out;
    uint32_t w, h, ow, oh;
    float jx, jy;
    float near_m, far_m, fov_y;
    float dt_ms;
    int reset;
    float sharpness;
    uint64_t fence;
} Fsr3Frame;

int fsr3_dispatch(VkCommandBuffer cb, const Fsr3Frame *f);
void fsr3_release(void);
int vk_fsr3_blob(uint32_t pass, uint32_t options, void *out);

#ifdef __cplusplus
}
#endif
