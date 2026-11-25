# NS Sports Recomp

[English](README.md) | [简体中文](README.zh-CN.md)

**NS Sports Recomp**（`nssports`）是一个将 *Nintendo Switch Sports*（Nintendo Switch，美版 v0）静态重编译为 Windows x86-64 原生可执行程序的实验性移植项目。

> [!IMPORTANT]
> **本仓库不包含任何游戏文件。** 你必须使用自己合法购买并从自有主机提取的 *Nintendo Switch Sports*（美版 v0）游戏本体、自有的 `prod.keys` 以及 Switch Mii 系统固件资源；本项目不提供任何获取上述文件的教程或途径。
>
> 游戏可执行程序是在你的本地机器上根据你自己的文件构建而成的，因此构建产物本身包含游戏内容。**构建产物仅限你个人使用，严禁向他人分享或分发编译后的可执行文件及提取出的游戏文件。**
>
> 本项目为独立的非商业技术研究项目，与任天堂株式会社（Nintendo Co., Ltd.）没有任何关联，亦未获得其认可或赞助。*Nintendo Switch Sports*、*Nintendo Switch*、*Joy-Con* 和 *Mii* 均为任天堂的注册商标。本项目不支持在线联机 / Nintendo Switch Online 服务。

## 当前状态

早期开发与持续移植阶段。目前在 Windows + Vulkan 下可正常跳过腕带提示与开场动画并进入主广场（Spocco Square），支持实体 Joy-Con (L/R) 单人及本地双人游玩（2P 使用访客账户）。画面细节、开场视频画面及各具体运动项目的局内适配仍在持续完善中。

## 已实现特性

- 原生 x86-64 静态重编译代码，而非传统模拟器
- Vulkan 渲染后端：支持 `VK_NV_geometry_shader_passthrough` + `VK_NV_viewport_array2`、无锁 16-bit 原子改写以及间接绘制命令头清零顶点着色器模拟
- 可配置 **3D 渲染比例**（`50%`–`200%`）、**AMD FSR 1** / 双线性升频、锐度调节、色彩预设、HDR10 输出以及 `F12` 按需单帧转储
- Windows 原生蓝牙 / USB HIDAPI **Joy-Con (L)**、**Joy-Con (R)** 与 **Pro 手柄**驱动，支持每支手柄独立的六轴体感融合与 HD 振动
- 本地**双人游玩**支持：支持自动单手柄拆分（`auto` / `dual` / `single` 配对模式）与玩家指示灯（Player LED）自动刷新
- 离线 `nn::account` 与 `nn::mii` 数据库支持
- 游戏内悬浮设置菜单（`ESC` / `F1`）与性能监控浮层（`F10`）
- 立体声与 5.1 环绕声输出

## 已知限制

- H.264 开场过场动画目前解码帧数为 `0`（可正常通过并进入主菜单）。
- 本地双人模式已通过访客账户实机验证（通过 `recomp/patches-v0.txt` 中的 `null_guard_2p` 补丁规避空指针）。
- 各具体运动项目仍在逐一实机测试与完善中。

## 构建步骤（Windows）

环境要求：Windows 10/11（x86-64）、支持 Vulkan 1.3 的显卡、蓝牙适配器（用于无线连接 Joy-Con）、MSYS2/Clang/Ninja 工具链、`.NET` SDK（用于着色器转译）以及 Python `.venv`。

1. 将你合法提取的基础版 NSP 放入 `rom/` 目录，将 `prod.keys` 放入 `keys/` 目录（这两个目录均已被 `.gitignore` 忽略）。
2. 在 `config.local.toml` 中配置基础版 NSP、密钥、固件 Mii 目录及本地工具链路径。
3. 执行完整构建（将 `TEMP`/`TMP` 指向 E 盘以避免向 C 盘写入构建缓存）：
   ```powershell
   $env:TEMP="E:\nssports\build\tmp"; $env:TMP="E:\nssports\build\tmp"
   E:\nssports\.venv\Scripts\python.exe tools/build_game.py --version v0 --jobs 8
   ```
4. 首次生成后，仅修改 `runtime/` C/C++ 代码时可执行增量编译：
   ```powershell
   $env:TEMP="E:\nssports\build\tmp"; $env:TMP="E:\nssports\build\tmp"
   E:\dkb\DKB-Setup\bin\ninja.exe -C E:\nssports -f E:\nssports\build\v0\build.ninja
   ```

## 运行与快捷键

1. 通过 Windows 蓝牙将 **Joy-Con (L)** 和/或 **Joy-Con (R)** 配对连接到电脑。
2. 运行 `build/v0/nssports.exe`：
   ```powershell
   cd E:\nssports\build\v0
   .\nssports.exe
   ```
3. **快捷键**：
   - `ESC` / `F1`：打开或关闭游戏内设置菜单（可调整 Joy-Con 配对模式、陀螺仪校准、3D 渲染比例、FSR、音频设备等）
   - `F10`：切换性能监控浮层（关闭 / 仅 FPS / 完整信息）
   - `F11`：切换全屏 / 窗口模式
   - `F12`：触发单帧渲染通道与纹理转储

## 致谢与第三方声明

所引用或适配的开源组件许可详情请见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
本项目基于 [totk-pc-recomp](https://github.com/valentderah/totk-pc-recomp) / MacNativeGames（MIT）、`nxnative`（MIT）与 `Ryujinx.Graphics.Shader`（MIT）的静态重编译运行时与工具链架构构建。
