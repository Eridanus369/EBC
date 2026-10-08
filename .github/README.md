# EBC 5.3

EBC 是基于 Blender 的定制 3D 引擎分支，基于 Blender 5.3 alpha。

## 版本信息

- Blender：5.3.0 Alpha
- 平台：Linux x86_64

## 特性

- 继承 Blender 5.3 全部建模 / 渲染 / 雕刻 / 动画功能
- 保留 UPBGE 游戏引擎（BGE）相关模块
- 自定义 EBC 品牌与启动画面

## 编译说明

本仓库不含预编译库（lib/）和编译产物（build_linux/），编译前需自行准备。

### 1. 获取预编译库
cd ~/Code/EBC/EBC5.3
mkdir -p lib && cd lib
svn checkout https://svn.blender.org/svnroot/bf-blender/trunk/lib/linux_x86_64_glibc_228 .

### 2. 系统依赖

- GCC 14+ 或 Clang 17+
- Python 3.13
- CMake 3.21+
- Ninja

### 3. 构建

mkdir -p build_linux && cd build_linux
cmake -G Ninja -DWITH_LIBS_PRECOMPILED=ON ..
ninja -j6
ninja install

编译完成后可执行文件位于 build_linux/bin/blender。

## 已知问题

- 部分资源（tests/files/、release/darwin/、release/windows/installer_wix/）在原仓库使用 Git LFS 存储，本仓库未包含，不影响 Linux 下编译和使用。
- 编译若遇到 OIIO / OpenEXR / glog 链接问题，参考仓库 Issues。

## 许可

GPL-3.0，继承自 Blender。
