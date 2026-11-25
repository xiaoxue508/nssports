# opensport 交接文档

> **项目**：*Nintendo Switch Sports*（Title ID `0100D2F00D5C0000`，USA v0）Windows x86-64 静态重编译
> **工作区**：`E:\nssports` ｜ **当前分支**：`opensport`（基线 `master` = `f7a84fc`）
> **远程仓库**：`https://github.com/xiaoxue508/nssports`（public）
> **当前产物**：`E:\nssports\build\v0\nssports.exe`（基于基线 `f7a84fc` + Joy-Con 修复重编译，462 目标文件，0 错误）
> **更新日期**：2026-11-25

> **⚠️ 代码库已重置说明（接手必看）**：当前 `opensport` 分支已干净恢复为 **`f7a84fc` baseline + 仅保留 `94b3575` 的 Joy-Con 手柄修复**（`runtime/input.c`、`runtime/input_joycon_win.c`、`runtime/ov_input.c`）及本文档；下文第 3 节中其余历史功能（OpenH264、2DArray 立方体视图、F12 抓帧、`a1026d5` 预通道 `LESS_OR_EQUAL`、临时 `NSS_DIAG_*` 钩子及未提交的 GS 补丁）均已按用户要求从代码中彻底移除。**但第 4–5 节记录的所有问题现象、根因实证与已证伪路径依然有效**，新会话可直接在此干净基线上重新设计与实现修复（注意先补回所需的基础修复如 `LESS_OR_EQUAL` / 2DArray / 按需日志，再设计干净的 `NVN_STAGE_GS` 方案）。

本文档为新会话接手入口，按**「红线与省 Token 规范 → 现状速览 → 历史改动参考 → 问题画像与根因实证 → 已证伪总表 → 下一步行动」**组织。

---

## 1. 协作、构建与省 Token 红线（下任 Agent 务必严格遵守）

1. **严禁自行启动 `nssports.exe`**（无论前台还是后台）。需要实机测试时，提供**单行可复制的 PowerShell 命令**（需包含 `Remove-Item Env:...` 清理残留变量），由用户在 `E:\nssports\build\v0` 已打开的终端中亲自执行；**查日志命令与启动 `.\nssports.exe` 命令必须分开提供**（启动游戏会实时刷新日志把前面的查询输出冲掉）。
2. **严禁向 C 盘写入构建缓存或临时文件**：构建前必须将临时目录指向 E 盘：
   ```powershell
   $env:TEMP="E:\nssports\build\tmp"; $env:TMP="E:\nssports\build\tmp"; E:\nssports\.venv\Scripts\python.exe tools/build_game.py --version v0 --jobs 8
   ```
3. **文件规范**：`core.autocrlf=false`，源码与文档保持 **LF** 换行；**严禁手改 `generated/`**。向 PowerShell 传多行或含嵌套引号的 Python 补丁脚本时，一律使用 here-string（`@' ... '@ | python -`）或 Base64 解码执行，避免 PowerShell 转义破坏语法。
4. **省 Token 与看图硬性规则（极重要，违背会极快烧干上下文）**：
   - **能用数值统计，绝不直接读图**：禁止直接读取全分辨率（如 `1600x900` / `1920x1080`）截图或多张 `pass_*.png`。所有像素均值、有效色数、全黑占比、`NaN`/Inf 检测、指定坐标 RGB 值、两次运行差异率等，**一律先用 Python（`PIL`/`numpy`）输出 ≤30 行的文本摘要**。
   - **非看图不可时，只看 1 张裁剪/缩小拼图**：必须看视觉效果时，先用 Python 把关注区域（或 A/B 对比 + 差异图）裁剪拼接并缩放到 `≤800x450`，**只读这一张图**；结论写入文档后提醒用户适时新开短会话（或 `/compact`），避免图片滞留对话历史中在后续每轮重复计费。
   - **禁止大文件盲读与全库扫描**：查 `runtime/` 代码必须先用 `git grep -n` 定位行号，再按 `StartLine/EndLine` 分段只读相关函数（≤150 行），禁止整文件读 `vk_exec_draw.c` / `vk_pipeline.c`；**严禁递归扫描 `generated/`、`build/v0/shader-cache/`、`tools/vendor/`**——需要长链条回溯（如 `generated/v0/main_*.c`）时，优先派发只读 `research` 子代理调研，只把精简结论带回主对话。
   - **日志与命令输出必须裁剪**：查看 `totk.log`、`passes.txt` 或目录列表时，必须附带 `Select-String` / `Select-Object -First 40` 或 Python 去重统计，禁止把数千行原样输出灌入上下文。
   - **少轮次、先规划再动大文件**：多文件架构改动（如接入 `NVN_STAGE_GS`）先列简短方案对齐再编码；给用户的测试命令单次尽量收集齐所需诊断信息，减少“改一点→跑一次”的碎片轮次。SPIR-V 反汇编工具位于 `E:\nssports\toolchain\msys64\ucrt64\bin\spirv-dis.exe`。

