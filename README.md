# RK3568 车牌检测与识别系统

## 本周实验分支

新增候选质量筛选、单目标整牌多帧确认、超时清除和分阶段计时。
参数、已运行测试、实景实验入口与未完成项目见 [本周实验记录](docs/WEEKLY_EXPERIMENTS.md)。
已补充 ARM 编译、板端图片阈值对照、采样性能和无损编码验证。序列对照使用公开图片派生帧的实际板端识别结果复放，尚未验证独立道路视频。现有 `dist/` 包仍为升级前版本。

`plate_ui/run_optimized.sh` 提供本轮实验参数：BMP 存帧、250ms 抽帧、最终分数门槛 0.5 和 5 次窗口 / 3 票确认。参数可以通过环境变量覆盖；原 `run.sh` 保留保守默认值。

基于 RK3568、Qt 5、OpenCV、RKNN 和 LPRNet 的车牌识别工程。当前整理的是项目最终优化版的源码与模型：支持摄像头预览、定时取帧识别、单张图片识别，以及常驻后端复用模型。

## 整体流程

```text
MIPI 摄像头 / 用户选择的图片
                ↓
Qt 界面：预览、后台保存帧、提交识别任务
                ↓
常驻 C++ 后端：加载并复用两个 RKNN 模型
                ↓
车牌检测 → 四角透视校正与缩放 → LPRNet → CTC 解码
                ↓
JSON 与标注图 → Qt 显示号码、位置及耗时
```

板端推理使用 C++。Python 脚本用于模型转换和兼容修复，不参与正常的板端识别流程。

## 目录

| 目录 | 内容 |
| --- | --- |
| `plate_ui/` | Qt Widgets 界面、摄像头与后台任务管理、板端启动脚本 |
| `plate_persistent/` | C++ 常驻识别后端、RKNN 调用、预处理与解码、构建及运行脚本 |
| `plate_detector/` | 检测 ONNX、RKNN 模型及转换脚本 |
| `lprnet/` | 原始和兼容 ONNX、RKNN 模型、Pow 兼容修复及转换脚本 |
| `docs/` | 构建部署、模型输入和关键修改说明 |
| `dist/` | 从已部署版本取回的 ARM 运行包，含 Qt 程序、后端、模型及匹配 RKNN 运行库 |
| `licenses/` | 检测上游许可文本及项目 SDK 随附的 rknpu2 许可 |
| `SHA256SUMS` | 本次整理的交付文件校验值 |

本包包含现有源码、五个模型文件和最终 ARM 运行包。运行包与开发板上的最终程序校验值一致，详见 [运行包说明](dist/README.md)。完整 SDK、交叉工具链、Qt/OpenCV 系统库和 RKNN Toolkit2 安装包需在匹配的环境中另行准备。

## 验证过的环境基线

| 项目 | 项目实际使用的版本或配置 |
| --- | --- |
| 开发板 | ATK-DLRK3568，2 GB RAM / 32 GB 存储，aarch64 |
| 板端系统 | Linux 4.19.232，厂商 Buildroot 系统 |
| 显示 | 10.1 英寸 MIPI 触摸屏，项目界面适配 800 × 1280 |
| 摄像头 | MIPI，项目采集节点 `/dev/video-camera0` 或 `/dev/video0` |
| Qt | 5.15.2；Widgets、Multimedia、MultimediaWidgets；板端 Wayland |
| OpenCV | 板端与交叉编译 sysroot 均为 4.5.5 |
| 交叉工具链 | `aarch64-buildroot-linux-gnu-g++`，GCC 10.3 |
| RKNN | Toolkit2 1.3.0；板端 Runtime 1.3.0；NPU 驱动 0.8.2 |
| 模型转换主机 | Ubuntu、Python 3.8.10，既有 RKNN 1.3 虚拟环境 |
| Windows 界面开发 | Qt 5.15.2 / MinGW 8.1.0 64-bit |

这些是本项目使用过的配套版本，不代表任意版本组合均已验证。Windows 构建用于界面开发；部署到开发板的程序需使用 ARM 交叉工具链重新编译。

## 从源码构建

在 Ubuntu 中进入本仓库根目录，保持四个工程目录的相对位置。

```sh
# 编译并整理识别后端
sh plate_persistent/build_native.sh

# 编译开发板 Qt 界面
mkdir -p plate_ui/build-rk3568
cd plate_ui/build-rk3568
/opt/atk-dlrk356x-toolchain/bin/qmake ../plate_ui.pro
make -j2
```

后端生成目录为 `plate_persistent/install/plate_persistent`；Qt 程序为 `plate_ui/build-rk3568/plate_ui`。后端脚本默认使用 `$HOME/rk3568_linux_sdk` 和 `/opt/atk-dlrk356x-toolchain`，支持环境变量覆盖，详见 [部署说明](docs/DEPLOYMENT.md)。

## 板端运行

按 [部署说明](docs/DEPLOYMENT.md) 推送程序后，在开发板执行：

```sh
cd /userdata/plate_ui
./run.sh
```

点击“开始预览”使用摄像头；也可以选择自己的图片进行识别。程序默认从 `/userdata/plate_persistent/images` 选图，该目录可自行创建并放入图片。

使用相同环境的现有开发板，也可以按 [运行包说明](dist/README.md) 直接部署已编译程序。

命令行单图识别：

```sh
cd /userdata/plate_persistent
sh run_image.sh /绝对路径/实际照片.jpg
```

## 最终优化内容

- Qt 通过独立常驻后端提交任务，两个模型在后端启动时加载，后续任务复用上下文。
- 识别繁忙时不堆积摄像头任务；后台保存帧，并处理取消、退出和过期结果。
- LPRNet 的四个常量平方 `Pow` 节点改成等价的 `Mul`，适配项目旧版运行库。
- 单行大车牌先按角点估算尺寸校正，再用区域插值缩小；较小车牌保持直接透视路径。
- 保留原始字符解码，不通过替换省份汉字来修饰结果。

定位分数不等于号码识别正确率。双行车牌路径保留在代码中，但尚未经过充分的实景验证。历史实验结果与当前文件完整性检查应区别看待，本次整理没有重新进行板端硬件回归。

## 模型与第三方来源

见 [模型说明](docs/MODEL_NOTES.md) 和 [第三方来源说明](THIRD_PARTY_NOTICES.md)。模型使用预训练权重，本项目完成了适配、转换和部署，没有重新训练这两个模型。

仓库未统一授予所有文件 MIT 或其他新许可证；第三方模型、依赖与派生内容应保留其上游适用的授权条件。

