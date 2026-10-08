# 在 Linux 上构建与运行 BedrockMap

本文档说明 BedrockMap 在 Linux（x86_64，X11 / Wayland）上的构建、运行、安装与打包方式，
并列出本次移植涉及的代码与构建系统改动。

## 1. 环境要求

| 依赖 | 版本 | 说明 |
| ---- | ---- | ---- |
| Qt 6 | ≥ 6.5（本机验证：6.11.2） | 需要 Core / Widgets / Concurrent / OpenGL / OpenGLWidgets / Svg / Network |
| CMake | ≥ 3.16（本机验证：4.4.3） | 构建系统 |
| Ninja | 任意 | 可选，未安装时退化为 Make |
| C++ 编译器 | 支持 C++20 | GCC 16.2 / Clang 22 |
| zlib | 任意 | leveldb-mcpe 依赖 |
| libdeflate | 任意（本机验证：1.26） | bedrock-level 的 `LibdeflateCompressorRaw` 需要；见第 5 节说明 |
| git | 任意 | 拉取子模块与 leveldb-mcpe |
| Qt LinguistTools | 可选 | 提供 `lupdate` / `lrelease`；缺失时见下文回退方案 |

Arch / CachyOS：

```bash
sudo pacman -S --needed base-devel cmake ninja git zlib libdeflate \
    qt6-base qt6-svg qt6-tools
```

Debian / Ubuntu：

```bash
sudo apt install build-essential cmake ninja-build git zlib1g-dev libdeflate-dev \
    libgl1-mesa-dev qt6-base-dev qt6-svg-dev qt6-tools-dev qt6-tools-dev-tools
```

### 1.1 免 root 工具链（本机采用）

本机没有 sudo 权限且缺少 cmake / ninja / lrelease，可在用户目录准备一个工具链虚拟环境，
`scripts/build.sh` 会自动使用它（PATH 中已有的工具优先）：

```bash
python3 -m venv ~/.local/venvs/bedrockmap
~/.local/venvs/bedrockmap/bin/pip install cmake ninja PySide6-Essentials
```

- `cmake` / `ninja` 由 PyPI wheel 提供；
- `PySide6-Essentials` 提供 `pyside6-lrelease` / `pyside6-lupdate`，用于在缺少
  Qt LinguistTools 时编译 `translations/*.ts`（版本与系统 Qt 一致时最稳妥，本机均为 6.11.2）。

可通过 `BEDROCKMAP_TOOLCHAIN_BIN` 指向其他位置的工具链目录。

## 2. 构建

```bash
git clone --recursive https://github.com/bedrock-dev/BedrockMap.git
cd BedrockMap

./scripts/build.sh              # Debug   -> build/
./scripts/build.sh --release    # Release -> build_rls/
./scripts/build.sh --clean      # 先删除构建目录
./scripts/build.sh --lupdate    # 先刷新 translations/*.ts（会修改被跟踪文件）
./scripts/build.sh --run        # 构建后直接运行
./scripts/build.sh -j 8         # 限制并行任务数
```

首次 configure 时会自动准备 leveldb-mcpe：
若存在 `third/leveldb-mcpe` 源码目录就直接作为子项目编译，否则从固定上游 commit 下载
（`cmake/LevelDbMcpe.cmake`），并套用 `cmake/patches/leveldb-mcpe-linux.patch`
把它编译成静态库（上游默认生成共享库、且依赖仅有 Windows 才存在的 `zlibstatic` 目标）。

## 3. 运行

```bash
./scripts/run.sh              # Debug
./scripts/run.sh --release    # Release
./scripts/run.sh --x11        # 强制使用 X11/XWayland 后端
BedrockMap /path/to/world     # 直接打开某个世界目录（含 level.dat 与 db）
```

启动时会检测 OpenGL 上下文：若不可用（部分 Wayland + NVIDIA 组合、虚拟机、纯软件环境），
程序会弹出提示并继续以 2D 模式运行（地图、NBT、导出等功能正常，仅 3D 体素视图不可用），
同时把原因写入日志。此时可用 `./scripts/run.sh --x11` 走 XWayland 的 GLX 路径。

运行时文件不再依赖当前工作目录，按 XDG 规范放置：

