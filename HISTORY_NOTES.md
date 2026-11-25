# opensport 历史排查记录与踩坑归档（仅供参考）

> **⚠️ 重要提示：本文档记录的 3.1（非 Joy-Con 项）、3.2、3.3 代码改动均已从当前 `opensport` 分支移除！**
> 当前分支代码已干净重置回 `f7a84fc` baseline + 仅保留 `94b3575` 的 Joy-Con 手柄修复。本文档仅用于保存过去排查中**已经实测验证的现象、根因实证与已证伪路径**，供新会话在干净基线上重新设计与实现修复时参考，避免重复走弯路。

---

## 1. 历史阶段代码改动台账（非 Joy-Con 项均已移除）

### 1.1 曾实现过并验证有效的功能（已移除，后续需干净重写）

| 模块 | 原涉及文件 | 改动要点与效果 |
|---|---|---|
| **H.264/AVC 开场视频解码**（原 `ab04333` / `e654206`） | `runtime/movie.h`<br>`runtime/ov_movie.c` | 使用主机 OpenH264 解码 H.264 (`decoder type 1 mode 1 format 4`)，并在 slot 释放后正确移交 end-of-stream 标记；修复后开场动画与菜单音乐均可正常运行。 |
| **6×N 方形 `2DArray` 立方体视图**（原 `94b3575` 部分） | `runtime/vk.h`<br>`runtime/vk_texture.c`<br>`runtime/vk_exec_draw.c` | `VkTexImage` 新增 `cube_compat`；对未压缩且 `layers >= 6 && layers % 6 == 0` 的方形 `2DArray` 启用 `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT` 并在 `get_view()` 放行（修复 `tex 1060` 等 `r11g11b10f` 纹理按 `decl cube` 采样不兼容）；压缩数组保持关闭以保留 AMD 驱动规避（issue #59）。 |
| **F12 按需帧转储**（原 `9f4407b`） | `runtime/vk_dump.c`<br>`runtime/vk_backend.c`<br>`runtime/input_win.c` | 运行中按 F12 延迟 3 帧抓取下一帧，按需读取 `TOTK_DUMP_TEX` / `TOTK_DUMP_DIR`；不可写目录时仅记日志不再 `FATAL`。 |
| **预通道深度 `LEQUAL` 修复**（原 `a1026d5`） | `runtime/vk_pipeline.c` | `depth_compare_op()` 中对 `dt && !dw && df == NVN_DEPTH_FUNC_EQUAL` 改为 `VK_COMPARE_OP_LESS_OR_EQUAL`，容忍 Z-prepass 与上色 pass 使用不同 VS 导致的 `gl_Position` 末位浮点差异。实测网球场 `pass_0015` 有效色数由 `40,176` 增至 `334,250`，恢复草地、白线、球网与看台几何。 |

### 1.2 曾用过的临时诊断思路（无代码残留）

- `vk_dump_draw()` 导出完整 `pipe` 状态行与 `state` 字段，并将 `MAX_ENTS` 提至 `256`（避免 `pass 0015` 截断）。
- `vk_exec_draw.c` 静默回退单次日志：`fp_c1` 落全零与视图不兼容绑定 1×1 占位图。
- 注意：`runtime/` 中新增临时开关务必用 `getenv()`，**切勿**调用未在配置表注册的 `cfg_bool()`，否则启动即 `FATAL`。

### 1.3 曾尝试的未提交 `NVN_STAGE_GS` 补丁及踩坑（已删除，供设计避坑）

曾尝试直接按如下方式接入 `NVN_STAGE_GS`，编译与着色器翻译（0 失败）和管线构建（0 失败）均通过，但**放行 `"gs"` 后进入主菜单即整屏全黑（开场动画和音乐正常，50 fps，无报错）**：

- `runtime/gfx_shader.c`：`reflect()`、`req_init()`、`reflection_valid()` 放行 `"gs"` ↔ `GFX_STAGE_GS`。
- `runtime/vk.h` / `runtime/vk_pipeline.c`：`VkPipeState` / `GKey` / `Job` 增加 `gs`、`VK_SHADER_STAGE_GEOMETRY_BIT`、`spirv_rebind` 与 `pin_subgroup32`；含 `s->gs` 时跳过 `spirv_add_outputs`、强制单描述符集、禁用 `gpl_link` 与 `keys_note`。
- `runtime/vk_exec_draw.c`：定义 `NVN_STAGE_GS 2`，`c0` 缓存数组扩至 4，补齐 Maxwell GS `c0` SSBO 基址 `0x410` 与屏障追踪（含 GS 时跳过 `D->SM` 缓存）。
- `runtime/vk_device.c`：启用 `VK_NV_geometry_shader_passthrough` 与 `VK_NV_viewport_array2`。

