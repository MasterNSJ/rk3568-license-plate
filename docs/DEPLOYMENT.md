# 构建与部署

## 1. 环境与目录

Ubuntu 上应已安装厂商配套 SDK、交叉工具链和 ARM Qt qmake。板端应已能显示 Wayland 桌面或运行 Qt 界面，摄像头驱动与 RKNN 驱动也应可用。

仓库根目录下保持以下并列结构：

```text
plate_ui/
plate_persistent/
plate_detector/model/
lprnet/model/
```

工具链的默认位置：

```text
/opt/atk-dlrk356x-toolchain/bin/aarch64-buildroot-linux-gnu-g++
/opt/atk-dlrk356x-toolchain/bin/qmake
```

后端构建脚本支持：

| 环境变量 | 默认值或用途 |
| --- | --- |
| `RK3568_SDK_ROOT` | `$HOME/rk3568_linux_sdk` |
| `RK3568_CXX` | 上述 ARM C++ 编译器路径 |
| `PLATE_DETECT_MODEL` | `plate_detector/model/plate_detect.rknn` |
| `PLATE_REC_MODEL` | `lprnet/model/lprnet.rknn` |

SDK 中需要 `external/rknpu2/runtime/RK356X/Linux/librknn_api` 的头文件与 aarch64 运行库。脚本通过编译器查询 sysroot，并链接其中的 OpenCV 4.5.5，避免混入 SDK 示例附带的另一版静态 OpenCV。

## 2. 编译

以下在 Ubuntu 的仓库根目录执行：

```sh
sh plate_persistent/build_native.sh
mkdir -p plate_ui/build-rk3568
cd plate_ui/build-rk3568
/opt/atk-dlrk356x-toolchain/bin/qmake ../plate_ui.pro
make -j2
```

回到仓库根目录，确认程序架构：

```sh
/usr/bin/file plate_ui/build-rk3568/plate_ui
/usr/bin/file plate_persistent/install/plate_persistent/plate_native
```

两者应为 ARM aarch64 ELF，不能将 Windows 的 EXE 直接推送为板端程序。

## 3. 推送到开发板

在运行 ADB 的 Ubuntu 终端执行，确保 `adb devices` 中只选择了目标板；多个设备连接时需自行添加 `-s` 参数指定序列号。下面用 PATH 中的 `adb`，也可替换成自己的平台工具完整路径。

推送前先退出正在运行的旧程序，再在仓库根目录执行：

```sh
adb devices
adb shell 'mkdir -p /userdata/plate_ui /userdata/plate_persistent/images'
adb push plate_ui/build-rk3568/plate_ui /userdata/plate_ui/plate_ui
adb push plate_ui/run.sh /userdata/plate_ui/run.sh
for item in plate_native run_image.sh run_stream.sh model lib; do
    adb push "plate_persistent/install/plate_persistent/$item" /userdata/plate_persistent/
done
adb shell 'chmod +x /userdata/plate_ui/plate_ui /userdata/plate_ui/run.sh /userdata/plate_persistent/plate_native /userdata/plate_persistent/run_image.sh /userdata/plate_persistent/run_stream.sh'
```

`build_native.sh` 会把 SDK 的匹配版 `librknnrt.so` 放入后端安装包。推送 `lib/` 时应保留该 SDK 适用的授权信息。板端仍需原有匹配的 Qt、OpenCV、字体与显示服务。

## 4. 运行

进入板端：

```sh
adb shell
cd /userdata/plate_ui
./run.sh
```

启动脚本使用本项目板端的 Wayland 环境：`QT_QPA_PLATFORM=wayland`、`XDG_RUNTIME_DIR=/var/run`、字体目录 `/usr/share/fonts`。不同系统需要按实际环境调整这些值。

界面点击开始预览，后端会在首次识别请求时启动并加载模型。相机识别最多每秒采样一次，繁忙时跳过，不把每个视频帧都加入识别队列。界面预览帧率与模型识别频率含义不同。

单图命令行：

```sh
cd /userdata/plate_persistent
sh run_image.sh /绝对路径/照片.jpg [结果目录]
```

不传图片参数时会显示用法并退出。结果目录可省略，脚本会生成带时间戳的目录；结果中包含 JSON 与标注图片等。

## 5. 可选：重新转换模型

本包已经包含转换好的 RKNN。普通构建与运行不要求重新转换。需要复现转换时，使用项目已有的 Python 3.8 / RKNN Toolkit2 1.3 环境：

```sh
# 先激活你已有的 RKNN 1.3 虚拟环境
cd lprnet
python convert_lprnet.py
cd ../plate_detector
python convert_detector.py
```

LPRNet 转换脚本依赖 `fix_lprnet_pow.py`，请保留。检测和识别模型的颜色次序、输入布局与归一化约定不同，详见模型说明。

## 6. 文件完整性

在仓库根目录可执行：

```sh
sha256sum -c SHA256SUMS
```

清单对应本次源码与模型整理时的文件。自行修改代码或重新转换模型后，清单应重新生成。文件校验通过不代表推理准确率或硬件功能已经通过测试。