### 常用单行命令参考

- **增量编译**（若仅改动 `runtime/`，在任意目录均可运行）：
  ```powershell
  $env:TEMP="E:\nssports\build\tmp"; $env:TMP="E:\nssports\build\tmp"; E:\dkb\DKB-Setup\bin\ninja.exe -C E:\nssports -f E:\nssports\build\v0\build.ninja
  ```
- **进入游戏按 F12 抓帧模板**（需先 `cd E:\nssports\build\v0`）：
  ```powershell
  Remove-Item Env:TOTK_*,Env:NSS_DIAG_* -ErrorAction SilentlyContinue; $env:TOTK_DUMP_DIR="E:\nssports\build\v0\dump_next"; .\nssports.exe
  ```
  > 进到目标画面后按 **F12**（延迟 3 帧抓取，卡顿约 1 秒），日志出现 `vk_dump: frame N goes to ... (F12)` 即完成。单次运行仅支持抓一次。

---

## 2. 当前项目状态速览

- **已打通部分**：蓝牙实体 Joy-Con (L) 输入与单柄竖持镜像已完全正常；开场动画（`ov_movie`）、音频、UI、海洋、SSAO、阴影遮罩、白线/`TENNIS` 地标、顶部观众、远端 `Local` 角色、球网、球拍线渲染正常；`a1026d5` 提交将“深度预通道 + EQUAL 上色通道”放宽为 `LESS_OR_EQUAL` 后，网球场草地、白线、球网和看台几何已出现（`pass_0015` 有效色数由 `40,176` 增至 `334,250`）。
- **原主阻塞根因已 100% 实证**：网球比赛场景中天空球、球场主面、看台以及近处 3 名角色全黑/极暗的根本原因是 **`totk-recomp` 运行时未实现几何着色器（`NVN_STAGE_GS = 2`）**。游戏在 Z-prepass 阶段使用 Passthrough GS 向 SSBO 写入可见性/间接绘制参数，随后在主着色阶段用 `nvnCommandBufferDrawElementsIndirect` 绘制；GS 缺失导致 `250,001 / 250,001` 次间接绘制的命令缓冲和索引缓冲全部保持为 0（详见第 4.4 节）。
- **当前新阻塞（接入 GS 后的黑屏回归）**：
  - 已在本地工作区 5 个 `runtime/` 文件中接入 `NVN_STAGE_GS` 支持并开启 `VK_NV_geometry_shader_passthrough` / `VK_NV_viewport_array2` 扩展，全部着色器翻译与管线构建均为 **0 失败**。
  - **现象**：开场动画正常显示、音乐正常播放、帧率约 50 fps、日志无任何报错或 `VK_ERROR_DEVICE_LOST`，但**只要 `gfx_shader.c` 放行 `"gs"` 阶段，进入主菜单后整个窗口（含 2D UI）即全黑**；此前未放行 `"gs"` 时（同套 `vk.h`/`vk_pipeline.c`/`vk_exec_draw.c` 补丁下）主菜单 UI 与网球 UI 均可正常显示。
  - 已排除扩展缺失、旧管线缓存、GPL 单/双描述符集混用（`TOTK_GPL=0`）与增量绑定（`TOTK_BIND_INC=0`），**尚未在黑屏菜单按 `F12` 抓帧定位具体是哪个 pass 变黑或哪一个 GS 程序触发了整屏涂黑**。

---

## 3. 代码改动台账（`opensport` 相对 `master`）

### 3.1 永久性修复与功能（已提交，保留）