**教训**：该实现尚未排查出“放行 GS 导致菜单全黑”的具体原因（未排除 GS 内 `%31 ViewportMaskNV` 为 0 丢弃光栅化、`VS → GS` 跳过 `spirv_add_outputs` 导致 `PassthroughNV Location` 缺失、或特定 GS 向 SSBO/HDR 写了坏值），重新设计 GS 时需逐项验证或先按 `prog->serial` / 单个 GS 程序二分验证。

---

## 2. 历史主机环境与渲染问题画像

### 2.1 主机环境

- **GPU 0（主力）**：NVIDIA GeForce GTX 1650（Turing，驱动 621.01，Vulkan 1.4.357）。
- **GPU 1（核显）**：AMD Radeon(TM) Graphics（`TOTK_GPU_INDEX=1` 时卡在 Joy-Con 腕带确认界面）。
- 本机未安装 `VK_LAYER_KHRONOS_validation`。

### 2.2 网球场异常现象与关键对照（`LEQUAL` 之后、GS 之前）

在仅做 `LEQUAL` 修复（未接 GS）的网球场比赛第 3671 帧（`dump_court3` 已清理）：
- **正常部分**：UI、海洋、SSAO、阴影遮罩（`pass 0014` 身体与地面大部呈绿色 `255` 全受光）、白线与 `TENNIS` 地标（`vs 5c1b6253` + `fs db3ae306`）、顶部观众（`vs ee2f2152` + `fs 41303b26`）、远端 `Local` 角色（`pass 0017 pipe 2`，`vs 26565057` + `fs b086765d`）、球网、球拍线、草地部分区域。
- **全黑/极暗部分**：天空球（`pass 0015/0016 pipe 90`，`vs da456e02` + `fs b77336bf`）、球场主面（`c674bdf0+1d383408`）、看台（`382b788e`/`93600363`）、近处 3 名角色（`near_player`/`Miguel`/`Julie`，`pass 0015 pipe 38/51`，`vs 13b1e0e4` + `fs fe507b68`——深度轮廓内 `6020` 像素并非全 0，而是约 1% 极暗 `[0..13, 0..14, 0..4]`）。
- **关键 A/B 对照**：
  - `fe507b68`（近黑角色）与 `b086765d`（正常 `far_local`）绑定**完全相同的角色贴图**（`tex 7715..7719`）、**完全相同的 cubemap**（`tex 7197`/`3259`）、**完全相同的 `vattr` 布局**，且 `vs/fs ubo` 除 `c4[26].x` 外 100% 一致。
  - 天空球 `pipe 90`（`da456e02+b77336bf`）结构极简：无 `fp_c1`、无光照/阴影/cubemap、`OpKill` 为 0，仅采样 `tex 1474`（4096×2048 BC6H），仍输出 `[0,0,0]`。

### 2.3 根因实证链：间接绘制缓冲全零 → 游戏依赖 Passthrough GS

1. **直接绘制 vs 间接绘制分水岭**：
   - 正常显示的远端角色 `b086765d` 走直接绘制 `vkCmdDrawIndexed`（`cnt=84`）。
   - 黑掉的天空球 `b77336bf`（`ind=0x1b50e7000`, `ibuf=0x1b50e7014`）与近处角色 `fe507b68`（`ind=0x1b12c2b40`, `ibuf=0x1b12c2b54`）均走间接绘制 `vkCmdDrawIndexedIndirect`（`op_draw_idx_ind`）。
2. **间接命令与索引缓冲 100% 全零**：在 `TOTK_MEMORY=import` 下插桩统计，**`250,001 / 250,001`** 次 `DrawElementsIndirect` 的间接命令全为 `cmd=(0,0,0,0,0)`、索引缓冲全为 `ib=0,0,0,0,0,0`。
3. **写入者锁定为 Passthrough GS**：
   - 网球整帧内 **0 次** compute dispatch（全会话仅 2 个 `1x1x1` CS），排除 CS 写入。
   - 静态回溯调用链：`nvnCommandBufferDrawElementsIndirect` (`lr=0x700192e2dc`) → `generated/v0/main/main_0227.c` (`f_700192e0c0`, `L_700192e248`) → `f_700192e930`（将 `ind` 缓冲作为 SSBO 绑定到 NVN stage 0/1/2/5）→ `main_0237.c:9911`（Z-prepass 先绑定 SSBO 再调用 `f_700027747c` 直接绘制）。
   - `nvnProgramSetShaders` 实测确认游戏注册了大量 `n=3, stage_mask=0x7`（`VS | FS | GS`）程序。
   - 缓存中 GS SPIR-V（`03ded194f0c233ac2fdcfe5af7edc6b55cd8f11008c9e5555b8ae079932c71e7.res.json`）反射为 `stage="gs"`、`sbufs=[{binding:1, slot:0, cb_slot:0, cb_offset:264, flags:1}]`（`264 * 4 = 0x420 = 0x410 + 16 * 1`，即 Maxwell GS `c0` SSBO 槽 1 写操作）；声明 `Geometry`、`GeometryShaderPassthroughNV`、`ShaderViewportMaskNV`、`SPV_NV_geometry_shader_passthrough`、`SPV_NV_viewport_array2`、`BuiltIn ViewportMaskNV`、`PassthroughNV` 输入等。

