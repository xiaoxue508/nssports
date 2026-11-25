# opensport 交接文档（当前分支入口）

> **项目**：*Nintendo Switch Sports*（Title ID `0100D2F00D5C0000`，USA v0）Windows x86-64 静态重编译
> **工作区**：`E:\nssports` ｜ **当前分支**：`opensport`（基线 `master` = `f7a84fc`）
> **远程仓库**：`https://github.com/xiaoxue508/nssports`（public）
> **当前产物**：`E:\nssports\build\v0\nssports.exe`
> **更新日期**：2026-11-25

---

## 1. 协作、构建与省 Token 红线（下任 Agent 务必严格遵守）

1. **严禁自行启动 `nssports.exe`**（无论前台还是后台）。需要实机测试时，提供**单行可复制的 PowerShell 命令**（需包含 `Remove-Item Env:...` 清理残留变量），由用户在 `E:\nssports\build\v0` 已打开的终端中亲自执行；**查日志命令与启动 `.\nssports.exe` 命令必须分开提供**（启动游戏会实时刷新日志把前面的查询输出冲掉）。
2. **严禁向 C 盘写入构建缓存或临时文件**：构建前必须将临时目录指向 E 盘：
   ```powershell
   $env:TEMP="E:\nssports\build\tmp"; $env:TMP="E:\nssports\build\tmp"; E:\nssports\.venv\Scripts\python.exe tools/build_game.py --version v0 --jobs 8
   ```
   - 若仅改动 `runtime/`，增量编译命令（任意目录均可执行）：
   ```powershell
   $env:TEMP="E:\nssports\build\tmp"; $env:TMP="E:\nssports\build\tmp"; E:\dkb\DKB-Setup\bin\ninja.exe -C E:\nssports -f E:\nssports\build\v0\build.ninja
   ```
3. **文件规范**：`core.autocrlf=false`，源码与文档保持 **LF** 换行；**严禁手改 `generated/`**。向 PowerShell 传多行或含嵌套引号的 Python 脚本时，一律使用 here-string（`@' ... '@ | python -`）或 Base64 解码执行。
4. **省 Token 与看图硬性规则（极重要，违背会极快烧干上下文）**：
   - **能用数值统计，绝不直接读图**：禁止直接读取全分辨率（`1600x900` / `1920x1080`）截图或多张 `pass_*.png`。任何像素均值、有效色数、全黑占比、`NaN`/Inf 检测、坐标 RGB、帧间差异率等，**一律先用 Python（`PIL`/`numpy`）输出 ≤30 行文本摘要**。
   - **非看图不可时，只看 1 张裁剪/缩小拼图**：必须看视觉效果时，先用 Python 将关注局部裁剪并缩放到 `≤800x450`，**只读这一张图**；结论写入文档后提醒用户适时新开短会话（或 `/compact`），避免图片滞留对话历史中在后续每轮重复计费。
   - **禁止大文件盲读与全库扫描**：查 `runtime/` 代码一律先用 `git grep -n` 定位行号，再按 `StartLine/EndLine` 分段只读相关函数（≤150 行），禁止整文件读 `vk_exec_draw.c` / `vk_pipeline.c`；**严禁递归扫描 `generated/`、`build/v0/shader-cache/`、`tools/vendor/`**——需要长链条回溯（如 `generated/v0/main_*.c`）时，优先派发只读 `research` 子代理调研并只带回精简结论。
   - **日志与命令输出必须裁剪**：查看 `totk.log`、`passes.txt` 或目录列表时，必须配合 `Select-String` / `Select-Object -First 40` 或 Python 汇总。
   - **少轮次、先规划再动大文件**：跨文件改动前先列简明方案对齐再编码；单次实机测试尽量一次收集齐所需诊断日志，减少碎片化来回。SPIR-V 反汇编工具位于 `E:\nssports\toolchain\msys64\ucrt64\bin\spirv-dis.exe`。