| 模块 / 提交 | 涉及文件 | 改动内容 | 验证状态 |
|---|---|---|---|
| **蓝牙 Joy-Con (L) 与单柄镜像** | `runtime/input.c`<br>`runtime/input_joycon_win.c`<br>`runtime/ov_input.c` | • `joycon_driver_mode()==1` 时在 `unhidden_pad_present()` / `scan_pads()` / `motion_source()` 屏蔽 `057e:{2006,2007,2009}`，并在 `SDL_Init()` 前关闭 SDL Joy-Con/Switch HIDAPI hints（`jc_mode==2` 启用竖持），解决双重接管摇杆按键错乱。<br>• 补全 `STYLE_HANDHELD` 与 `gyro_scale` 档位选择；实现单柄竖持镜像与 `joycon_send_rumble()` 单柄回退。<br>• 清理 `last_kbtn` 日志刷屏；键盘/鼠标摇杆按向量模长较大者合并；`ov_hold_type()` 仅在模式变化时记日志。 | **用户实机验证完全正常** |
| **6×N 方形 `2DArray` 立方体视图** | `runtime/vk.h`<br>`runtime/vk_texture.c`<br>`runtime/vk_exec_draw.c` | • `VkTexImage` 新增 `cube_compat`；对未压缩且 `layers >= 6 && layers % 6 == 0` 的方形 `2DArray` 启用 `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`，并在 `get_view()` 放行。<br>• 恢复 `bind_pool_memory()` 简洁实现，移除 `c565001` 刷屏日志。 | 已合入，编译与抓帧正常 |
| **F12 按需帧转储** | `runtime/vk_dump.c`<br>`runtime/vk_backend.c`<br>`runtime/input_win.c` | 运行中按 F12 延迟 3 帧抓取下一帧，按需读取 `TOTK_DUMP_TEX` / `TOTK_DUMP_DIR`；不可写目录时仅记日志不再 `FATAL`。 | 已广泛用于 `dump_court*` 抓帧 |
| **预通道深度 `LEQUAL` 修复**<br>(`a1026d5`) | `runtime/vk_pipeline.c` | `depth_compare_op()` 中对 `dt && !dw && df == NVN_DEPTH_FUNC_EQUAL` 默认改为 `VK_COMPARE_OP_LESS_OR_EQUAL`（保留 `NSS_DIAG_DEPTH_EQUAL_STRICT=1` 回退开关）。 | **`dump_court3` 实测有效**：`pass_0015` 颜色数 40,176 → 334,250 |

### 3.2 临时诊断改动与开关（默认关闭，问题解决后必须全部清理）

> **注意**：所有新增 `NSS_DIAG_*` 均直接通过 `getenv()` 读取；**切勿**调用未在配置表注册的 `cfg_bool()`，否则启动即 `FATAL`。本轮排查间接绘制期间临时插入到 `vk_exec_draw.c` / `nvn_api_cmd.c` / `nvn_api_obj.c` / `nvn_api_core.c` 的统计钩子已全部通过 `git checkout` 还原。

| 文件 | 临时改动内容 |
|---|---|
| `runtime/vk_dump.c` | • `vk_dump_draw()` 导出完整 `pipe` 状态行与 `state` 字段。<br>• `MAX_ENTS` 临时提高至 `256`（防 `pass 0015` 管线记录截断）。 |
| `runtime/nvn_api_core.c` | `nvnDeviceSetWindowOriginMode()` 首次调用时用 `rt_log` 打印设置值。 |
| `runtime/vk_exec_draw.c` | • 静默回退单次日志：`fp_c1` 落到全零与视图不兼容绑定 1×1 占位图。<br>• `NSS_DIAG_DUMMY_BC6H=1`：非存储用途 `BC6H_UFLOAT` 视图替换为空占位图。 |
| `runtime/vk_pipeline.c` | `NSS_DIAG_DEPTH_EQUAL_STRICT` / `NSS_DIAG_DEPTH_LEQUAL` / `NSS_DIAG_DEPTH_ALWAYS` / `NSS_DIAG_NO_BLEND_R11` / `NSS_DIAG_FORCE_CULL0` / `NSS_DIAG_FLIP_FRONT` / `NSS_DIAG_DEPTH_CLAMP`。 |