---

## 3. 已证伪 / 已排除路径总表（切勿重复浪费时间测试）

### 3.1 网球场黑天空 / 黑球场 / 黑角色阶段已排除项

| 怀疑方向 | 历史验证手段 | 结论 |
|---|---|---|
| **Z-prepass `LEQUAL` 仍漏剔除** | `NSS_DIAG_DEPTH_ALWAYS=2`（`dt && !dw` 强制 `ALWAYS`） | **已证伪**：`pass 0015` 仅 0.8% 像素变化，黑区域不变。 |
| **Window Origin / 正反面朝向反转** | `DeviceSetWindowOriginMode(1 -> upper left)` + `NSS_DIAG_FLIP_FRONT=1` | **已证伪**：反转导致含 UI 在内整屏全黑。 |
| **背面剔除 `cull 2` 与 `z==w` 远平面裁剪** | `NSS_DIAG_FORCE_CULL0=1` + `NSS_DIAG_DEPTH_CLAMP=1` | **已证伪**：天空/球场/看台/3 黑角色依然全黑。 |
| **`BC6H_UFLOAT` 贴图全零或 BNTX 布局错误** | `NSS_DIAG_DUMMY_BC6H=1` + 解包 `RegularMenu.bkres.zs` | **已证伪**：禁用 BC6H 后外围草坪与看台明显掉暗，`tex 1474` 含真实非零数据，`BRTI` `bhl=4` 与 `mipOffsets` 一致。 |
| **Cubemap / UBO 常量数组异常** | `dump_ubo/` + `diag_fsubo.txt` + `shader-programs.bin` 静态检查 | **已证伪**：`fe507b68` 与 `b086765d` 的 `vs/fs ubo c3..c6`（除 `c4[26].x`）及贴图完全一致；726 个程序全部 `c1_off % 64 == 0`。 |
| **VS/FS SPIR-V 缺陷 / 接口不匹配 / FP64 / TXD / Interlock** | `spirv-val --target-env vulkan1.3` + 7 组关键对反汇编 | **已证伪**：89 个 `.spv` 0 错误，7 组关键对全部无缺失输出或未初始化变量。 |
| **管线预热失败 / `fp_c1` 落零 / 视图回退 / `vp`/`sc`/`vattr` 错误** | 日志统计与管线状态导出 | **已证伪**：预热 0 失败，`fp_c1` 0 落零，`vp`/`sc`/`vattr` 全正常。 |
| **混合模式 / 逻辑运算错误** | `NSS_DIAG_NO_BLEND_R11=1` + 管线日志 | **已证伪**：`pass 0010/0015` 本身 `blend=0, lop=3(COPY)`。 |
| **GPL / 纹理别名 / 屏障合并 / 动态顶点 / 管线缓存 / `BindUniformBuffersDeferred`** | `TOTK_GPL=0`、`TOTK_TEX_ALIAS=0`、`TOTK_BARRIER_MERGE=0`、`TOTK_VK_DYN_VERTEX=0`、`TOTK_PIPELINE_CACHE=0` | **已排除**：均无改善；`BindUniformBuffersDeferred` 调用次数为 0。 |
| **Compute Shader 生成间接绘制缓冲** | 插桩统计 `op_dispatch` | **已证伪**：网球帧内 0 次 compute dispatch，全会话仅 2 个 `1x1x1` CS。 |
| **禁用 Passthrough GS 能力诱导游戏回退** | `NVN_DEVICE_INFO_SUPPORTS_PASSTHROUGH_GEOMETRY_SHADERS (0x42)` 返回 0 | **已证伪**：游戏不回退，仍然绑定并依赖 GS。 |

### 3.2 曾尝试接入 GS 后主菜单全黑阶段已排除项

| 怀疑方向 | 历史验证手段 | 结论 |
|---|---|---|
| **Vulkan 设备丢失或呈现链路损坏** | 日志检查与帧率观察 | **已排除**：开场动画、菜单音乐、50.7 fps 正常，无报错或管线构建失败。 |
| **未开启 NV GS 扩展或旧管线缓存污染** | `vk_device.c` 开启两项 NV 扩展 + `TOTK_PIPELINE_CACHE=0` | **已排除**：扩展生效且跳过旧缓存后菜单依然全黑。 |
| **GPL 单/双描述符集切换或增量绑定串台** | `TOTK_GPL=0` + `TOTK_BIND_INC=0` | **已排除**：强制单描述符集并关闭增量绑定后菜单依然全黑。 |
| **非 GS 代码补丁引入的回归** | 对照 `reflection_valid()` 放行 `"gs"` 前后 | **已排除**：仅当放行 `"gs"` 后才黑屏，未放行时菜单 UI 正常。 |