| 内容 | 默认位置 | 覆盖变量 |
| ---- | -------- | -------- |
| `config.ini` | `~/.config/BedrockMap/config.ini` | `BEDROCKMAP_CONFIG_DIR` |
| 运行日志 | `~/.local/state/BedrockMap/logs/*.log` | `BEDROCKMAP_LOG_DIR` |
| 崩溃日志 | `~/.local/state/BedrockMap/logs/crash_<时间戳>.log` | `BEDROCKMAP_LOG_DIR` |
| 最近打开的世界 | `~/.config/BedrockMap/cache.json` | `BEDROCKMAP_CONFIG_DIR` |
| 方块/生物群系颜色表、翻译 | 可执行文件同目录，其次 `<prefix>/share/BedrockMap` | `BEDROCKMAP_DATA_DIR` |

如果这些资产都找不到（例如只拷贝了可执行文件），程序仍会启动，但地图没有颜色，日志中会给出
查找过的路径。

## 4. 安装

```bash
./scripts/install.sh                     # 安装到 ~/.local（无需 root）
./scripts/install.sh --prefix /usr/local # 或指定前缀
```

安装布局（`cmake --install`）：

```
<prefix>/bin/BedrockMap
<prefix>/share/BedrockMap/block_color.json
<prefix>/share/BedrockMap/biome_color.json
<prefix>/share/BedrockMap/translations/{en,zh_CN}.qm
<prefix>/share/applications/BedrockMap.desktop
<prefix>/share/icons/hicolor/scalable/apps/BedrockMap.svg
```

桌面环境会把程序识别为 `BedrockMap`（`QApplication::setDesktopFileName`），
图标与任务栏分组随之生效。

## 5. 打包

```bash
./scripts/deploy.sh
# -> dist/BedrockMap-<git tag>-linux-x86_64.tar.gz
```

打包产物依赖系统 Qt 6 运行库（与 Windows 侧 windeployqt 打包方式对应的是发行版软件包），
解压后可直接运行 `./BedrockMap`。

## 6. Linux 上的世界存档位置

基岩版没有原生 Linux 版本，通常经由 mcpelauncher 或 Waydroid 运行，程序会自动扫描：

- `~/.local/share/mcpelauncher/games/com.mojang/minecraftWorlds`（原生 mcpelauncher，`$MCPELAUNCHER_DATA_DIR` 优先）
- `~/.var/app/io.mrarm.mcpelauncher/data/mcpelauncher/games/com.mojang/minecraftWorlds`（Flatpak）
- `~/.local/share/waydroid/data/media/0/Android/data/com.mojang.minecraftpe/files/games/com.mojang/minecraftWorlds`（Waydroid 共享存储）
- `/var/lib/waydroid/data/data/com.mojang.minecraftpe/files/games/com.mojang/minecraftWorlds`（Waydroid 应用私有目录）
- BedrockBoot：自动读取 `~/.config/RoundStudio/BedrockBoot2/*/*/game_prefix/drive_c/users/*/AppData/Roaming`
  下的 `Minecraft Bedrock/Users/<id>/games/com.mojang/minecraftWorlds`（Windows 版通过 Proton 运行时的标准布局，
  与「正式版/预览版」两个分组对应）

其他位置可在"打开世界"对话框中直接选择目录，选择过的路径会进入"最近打开"列表。

## 7. 移植改动清单

