# 最终 ARM 运行包

`rk3568-runtime-20261105.tar.gz` 是从项目最后完成部署的版本整理的运行文件。虚拟机与开发板上的 Qt、后端可执行文件 SHA-256 一致。

| 文件 | SHA-256 |
| --- | --- |
| `plate_ui/plate_ui` | `d335653b4c877d33d4da2ca406a1d23f19246bcb2859adfb314a6c05aa9e1612` |
| `plate_persistent/plate_native` | `42ab83d5265c1770b40d6422c73312e454668e4fe0aa60dc60b1d2548b54585d` |
| `plate_persistent/model/plate_detect.rknn` | `862e96f4b3e2b973b8bdc1fa8c3f8862dbc08866b658e1469b9b626ce7b0947a` |
| `plate_persistent/model/lprnet.rknn` | `296484292b33a124da6ba2e864ed243304b8ae4daacc2d5c78194611797e40ef` |

两份程序均为 ARM aarch64 ELF。压缩包还包含启动脚本、RKNN 运行库、空图片目录、内部 `SHA256SUMS` 和 SDK 随附许可，没有个人图片或识别日志。

## 适用环境

项目原有 RK3568 / Linux 4.19.232 系统，Qt 5.15.2、OpenCV 4.5.5、可用的 Wayland、MIPI 摄像头及 NPU 驱动 0.8.2。运行包提供 RKNN Runtime 1.3.0。它不是完整系统镜像，也不包含 Qt 和 OpenCV 的系统安装文件。

## 查看和校验

在 Ubuntu 中先解压到独立目录：

```sh
mkdir -p rk3568-runtime
tar -xzf dist/rk3568-runtime-20261105.tar.gz -C rk3568-runtime
cd rk3568-runtime
sha256sum -c SHA256SUMS
```

## 部署到现有开发板

先退出正在运行的 Qt 程序，再在 Ubuntu 的仓库根目录推送压缩包：

```sh
adb push dist/rk3568-runtime-20261105.tar.gz /tmp/
adb shell
```

随后在开发板终端执行，只展开两个项目运行目录到 `/userdata`：

```sh
tar -xzf /tmp/rk3568-runtime-20261105.tar.gz -C /userdata ./plate_ui ./plate_persistent
cd /userdata/plate_ui
./run.sh
```

展开会更新这两个目录下的同名运行文件。界面点“开始预览”后进行摄像头识别；也可选图。该包没有配置开机自启动。

本次发布核对了文件完整性与程序架构，未重新进行摄像头和推理功能回归；历史实机运行结果见项目说明。