### 3.3 当前工作区未提交改动（`NVN_STAGE_GS` 几何着色器接入，存在菜单黑屏待查）

| 文件 | 改动内容 | 状态 |
|---|---|---|
| `runtime/gfx_shader.c` | `reflect()`、`req_init()`、`reflection_valid()` 新增 `"gs"` ↔ `GFX_STAGE_GS` 映射与校验放行。 | 编译通过，全部 GS 翻译 0 失败 |
| `runtime/vk.h` | `VkPipeState` 新增 `const GfxShader *gs`。 | 编译通过（`KeyRec` 尺寸变化后自动重建 `pipeline-keys.bin`） |
| `runtime/vk_pipeline.c` | • `stage_bit()` 增加 `VK_SHADER_STAGE_GEOMETRY_BIT`；`GKey` 新增 `gs`、`Job` 新增 `gs_off`。<br>• `layout_covers()` 与 `create_graphics()` 支持 3 阶段（`VS + GS + FS`）、`spirv_rebind(gcode, gw, gs_off)` 与 `pin_subgroup32`；含 `s->gs` 时跳过 `spirv_add_outputs`、强制单描述符集（关闭 `P.indep`）、禁用 `gpl_link` 与 `keys_note`。 | 编译通过，管线构建 0 失败 |
| `runtime/vk_exec_draw.c` | • 定义 `NVN_STAGE_GS 2`，`c0` 缓存数组由 3 扩至 4；`nvn_stage_of()` / `shader_for()` 接入 `GFX_STAGE_GS`。<br>• `vk_c0_fill()` 与 `ssbo_slot()` 补齐 Maxwell GS `c0` SSBO 基址 `0x410`。<br>• `vk_exec_draw_writes()`、`prepare_draw()`、`shader_may_write_storage()` 补齐 `gs` 查找、等待、绑定与屏障追踪（含 GS 时跳过 `D->SM` 缓存）。 | 编译通过，但放行 GS 后进入菜单全黑 |
| `runtime/vk_device.c` | `create_device()` 启用 `VK_NV_geometry_shader_passthrough` 与 `VK_NV_viewport_array2` 设备扩展。 | 编译通过，开启后菜单仍黑屏 |

---

## 4. 问题画像与根因实证

### 4.1 主机环境

- **GPU 0（主力）**：NVIDIA GeForce GTX 1650（Turing，驱动 621.01，Vulkan 1.4.357）。
- **GPU 1（核显）**：AMD Radeon(TM) Graphics（`TOTK_GPU_INDEX=1` 卡在 Joy-Con 腕带确认界面）。
- 本机未安装 `VK_LAYER_KHRONOS_validation`。

### 4.2 磁盘可用帧转储（`E:\nssports\build\v0\`）

| 目录 | 帧号 / 基线 | 附加参数 | 用途与关键结论 |
|---|---|---|---|
| `dump/` | F2400（修复前菜单） | 全 Pass + 89 `.spv` | 菜单界面 `pass 0010` 存在 `2^k` 尾数台阶；`spirv-val --target-env vulkan1.3` 全部 89 个着色器 0 错误。 |
| `dump_ubo/` | F1900（修复前菜单） | 含 UBO 数值、128×128 贴图、管线行 | 验证 UBO `c3..c5`、cubemap 正常。 |
| `dump_court/` | F5188（修复前网球场） | 39 pass，168 MB | 修复前基线：`pass 0015` 地面全黑、看台梳状残影；`fp_c1` 未落全零。 |
| **`dump_court3/`** | **F3671（`a1026d5` 基线）** | 无诊断开关 | **修复前对比主基线**：草地/白线/球网恢复，但天空/球场主面/看台/近处 3 人极暗或全黑。 |
| `dump_court4/` | F3629（`a1026d5`） | `NSS_DIAG_DEPTH_ALWAYS=2`, `TOTK_DUMP_UBO=64` | `pass 0015` 仅 0.8% 像素变化，证伪预通道剔除漏像素。 |
| `dump_court5/` | F4115（`a1026d5`） | `NSS_DIAG_DUMMY_BC6H=1`, `TOTK_DUMP_TEX=1474,7207,7698` | 证伪 `BC6H` 全零。 |
| `dump_court6/` | F4362（`a1026d5`） | `NSS_DIAG_FORCE_CULL0=1`, `NSS_DIAG_DEPTH_CLAMP=1`, `TOTK_DUMP_UBO=64` | 证伪 `cull 2` 与 `z==w` 裁剪，验证 `vp`/`sc`/`vattr` 全正常。 |

