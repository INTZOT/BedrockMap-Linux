# 在 Linux 上构建与运行 BedrockMap

本文档说明 BedrockMap（Qt Quick 版本）在 Linux x86_64 / X11 / Wayland 上的构建、运行、
安装与打包方式，并列出移植涉及的代码与构建系统改动。

## 1. 环境要求

| 依赖 | 版本 | 说明 |
| ---- | ---- | ---- |
| Qt 6 | ≥ 6.11（本机验证：6.12.0） | 需要 Core / Gui / Qml / Quick / Concurrent / Network / Svg / ShaderTools |
| Qt 6 私有模块 | 同上 | CorePrivate / GuiPrivate / QuickPrivate / QuickDialogs2Private / QuickDialogs2UtilsPrivate / QuickTemplates2Private；见下方说明 |
| CMake | ≥ 3.16（本机验证：4.5.0） | 构建系统 |
| Ninja | 任意 | 可选，未安装时退化为 Make |
| C++ 编译器 | 支持 C++20 | GCC 16.2 / Clang 22 |
| zlib | 任意 | leveldb-mcpe 依赖 |
| libdeflate | 任意（本机验证：1.26） | bedrock-level 的解压加速路径依赖它，见第 2 节 |
| git | 任意 | 拉取子模块与 leveldb-mcpe |
| Qt LinguistTools | 可选 | 提供 `lrelease` / `lupdate`；缺失时见下文回退方案 |

Arch / CachyOS（私有头文件随 `qt6-base` / `qt6-declarative` 一起安装）：

```bash
sudo pacman -S --needed base-devel cmake ninja git zlib libdeflate \
    qt6-base qt6-declarative qt6-shadertools qt6-svg
```

Debian / Ubuntu（私有模块在单独的 `-private-dev` 包里）：

```bash
sudo apt install build-essential cmake ninja-build git zlib1g-dev libdeflate-dev \
    libgl1-mesa-dev \
    qt6-base-dev qt6-base-private-dev qt6-declarative-dev qt6-declarative-private-dev \
    qt6-shadertools-dev qt6-svg-dev qt6-tools-dev qt6-tools-dev-tools
```

### 1.1 免 root 工具链

若系统缺少 `cmake` / `ninja` / `lrelease`，可在用户目录准备一个工具链虚拟环境，
`scripts/build.sh` 会自动使用它（PATH 中已有的工具优先）：

```bash
python3 -m venv ~/.local/venvs/bedrockmap
~/.local/venvs/bedrockmap/bin/pip install cmake ninja PySide6-Essentials
```

- `cmake` / `ninja` 由 PyPI wheel 提供；
- `PySide6-Essentials` 提供 `pyside6-lrelease` / `pyside6-lupdate`，用于在缺少
  Qt LinguistTools 时编译 `translations/*.ts`。

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

与上游 Windows 构建的两处差异：

1. **leveldb-mcpe 由源码构建。** 上游只为 Windows / Android / wasm 提供预编译库
   （`bedrock-level/libs/`），因此 Linux 上由 `cmake/LevelDbMcpe.cmake` 负责：
   若存在 `third/leveldb-mcpe` 源码目录就直接作为子项目编译，否则从固定上游 commit 下载，
   并套用 `cmake/patches/leveldb-mcpe-linux.patch`（静态库 + 系统 zlib）。
2. **libdeflate 编解码。** bedrock-level 的 vendored 头文件在
   `ZlibCompressorBase` 上暴露 `useLibdeflate`，而公开的 Amulet-Team leveldb-mcpe
   没有该实现，`cmake/patches/leveldb-mcpe-libdeflate.patch` 会补上
   （解压走 libdeflate，压缩回退顺序与原实现一致）。

## 3. 运行

```bash
./scripts/run.sh              # Debug
./scripts/run.sh --release    # Release
./scripts/run.sh --x11        # 强制使用 X11/XWayland 后端
BedrockMap /path/to/world     # 直接打开某个世界目录（含 level.dat 与 db）
```