---

## 2. 当前代码库实际状态与已解决问题

1. **Joy-Con (L) 蓝牙直连与单柄竖持镜像修复**（源自 `94b3575`，实机验证正常）：
   - [input.c](runtime/input.c)：屏蔽 `joycon_driver_mode()==1` 下的 `057e:{2006,2007,2009}` 并关闭冲突的 SDL Joy-Con HIDAPI hints。
   - [input_joycon_win.c](runtime/input_joycon_win.c)：补全 `STYLE_HANDHELD`、`gyro_scale` 选择、单柄镜像（`ZL↔ZR`、十字键↔`ABXY`、`L↔R`、`MINUS↔PLUS` 及摇杆）与振动回退。
   - [ov_input.c](runtime/ov_input.c)：按摇杆向量模长合并键鼠/手柄输入，`ov_hold_type()` 仅在模式变化时记日志。
2. **按腕带“下一步”进入 H.264 开场视频闪退（已解决闪退）**：
   - [movie.h](runtime/movie.h)、[ov_movie.c](runtime/ov_movie.c)：补全主机侧 OpenH264（`decoder type 1 mode 1 format 4`）解码路径并移除 `Initialize()` 后的多线程设置崩溃，现可顺利跨过开场视频并稳定进入运动选择菜单。
   - **遗留小问题**：`totk.log` 显示 `csd 0 bytes` / `ds=0x10`（开场视频当前解码 `0 frames`），可后续补齐 H.264 SPS/PPS 提取以正常显示开场动画画面。
3. **运动选择界面（Spocco Square）3D 背景渲染错乱与黑屏挂起（已彻底解决，默认 `TOTK_MEMORY=auto` 实机验证完全正常）**：
   - **Passthrough Geometry Shaders（`NVN_STAGE_GS = 2`）**：在 [vk.h](runtime/vk.h)、[vk_device.c](runtime/vk_device.c)、[gfx_shader.c](runtime/gfx_shader.c)、[vk_pipeline.c](runtime/vk_pipeline.c)、[vk_exec_draw.c](runtime/vk_exec_draw.c) 补全 `VK_NV_geometry_shader_passthrough` + `VK_NV_viewport_array2` 支持，使几何着色器剔除阶段（如 `pass 0003`）能向后续 `DrawElementsIndirect` 的间接命令缓冲区与索引缓冲区写入剔除后三角形。
   - **无锁 16-bit 原子改写（[vk_pipeline.c](runtime/vk_pipeline.c)）**：将 Ryujinx 生成的 49 指令 `float`-bitcast `OpAtomicCompareExchange` 16-bit 索引写入 CAS 循环改写为 19 指令无循环无锁 `OpAtomicAnd` + `OpAtomicOr` 并为目标 SSBO 添加 `Coherent` 修饰。
   - **间接命令头清零 VS 模拟（[gfx.h](runtime/gfx.h)、[gfx_shader.c](runtime/gfx_shader.c)、[vk_exec_draw.c](runtime/vk_exec_draw.c)）**：`pass 0000` 顶点着色器从 VS SSBO 0 读取 64 位地址并用全局内存写（`STG`）将约 310 个 `DrawIndexedIndirect` 命令头初始化为 `(0, 1, 0, 0, 0)`；由于地址并非直接来自 `c0` SSBO 槽位，Ryujinx `GlobalToStorage` 将其降级为空 `OpReturn`。现于反射阶段精确识别该着色器（`detect_indirect_clear_vs`），并在 `op_draw` 中通过 `try_indirect_clear_draw` 以批量 `vkCmdCopyBuffer`（配合 `VK_MEM_USE`、`vk_mem_gpu_wrote` 及前后 `vk_exec_barrier`）完成初始化。