### 4.3 `dump_court3`（F3671）异常 vs 正常对照

- **近黑角色 vs 正常 `far_local`**：`fe507b68`（`pass 0015 pipe 38/51`，近处 3 名黑角色）与 `b086765d`（`pass 0017 pipe 2`，正常远端角色）绑定了**完全相同的角色贴图**（`tex 7715..7719`）、**完全相同的 cubemap**（`tex 7197`/`3259`）、**完全相同的 `vattr` 布局**，且 `vs ubo c3/c6` 与 `fs ubo c3/c4/c5` 除 `c4[26].x` 外 100% 一致。
- **极简对照组——天空球 `pipe 90`（`vs da456e02` + `fs b77336bf`）**：无 `fp_c1`、无光照/阴影/cubemap、`OpKill` 数为 0，仅采样 `tex 1474`（BC6H），在未接入 GS 前仍全黑。

### 4.4 根因实证链（间接绘制全零 → 几何着色器缺失）

1. **直接绘制 vs 间接绘制分水岭**：对 `op_draw_idx`、`op_draw_idx_ind`、`op_dispatch` 及 `nvnCommandBufferDrawElementsIndirect` 插桩实测：
   - 正常显示的远端角色 `b086765d` 走直接绘制 `vkCmdDrawIndexed`（`cnt=84`）。
   - 黑掉的天空球 `b77336bf`（`ind=0x1b50e7000`, `ibuf=0x1b50e7014`）与近处角色 `fe507b68`（`ind=0x1b12c2b40`, `ibuf=0x1b12c2b54`）均走间接绘制 `vkCmdDrawIndexedIndirect`（`op_draw_idx_ind`）。
2. **间接命令与索引缓冲 100% 全零**：在 `TOTK_MEMORY=import`（`cls=3`, `CPU_CACHED`, `alloc=0`）下统计，**`250,001 / 250,001`** 次 `DrawElementsIndirect` 的间接命令全为 `cmd=(0,0,0,0,0)`、索引缓冲全为 `ib=0,0,0,0,0,0`（到 `hit=120` 仍全零）。
3. **追踪间接缓冲写入者**：
   - 网球整帧内 **0 次** compute dispatch（全会话仅有 2 个 `1x1x1` compute shader：`ff007f3a8722fb2f`、`fe6114fff275fab4`），排除 CS 写入。
   - 静态回溯 `nvnCommandBufferDrawElementsIndirect` 调用者 `lr=0x700192e2dc` → `generated/v0/main/main_0227.c`（`f_700192e0c0`, `L_700192e248`）→ `f_700192e930`（将 `ind` 缓冲作为 SSBO 绑定到 NVN stage 0/1/2/5）→ `main_0237.c:9911`（Z-prepass 先通过 `f_700192e930` 绑定 SSBO，再调用 `f_700027747c` 直接绘制）。
4. **确认游戏大量使用 Passthrough GS**：
   - `nvnProgramSetShaders` 实测确认游戏注册了大量 `n=3, stage_mask=0x7`（`VS | FS | GS`）程序。
   - 反汇编缓存中的 GS SPIR-V（`03ded194f0c233ac2fdcfe5af7edc6b55cd8f11008c9e5555b8ae079932c71e7.res.json`，key `028be6c0572886187c35db0a5734140043e38a2d1688215be8f7ea87d3912f91`）：
     - 反射：`stage="gs"`、`notes=[]`、`cbufs=[{binding:3, slot:0}, {binding:2, slot:3}]`、`sbufs=[{binding:1, slot:0, cb_slot:0, cb_offset:264, flags:1}]`（`264 * 4 = 0x420 = 0x410 + 16 * 1`，确认为 Maxwell GS `c0` SSBO 槽 1 写操作）。
     - SPIR-V 能力与扩展：`Geometry`、`GeometryShaderPassthroughNV`、`ShaderViewportMaskNV`、`OpExtension "SPV_NV_geometry_shader_passthrough"`、`OpExtension "SPV_NV_viewport_array2"`；执行模式 `Triangles`、`Invocations 1`、`OutputTriangleStrip`、`OutputVertices 3`；接口含 `%31`（`BuiltIn ViewportMaskNV`）、`%36`（`PassthroughNV Location 1`）、`%gl_in`（`PassthroughNV`）和 `%54`（`gl_PerVertex_0` Output）。