界面使用 Qt Quick 场景图（RHI）。默认后端为自动选择；可用 `QSG_RHI_BACKEND=opengl`
或 `QSG_RHI_BACKEND=vulkan` 覆盖，程序也会把设置中的渲染后端写入 `QSG_RHI_BACKEND`。

运行时文件不依赖当前工作目录，按 XDG 规范放置（`app_paths`，基于 `QStandardPaths`）：

| 内容 | 默认位置 |
| ---- | -------- |
| `config.ini` | `~/.config/BedrockMap/config.ini` |
| `recent.ini`（最近打开） | `~/.config/BedrockMap/recent.ini` |
| 缓存 | `~/.cache/BedrockMap` |
| 运行日志 | `~/.cache/BedrockMap/logs` |
| 导入/导出目录 | `~/Documents/BedrockMap` |
| 应用内置世界 | `~/.local/share/BedrockMap/worlds` |

方块/生物群系颜色表、翻译、着色器与图标都编译进可执行文件，运行时不需要额外资产目录。

开发用环境变量：

| 变量 | 作用 |
| ---- | ---- |
| `BEDROCKMAP_OPEN_WORLD=/path/to/world` | 启动后直接打开指定世界 |
| `BEDROCKMAP_START_PAGE=<n>` | 启动页（0 世界列表，1 已打开，2 工具，3 设置） |
| `QSG_RHI_BACKEND=opengl\|vulkan` | 覆盖 Qt Quick 渲染后端 |
| `QSG_INFO=1` | 打印场景图/RHI 初始化信息，排查黑屏时使用 |

## 4. 安装

```bash
./scripts/install.sh                     # 安装到 ~/.local（无需 root）
./scripts/install.sh --prefix /usr/local # 或指定前缀
```

安装布局（`cmake --install`）：

```
<prefix>/bin/BedrockMap
<prefix>/share/applications/BedrockMap.desktop
<prefix>/share/icons/hicolor/scalable/apps/BedrockMap.svg
```

## 5. 打包

```bash
./scripts/deploy.sh
# -> dist/BedrockMap-<git tag>-linux-<arch>.tar.gz
```

打包产物依赖系统 Qt 6 运行库（与 Windows 侧 windeployqt 的思路对应的是发行版软件包），
解压后可直接运行 `./BedrockMap`。

## 6. Linux 上的世界存档位置

基岩版没有官方 Linux 版本，程序会自动扫描以下位置：

- **mcpelauncher**：`~/.local/share/mcpelauncher/games/com.mojang/minecraftWorlds`
  （`MCPELAUNCHER_DATA_DIR` 优先）
- **mcpelauncher Flatpak**：
  `~/.var/app/io.mrarm.mcpelauncher/data/mcpelauncher/games/com.mojang/minecraftWorlds`
- **Waydroid**：
  `~/.local/share/waydroid/data/media/0/Android/data/com.mojang.minecraftpe/files/games/com.mojang/minecraftWorlds`
  与 `/var/lib/waydroid/data/data/com.mojang.minecraftpe/files/games/com.mojang/minecraftWorlds`
- **BedrockBoot**（Proton 前缀）：
  `~/.config/RoundStudio/BedrockBoot2/*/*/game_prefix/drive_c/users/*/AppData/Roaming/Minecraft Bedrock*/Users/*/games/com.mojang/minecraftWorlds`

其他位置可在"打开世界"里直接选择目录，选择过的路径会进入最近列表，
并可在设置里保存为额外的世界来源。

## 7. 移植改动清单

