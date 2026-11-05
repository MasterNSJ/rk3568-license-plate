# 第三方来源与许可说明

本项目组合了 Qt 界面、板端识别程序、预训练模型及 Rockchip SDK 接口。不同组成部分的来源与适用条款应分别核对；本文件没有为整个项目添加统一的 MIT 或其他重新许可声明。

## 1. 车牌检测模型

- 上游项目：[we0091234/Chinese_license_plate_detection_recognition](https://github.com/we0091234/Chinese_license_plate_detection_recognition)。
- 本项目保留其检测 ONNX，并通过 `plate_detector/convert_detector.py` 导出供 RK3568 使用的三路原始检测头。当前成品没有重新训练该检测权重。
- 上游 GitHub 仓库标示 **GPL-3.0**。请以对应版本的上游许可文本、文件级声明及模型发布说明为准，并保留适用的版权、许可和来源信息。不能把本文件理解为对第三方模型及相关代码的额外许可。
- 随包保留上游公开的 [GPL-3.0 许可文本](licenses/Detection-upstream-GPL-3.0.txt)。
- 原始 ONNX 与当前 RKNN 的校验值见 [模型说明](docs/MODEL_NOTES.md)。

## 2. LPRNet 原始模型与兼容图

- 原始 `lprnet/model/lprnet.onnx` 来自项目所用 RK3568 SDK 中的 Rockchip LPRNet 示例。
- 对应公开上游入口：[rockchip-linux/rknpu2](https://github.com/rockchip-linux/rknpu2)。
- rknpu2 仓库根许可为 **BSD-3-Clause**，参见其[许可文件](https://github.com/rockchip-linux/rknpu2/blob/master/LICENSE)。这条根许可信息本身不足以证明所有示例模型权重、下载资源或 SDK 随附文件都适用同一许可。模型权重仍须结合具体来源和发布说明确认。
- `lprnet_compat.onnx` 在保留权重的基础上，仅将四个常量平方 `Pow(x, 2)` 改写为 `Mul(x, x)`；`lprnet.rknn` 为兼容图的 RKNN 导出物。模型转换或算子改写不会消除原模型的来源及适用许可要求。
- 原始、兼容及 RKNN 模型的校验值与修改范围见 [模型说明](docs/MODEL_NOTES.md)。

## 3. Rockchip SDK、工具链及运行库

后端源码通过 `rknn_api.h` 使用 RKNN C API，构建时链接 SDK 提供的 `librknnrt.so`。模型转换使用 RKNN Toolkit2。相关资料：

- [Rockchip rknpu2](https://github.com/rockchip-linux/rknpu2)
- [Rockchip rknn-toolkit2](https://github.com/rockchip-linux/rknn-toolkit2)

`dist/` 中的最终 ARM 运行包含项目 Qt 程序、识别后端及配套 SDK 的 `librknnrt.so`，并保留项目 SDK 中 `external/rknpu2/LICENSE`，也可在仓库的 [Rockchip-rknpu2-LICENSE](licenses/Rockchip-rknpu2-LICENSE) 中查看。未随附完整 SDK、工具链、RKNN 头文件或 Toolkit 安装包。使用者应遵循具体 SDK 发行物及依赖的适用条款；这里保留来源和随附许可，不另行授予第三方运行库权利。

## 4. 框架与其他依赖

下列组件是构建或运行依赖，当前暂存包没有打包它们的完整发行版：

| 组件 | 在项目中的用途 | 上游说明 |
| --- | --- | --- |
| Qt 5.15.2 Widgets / Multimedia | 界面、摄像头、线程与进程管理 | [Qt 许可说明](https://doc.qt.io/qt-5/licensing.html)；核对所安装发行版及相应模块条款 |
| OpenCV 4.5.5 | 图像读取、缩放、颜色转换、透视校正及结果保存 | [OpenCV 许可说明](https://opencv.org/license/) |
| ONNX、NumPy | 识别图兼容改写与模型检查 | [ONNX](https://github.com/onnx/onnx)、[NumPy](https://github.com/numpy/numpy) 的适用发行版许可 |

厂商运行库、第三方模型、依赖框架与本项目编写的连接代码，应分别保留各自的许可来源。增加本项目自身代码的许可证时，也需处理与上游代码及模型相关的适用要求，不应直接把所有文件标为自研或统一 MIT。

## 5. 数据与成果范围

当前暂存包不包含训练数据、公开测试照片、个人拍摄照片、历史识别结果或汇报文档。模型输入约定、兼容修复及验证边界集中记录于 [docs/MODEL_NOTES.md](docs/MODEL_NOTES.md)。来源记录与模型哈希不构成训练数据授权、现场准确率或商业使用许可的证明。