---

## 5. 已证伪 / 已排除路径总表（切勿重复测试）

### 5.1 原“网球场黑天空/黑球场/黑角色”阶段已排除项

| 怀疑方向 | 验证手段与对应数据 | 结论 |
|---|---|---|
| **Z-prepass `LEQUAL` 仍漏剔除** | `dump_court4`（`NSS_DIAG_DEPTH_ALWAYS=2`） | **已证伪**：`pass 0015` 仅 0.8% 像素变化，黑区域不变。 |
| **Window Origin / 正反面朝向反转** | `totk.log` + `NSS_DIAG_FLIP_FRONT=1` | **已证伪**：`DeviceSetWindowOriginMode(1 -> upper left)`，反转导致整屏全黑。 |
| **背面剔除 `cull 2` 与 `z==w` 远平面裁剪** | `dump_court6`（`NSS_DIAG_FORCE_CULL0=1` + `NSS_DIAG_DEPTH_CLAMP=1`） | **已证伪**：天空/球场/看台/3 黑角色依然全黑。 |
| **`BC6H_UFLOAT` 贴图全零或 BNTX 布局错误** | `dump_court5`（`NSS_DIAG_DUMMY_BC6H=1`）+ 解包 `RegularMenu.bkres.zs` | **已证伪**：禁用 BC6H 后外围草坪与看台明显掉暗，`tex 1474` 含真实非零数据。 |
| **Cubemap / UBO 常量数组异常** | `dump_ubo/` + `diag_fsubo.txt` + `shader-programs.bin` 静态检查 | **已证伪**：`fe507b68` 与正常 `b086765d` 的 `vs/fs ubo c3..c6`（除 `c4[26].x`）与贴图完全一致；726 个程序全部 `c1_off % 64 == 0`。 |
| **VS/FS SPIR-V 缺陷 / 接口不匹配 / FP64 / TXD / Interlock** | `spirv-val --target-env vulkan1.3` + 7 组关键对反汇编 | **已证伪**：89 个 `.spv` 0 错误，7 组关键对全部无缺失输出或未初始化变量。 |
| **管线预热失败 / `fp_c1` 静音落零 / 视图回退 / `vp`/`sc`/`vattr` 错误** | `totk.log` + `dump_court` + `dump_court6/passes.txt` | **已证伪**：预热 0 失败，`fp_c1` 0 落零，`vp`/`sc`/`vattr` 全正常。 |
| **混合模式 / 逻辑运算错误** | `NSS_DIAG_NO_BLEND_R11=1` + 管线日志 | **已证伪**：`pass 0010/0015` 本身 `blend=0, lop=3(COPY)`。 |
| **GPL / 纹理别名 / 屏障合并 / 动态顶点 / 管线缓存 / `BindUniformBuffersDeferred`** | 实测 `TOTK_GPL=0`、`TOTK_TEX_ALIAS=0`、`TOTK_BARRIER_MERGE=0`、`TOTK_VK_DYN_VERTEX=0`、`TOTK_PIPELINE_CACHE=0` 及日志统计 | **已排除**：均无改善；`BindUniformBuffersDeferred` 调用次数为 0。 |
| **Compute Shader 生成间接绘制缓冲** | 插桩统计 `op_dispatch` | **已证伪**：网球帧内 0 次 compute dispatch，全会话仅 2 个 `1x1x1` CS。 |
| **禁用 Passthrough GS 能力诱导游戏回退** | `NSS_DIAG_NO_PTGS=1`（将 `NVN_DEVICE_INFO_SUPPORTS_PASSTHROUGH_GEOMETRY_SHADERS = 0x42` 返回 0） | **已证伪**：游戏不回退，仍然绑定并依赖 GS。 |

### 5.2 新“接入 GS 后主菜单全黑”阶段已排除项

