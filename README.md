# MotionCore

MotionCore 是平台层核心库（SDK），对外导出 CMake 包 **MotionCore**（`MotionCore::MotionCore`）。

- 该仓库只负责“库”的构建与安装
- 应用工程（例如 Workstation）通过 `find_package(MotionCore CONFIG REQUIRED)` 依赖已安装的 MotionCore

## 文档入口

- 文档索引（推荐从这里开始）：docs/README.md
- 新对话快速衔接：docs/00_架构与开发目的.md

## 构建与安装

### 开发态（推荐）：安装到 build 目录本身

```bash
cd MotionCore
mkdir -p build
cd build

cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j"$(nproc)"

# 默认安装到 MotionCore/build（不创建额外 install 文件夹）
./install.sh
```

### 发布态（可选）：安装到系统前缀（示例 /opt/motioncore）

```bash
cd MotionCore/build
./install.sh --prefix /opt/motioncore --with-docs
```

## （可选）FastDDS/FastRTPS 前缀 Pin（避免误链接 /usr/local 的旧版本）

如果你在 wxz_robot 工作空间内验证 FastDDS teardown 稳定性，建议把 FastDDS/Fast-CDR 构建并安装到工作区本地前缀，然后让 MotionCore 优先使用该前缀：

```bash
WS=/path/to/wxz_robot

# 1) 构建并安装到工作区本地前缀
cd "$WS"
./scripts/build_fastdds_prefix.sh --tag v2.14.4

# 2) 使用该前缀配置/编译 MotionCore
cmake -S MotionCore -B MotionCore/build_fastdds \
  -DCMAKE_BUILD_TYPE=Release \
  -DWXZ_FASTDDS_PREFIX="$WS/depends/eprosima_fastdds/v2.14.4/_install"
cmake --build MotionCore/build_fastdds -j"$(nproc)"

# 3) 安装到 build 目录（供下游 find_package(MotionCore) 使用）
cd MotionCore/build_fastdds
./install.sh
```

## 下游工程接入（CMake）

```cmake
find_package(MotionCore CONFIG REQUIRED)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE MotionCore::MotionCore)
```
