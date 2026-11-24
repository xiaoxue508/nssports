// Stubs for libnn_glslc imports (NS Sports ships precompiled NVN shaders in RomFS; runtime GLSL compilation is unused).
#include <stdint.h>
#include <string.h>

#include "host.h"
#include "ov.h"

static void ov_glslc_initialize(Cpu *c) {
    uint64_t obj = arg_x(c, 0);
    if (obj) memset((void *)(uintptr_t)obj, 0, 64);
    ret_x(c, 1);
}

static void ov_glslc_finalize(Cpu *c) { (void)c; }

static void ov_glslc_get_default_options(Cpu *c) {
    // Returns GLSLCoptions by value (large struct via x8)
    if (c->x[8]) memset((void *)(uintptr_t)c->x[8], 0, 128);
}

static void ov_glslc_get_version(Cpu *c) {
    // GLSLCversion { apiMajor=4, apiMinor=23, gpuCodeVersionMajor=1, gpuCodeVersionMinor=15, package=0 } via x8
    if (c->x[8]) {
        uint32_t *v = (uint32_t *)(uintptr_t)c->x[8];
        memset(v, 0, 0x34);
        v[0] = 4;
        v[1] = 23;
        v[2] = 1;
        v[3] = 15;
    }
}

static void ov_glslc_set_allocator(Cpu *c) { (void)c; }

static void ov_glslc_compile(Cpu *c) {
    static int logged;
    if (!logged++) rt_log("glslc: unexpected runtime shader compilation call (returning failure)");
    ret_x(c, 0);
}

HOST_OVERRIDE("glslcInitialize", ov_glslc_initialize);
HOST_OVERRIDE("glslcFinalize", ov_glslc_finalize);
HOST_OVERRIDE("glslcGetDefaultOptions", ov_glslc_get_default_options);
HOST_OVERRIDE("glslcGetVersion", ov_glslc_get_version);
HOST_OVERRIDE("glslcSetAllocator", ov_glslc_set_allocator);
HOST_OVERRIDE("glslcCompile", ov_glslc_compile);
HOST_OVERRIDE("glslcCompilePreSpecialized", ov_glslc_compile);
HOST_OVERRIDE("glslcCompileSpecialized", ov_glslc_compile);