| 怀疑方向 | 验证手段与对应数据 | 结论 |
|---|---|---|
| **Vulkan 设备丢失（`VK_ERROR_DEVICE_LOST`）或呈现链路损坏** | 实机观察 + `logs\totk.log` | **已排除**：开场动画（`ov_movie`）显示完全正常，菜单音乐正常播放，帧率 50.7 fps，日志无报错、无管线构建失败。 |
| **未开启 `VK_NV_geometry_shader_passthrough` / `VK_NV_viewport_array2` 扩展或旧管线缓存污染** | 在 `runtime/vk_device.c` 启用两项扩展 + `TOTK_PIPELINE_CACHE=0` 重测 | **已排除**：扩展生效且跳过旧缓存后，菜单依然全黑。 |
| **GPL 单/双描述符集布局切换（`n_sets=1` vs `2`）或增量绑定串台** | 实测 `TOTK_GPL=0` + `TOTK_BIND_INC=0` | **已排除**：强制全管线单描述符集并关闭增量绑定后，菜单依然全黑。 |
| **非 GS 代码补丁引入的回归** | 对照 `reflection_valid()` 修改前后的两次运行 | **已排除**：在完全相同的 `vk.h`/`vk_pipeline.c`/`vk_exec_draw.c` 补丁下，仅当 `gfx_shader.c` `reflection_valid()` 放行 `"gs"` 后才出现黑屏；未放行时（GS 绘制被跳过）主菜单与网球 UI 均正常显示。 |

---

## 6. 下一步排查路线（接手直接从这里开始）

### P0：零编译 F12 抓帧，定位菜单变黑的确切 Pass（耗时约 30 秒）

在 `E:\nssports\build\v0` 运行游戏，等开场动画播完进入“黑屏但有音乐的菜单”后按 `F12`，等 2 秒关掉窗口，然后单独执行日志筛选命令检查 `.\dump\passes.txt`：

1. **启动并按 F12 抓帧**：
   ```powershell
   Remove-Item Env:TOTK_*,Env:NSS_DIAG_* -ErrorAction SilentlyContinue; Remove-Item .\dump -Recurse -Force -ErrorAction SilentlyContinue; .\nssports.exe
   ```
2. **关掉游戏后查看各 Pass 颜色均值与异常（区分 3D 染黑/NaN 还是 UI 未绘制）**：
   ```powershell
   Select-String -Path .\dump\passes.txt -Pattern "^pass |^\s+c0 |^\s+d0 |^present|RGB|NaN|draws" | ForEach-Object { $_.Line }
   ```

### P1：按 F12 抓帧结果定点排查 GS 执行问题

1. **核查 GS 反汇编函数体与 `ViewportMaskNV`（`%31`）**：此前仅核对了 `diag_gs_spv.txt` 头部声明，尚未检查函数体是否写入 `ViewportMaskNV`（若为 0 则光栅化全部丢弃）或向 SSBO 写入了越界/错误偏移数据；同时核对 `tools/xlate/Program.cs` 中 `Accessor` 对 Passthrough GS / ViewportMask 的能力声明。
2. **核查 `VS → GS` 接口补齐**：当前 `vk_pipeline.c` 在 `s->gs != NULL` 时跳过了 `spirv_add_outputs`；需确认 `diag_gs_spv.txt` 声明的 `PassthroughNV Location 1` 等输入是否在对应 VS 中全部存在。
3. **若 `passes.txt` 显示整帧被某个特定 GS 破坏（如 HDR `NaN` 污染自动曝光/合成或 SSBO 误写）**：在 `vk_exec_draw.c` 按 `prog->serial` 或 GS 哈希做开关二分，先放行单一/部分 GS 程序定位罪魁祸首，再核查其 SSBO 绑定槽（`c0 + 0x410`）与同步屏障。

### P2：修复完成后的收尾清单

1. 验证 `DrawElementsIndirect` 缓冲非零，实机复验网球场（天空球/球场主面/看台/近处 3 名角色）、主菜单 3D 背景与顶部幕墙彩色流纹。
2. 清理第 3.2 节中全部临时诊断代码与 `NSS_DIAG_*` 开关，并清理 `E:\nssports\build\v0\dump*` 临时目录。
3. 执行全量/增量编译并提交干净的 `NVN_STAGE_GS` 修复补丁。