| 文件 | 改动 |
| ---- | ---- |
| `CMakeLists.txt` | LinguistTools 改为可选并提供 `lrelease` 回退；Linux 分支自行构建 leveldb-mcpe 与 bedrock-level；RHI 头文件路径改为按 `qmake -query` 解析；QML 导入目录复制加存在性保护；新增 `install()` 规则 |
| `cmake/LevelDbMcpe.cmake` | 新增：定位/下载 leveldb-mcpe，应用静态库与 libdeflate 补丁，导出 `LEVELDB_MCPE_INCLUDE_DIR` 并链接系统 libdeflate |
| `cmake/BedrockLevelLinux.cmake` | 新增：子模块的 CMakeLists 只支持 MinGW/Android/wasm，Linux 上由本文件用相同源文件与头文件顺序创建 `bedrock-level` 目标 |
| `cmake/patches/leveldb-mcpe-linux.patch` | 新增：静态库构建 + 系统 zlib |
| `cmake/patches/leveldb-mcpe-libdeflate.patch` | 新增：为 `ZlibCompressorBase` 增加 `useLibdeflate` 及 libdeflate 编解码实现 |
| `cmake/leveldb-mcne/mcne.cc` | 新增：网易版（mcne）XOR 文件编解码。子模块的 vendored `leveldb/mcne.h` 只有声明，实现只存在于私有 leveldb-mcpe 构建中；此文件（MIT，移植自 HTMonkeyG/leveldb-mcne）按下标版本头文件的接口补齐实现 |
| `src/loader/worldsource.cpp`、`worldsource.h` | 新增 Linux 世界来源扫描（mcpelauncher / Flatpak / Waydroid / BedrockBoot） |
| `scripts/build.sh`、`run.sh`、`install.sh`、`deploy.sh` | 新增：Linux 构建/运行/安装/打包脚本 |
| `packaging/BedrockMap.desktop` | 新增：桌面入口 |
| `.gitignore` | 忽略本地 `third/` 依赖缓存，并保留 `docs/Linux.md` |

## 8. 故障排查

| 现象 | 处理 |
| ---- | ---- |
| configure 报 `Could not find a package configuration file ... Qt6QuickPrivate` | 缺少 Qt 私有模块：Arch 安装 `qt6-base` / `qt6-declarative`；Debian 安装 `qt6-base-private-dev` / `qt6-declarative-private-dev` |
| configure 报 `libdeflate was not found` | 安装 libdeflate（Arch：`libdeflate`；Debian：`libdeflate-dev`），或用 `-DLIBDEFLATE_INCLUDE_DIR=` / `-DLIBDEFLATE_LIBRARY=` 指定 |
| configure 报 `Cannot locate QtGui's RHI headers` | Qt 安装不完整（缺私有头）；确认 `qmake6 -query QT_INSTALL_HEADERS` 下的 `QtGui/<版本>/QtGui/rhi/qrhi.h` 存在 |
| 启动黑屏 / 窗口空白 | 先设 `QSG_INFO=1` 看 RHI 初始化；Wayland + NVIDIA 组合可试 `./scripts/run.sh --x11`，或 `QSG_RHI_BACKEND=opengl` |
| 界面是英文 | 未编译出 `.qm`：安装 `qt6-tools`（或保持 PySide6 回退）后重新构建；configure 日志会打印实际使用的 `lrelease` |
| 世界列表为空 | 程序只扫描第 6 节列出的路径；用"打开世界"手动选择，或把目录加入世界来源设置 |
| 地图没有颜色 | 颜色表已编译进程序；若自行替换了 `bedrock-level/data/*.json`，需要重新构建 |
| 崩溃 | 目前 Linux 上 `platform::installCrashHandler()` 还是空实现（Windows 使用 SEH + libbacktrace），先用 `gdb --args build/BedrockMap` 复现 |

## 9. 调试

```bash
# Debug 构建（build/）带完整 -g。
gdb --args build/BedrockMap /path/to/world

# 需要优化后的 Release 带完整调试信息时：
cmake -S . -B build_rls -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBEDROCK_LEVEL_BUILD_BENCHMARKS=ON

# 查看 QML 加载与 RHI 初始化：
QSG_INFO=1 QML_IMPORT_TRACE=1 ./scripts/run.sh
```

## 10. 与上游 Windows 版本的差异

- 由发行版 Qt 6 提供运行库，打包形式为 tar.gz + `.desktop`，不生成安装器；
- leveldb-mcpe 与 libdeflate 在 Linux 上从源码/系统库链接，不使用子模块里的预编译库；
- 崩溃处理尚未移植（Windows 使用 SEH + libbacktrace）；
- 世界扫描路径不同（见第 6 节）。