4. **F12 按需帧转储**：
   - [input_win.c](runtime/input_win.c)、[vk_dump.c](runtime/vk_dump.c)、[vk_backend.c](runtime/vk_backend.c)、[vk_exec.h](runtime/vk_exec.h)、[vk_exec_draw.c](runtime/vk_exec_draw.c)：支持在游戏中按 `F12` 触发单帧 `passes.txt` + `images.txt` + PNG 转储（含 `gs` 与 `ind` 状态）。
5. **双人 Joy-Con 自动拆分与 2P 档案空指针保护**（实机访客双人游玩正常）：
   - [input_joycon.h](runtime/input_joycon.h)、[input_joycon_win.c](runtime/input_joycon_win.c)、[ov_input.c](runtime/ov_input.c)：新增 `joycon_set_game_single_hint()`，在 `TOTK_JOYCON_MODE=auto` 下响应 `SetNpadJoyAssignmentModeSingle` 与 `ShowControllerSupport`，自动将 L/R Joy-Con 拆分为 1P/2P 并刷新 Player LED；支持 `refresh_cfg()` 动态切换模式。
   - [patches-v0.txt](recomp/patches-v0.txt)：保留原有 spin waits 并追加 `null_guard_2p`（`main+0xbe887c` 入口判空 `x1 == 0` 跳转 `L_7000be88d8`），避免 2P 越界档案查询空指针崩溃（使用访客账户可正常双人游玩）。
6. **窗口标题、输入与 ESC 菜单 NS Sports 适配**：
   - [gfx_window_win.c](runtime/gfx_window_win.c)：窗口标题改为 `"Nintendo Switch Sports"`；[ui_text.c](runtime/ui_text.c) HUD 顶部版本文案改为 `"NS Sports Recomp"`。
   - [input.c](runtime/input.c)、[input_win.c](runtime/input_win.c)：移除原 TotK 键鼠映射逻辑，取消启动后 `ClipCursor` 强制锁鼠标。
   - [menu.cpp](runtime/ui/menu.cpp)、[ui_text.h](runtime/ui_text.h)、[ui_text.c](runtime/ui_text.c)：在 Controls 页加入 `TOTK_JOYCON_MODE` 与 `TOTK_JOYCON_DRIVER` 选项，移除 TotK 专属项（草地/阴影/FOV/HUD 宽度/究极手/键鼠绑定/按键布局/Xbox 扳机振动/音频分轨音量等）。
7. **3D 渲染比例（`TOTK_RENDER_SCALE`）接通与 HUD 显示修正**：
   - [vk.h](runtime/vk.h)、[vk_exec.h](runtime/vk_exec.h)、[vk_texture.c](runtime/vk_texture.c)、[vk_exec.c](runtime/vk_exec.c)、[vk_exec_draw.c](runtime/vk_exec_draw.c)、[vk_exec_xfer.c](runtime/vk_exec_xfer.c)：将 Vulkan 后端 `scale` / `pass_scale` 扩展为浮点系数，按 `TOTK_RENDER_SCALE` 缩放 GPU 16:9 3D 渲染目标及对应 `Viewport` / `Scissor` / 区域拷贝，保持 `rgba8srgb` 交换链输出为原生尺寸。
   - [nss_stubs.c](runtime/nss_stubs.c)：修正 `res_wanted()` 返回游戏实际输出（Docked `1920x1080` / Handheld `1280x720`），`res_sizes()` 按 `TOTK_RENDER_SCALE` 计算并反馈 HUD `3D 场景` 分辨率。
   - 注：运行日志位于 `E:\nssports\build\v0\logs\totk.log`。

---

## 3. 后续可选工作

1. **各运动项目实机验证**：在默认配置下进入排球、羽毛球、保龄球、足球、击剑、网球等具体运动，验证局内 3D 渲染、阴影/反射、帧率与稳定性。
2. **H.264 开场视频画面补齐**：排查 [ov_movie.c](runtime/ov_movie.c) 中 `csd 0 bytes` / `ds=0x10` 的 SPS/PPS 传递问题，使开场动画视频可见播放。