| 文件 | 改动 |
| ---- | ---- |
| `src/include/core/apppaths.h`、`src/core/apppaths.cpp` | 新增：XDG 配置/数据/状态目录解析、资产搜索路径、可执行文件目录 |
| `src/core/config.cpp`、`src/include/core/config.h` | 路径常量改为运行时解析；移除 Windows 专用的 `MCBE_LEVEL_PATH` |
| `src/core/main.cpp` | 日志写入 XDG 状态目录；默认字体由"微软雅黑"改为按平台挑选已安装的中文字体；设置桌面文件名；支持命令行直接打开世界目录；启动时探测 OpenGL 上下文并在不可用时给出提示（headless 平台只记日志） |
| `src/include/core/msg.h`、`translations/*.ts` | 新增 OpenGL 不可用提示的文案（中/英） |
| `src/core/crashhandler.cpp`、`src/include/core/crashhandler.h` | 新增 POSIX 实现：`sigaction` + 备用信号栈 + `backtrace`/`dladdr` 符号化，输出到 stderr 与 `crash_<ts>.log`（原 Windows SEH 实现保留在 `_WIN32` 分支） |
| `src/core/processmonitor.cpp` | Linux 下读取 `/proc/self/statm`，回退 `getrusage` |
| `src/core/levelpathmanager.cpp`、头文件 | 新增 mcpelauncher / Flatpak / Waydroid 扫描路径与默认目录 |
| `src/core/resourcemanager.cpp` | 翻译目录来自 apppaths，并记录到日志 |
| `src/chunkeditorwidget.cpp` 等 4 处 | 等宽字体回退列表补充 Noto Sans CJK / 文泉驿 |
| `CMakeLists.txt` | LinguistTools 变为可选并提供 lrelease 回退；`icon.rc` 仅 Windows；集成 leveldb-mcpe；Linux 链接 `-rdynamic`；新增 `install()` 规则与 `desktop`/图标安装 |
| `cmake/LevelDbMcpe.cmake`、`cmake/patches/leveldb-mcpe-linux.patch` | 新增：定位/下载并以静态库方式构建 leveldb-mcpe |
| `cmake/leveldb-mcpe/libdeflate_compressor.cc` | 新增：补齐 `LibdeflateCompressorBase` 的两个虚函数。bedrock-level 子模块自 c217665 起引入 `leveldb::LibdeflateCompressorRaw`，其实现只随该子模块私有的 leveldb-mcpe 构建（即 `bedrock-level/libs/` 里的 Windows 静态库）发布，公开的 Amulet-Team 仓库没有该文件；此文件以相同头文件环境编译，链接系统 libdeflate |
| `scripts/build.sh`、`run.sh`、`install.sh`、`deploy.sh` | 新增：Linux 构建/运行/安装/打包脚本 |
| `packaging/BedrockMap.desktop` | 新增：桌面入口 |

## 8. 故障排查

| 现象 | 处理 |
| ---- | ---- |
| 启动时提示"无法创建 OpenGL 上下文" / 3D 体素视图空白 | 需要 OpenGL 3.3 Core。Wayland + NVIDIA（本机即为此类环境，EGL 可用但 Qt 建不出上下文）请改用 X11：`./scripts/run.sh --x11` 或 `QT_QPA_PLATFORM=xcb BedrockMap`；虚拟机可用 `LIBGL_ALWAYS_SOFTWARE=1` 2D/软件路径验证 |
| Wayland 下窗口无边框/位置异常 | 由合成器决定，与程序无关；可在合成器规则里为 `BedrockMap` 单独设置 |
| 界面是英文 | 未找到 `.qm`：安装 `qt6-tools`（或 PySide6 回退）后重新构建，日志中会打印实际使用的翻译目录 |
| 地图没有颜色 | `block_color.json` / `biome_color.json` 缺失，用 `BEDROCKMAP_DATA_DIR` 指向存放目录 |
| 崩溃 | 查看 `~/.local/state/BedrockMap/logs/crash_*.log`（Release 构建同样可用，链接时已加 `-rdynamic`） |
| configure 阶段提示 `Current compiler is GUN gcc, not support yet` | 这是 bedrock-level 子模块里的旧提示；本项目的 `cmake/LevelDbMcpe.cmake` 已自动处理该依赖 |
| configure 阶段报 `libdeflate was not found` | 安装系统 libdeflate（Arch：`libdeflate`；Debian/Ubuntu：`libdeflate-dev`），或用 `-DLIBDEFLATE_INCLUDE_DIR=`/`-DLIBDEFLATE_LIBRARY=` 指定 |
| 链接报 `undefined reference to leveldb::LibdeflateCompressorBase::compressImpl` | `cmake/leveldb-mcpe/libdeflate_compressor.cc` 未参与编译：确认根 `CMakeLists.txt` 的 UNIX 分支里 `target_sources(bedrock-level ...)` 仍在，并重新 configure |

## 9. 调试

```bash
# Debug 构建（build/）已包含完整 -g；Release 构建保留行号表（-g1）并加 -rdynamic，
# 便于把崩溃日志里的地址还原成源码位置：
addr2line -e build_rls/BedrockMap -f -C 0x<崩溃日志中的地址>

# 需要完整的 -g（查看局部变量）时：
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBEDROCKMAP_FULL_DEBUG=ON

gdb --args build/BedrockMap /path/to/world
```

## 10. 与 Windows 版本的差异

- 打包形式为 tar.gz + `.desktop`，不生成安装器；
- 3D 视图与字体依赖发行版 Qt 与系统字体；
- 世界扫描路径不同（见第 6 节）；
- 崩溃报告使用信号 + glibc backtrace，而非 SEH + libbacktrace。
