# opensport 交接文档（当前分支入口）

> **项目**：*Nintendo Switch Sports*（Title ID `0100D2F00D5C0000`，USA v0）Windows x86-64 静态重编译
> **工作区**：`E:\nssports` ｜ **当前分支**：`opensport`（基线 `master` = `f7a84fc`）
> **远程仓库**：`https://github.com/xiaoxue508/nssports`（public）
> **当前产物**：`E:\nssports\build\v0\nssports.exe`（176.3 MiB，462 目标文件编译链接通过，0 错误）
> **更新日期**：2026-11-25

> **历史踩坑归档**：此前已排查过的图形渲染根因（预通道深度 `LESS_OR_EQUAL`、`2DArray` 立方体视图、`DrawElementsIndirect` 全零 / `NVN_STAGE_GS` 缺失等）及已证伪假设，已全部拆分至 [HISTORY_NOTES.md](HISTORY_NOTES.md)。**注意：该归档中除 Joy-Con 外的代码改动均已从当前分支移除，仅作经验与避坑参考，切勿误以为当前代码库仍包含那些补丁。**

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

## 2. 当前代码库实际状态

当前 `opensport` 分支相对于 `master`（`f7a84fc`）**仅包含两项提交**（本地与 `origin/opensport` 完全一致）：

1. **Joy-Con (L) 蓝牙直连与单柄竖持镜像修复**（源自 `94b3575`，用户实机验证正常）：
   - `runtime/input.c`：屏蔽 `joycon_driver_mode()==1` 下的 `057e:{2006,2007,2009}` 并关闭冲突的 SDL Joy-Con HIDAPI hints。
   - `runtime/input_joycon_win.c`：补全 `STYLE_HANDHELD`、`gyro_scale` 选择、单柄镜像（`ZL↔ZR`、十字键↔`ABXY`、`L↔R`、`MINUS↔PLUS` 及摇杆）与振动回退。
   - `runtime/ov_input.c`：按摇杆向量模长合并键鼠/手柄输入，`ov_hold_type()` 仅在模式变化时记日志。
2. **本交接文档与历史归档**（`HANDOVER.md`、`HISTORY_NOTES.md`）。

除此之外的所有历史改动均已回滚删除。

---

## 3. 当前首要问题（最新编译实测）

- **现象**：启动新编译的 `build\v0\nssports.exe`，可正常进入“请将腕带系在 Joy-Con 上”确认界面；**按下手柄“下一步”按钮后程序直接闪退**。
- **日志直接死因**（已从 `E:\nssports\build\v0\logs\totk.1.log` 确认）：
  ```text
  [host] movie: decoder type 1 mode 1 format 4 left to the library (only VP9 in buffer mode runs on the host)
  FATAL: svc #0x26 unknown at sdk+0x3ccf40: Break (the SDK aborted)
  ```
- **原因定位**：确认腕带界面点击“下一步”后，游戏立即启动开场视频解码（`decoder type 1 mode 1 format 4`，即 **H.264 / AVC** 视频格式）。当前基线 `f7a84fc`（源自 `totk-recomp`）的 `runtime/ov_movie.c` **只实现了 buffer 模式的 VP9**，遇到 H.264 时将其交给 guest `nn::movie` 库处理，而 guest 侧因缺少对应服务/实现直接触发 `svc #0x26 (Break)` 崩溃。
- **环境提示**：构建链已默认链接 `-lopenh264` 且 `build\v0\libopenh264-8.dll` 已就绪（`tools/build_game.py` 的 `SDL3_DLLS` 与 `LINK_LIBS` 中均已配置），可直接在干净的 `runtime/ov_movie.c` + `runtime/movie.h` 中实现 H.264/AVC 解码以跨过开场视频。

---

## 4. 建议推进顺序

1. **优先修复过场/开场视频闪退**：先查看 `runtime/ov_movie.c` 与 `runtime/movie.h`，补全 H.264 (`format 4` / `type 1`) 的主机解码路径（OpenH264），确认能顺利播完开场动画并稳定进入主菜单。
2. **进入主菜单与 3D 球场后，再按优先级处理历史已定位的渲染问题**（详见 [HISTORY_NOTES.md](HISTORY_NOTES.md)）：
   - 先复核并干净补回 `2DArray` 立方体视图兼容（6×N 层未压缩方形 2DArray）与 Z-prepass `LESS_OR_EQUAL`；
   - 再针对 `DrawElementsIndirect` 全零导致的黑天空/黑球场/黑看台/黑角色问题，从头干净设计 `NVN_STAGE_GS`（Passthrough GS）实现，并规避此前“直接放行全量 GS 导致进入菜单全黑”的踩坑。
