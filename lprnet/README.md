# LPRNet 识别模型

包含原始 `model/lprnet.onnx`、兼容修复后的 `model/lprnet_compat.onnx` 和可直接用于本项目板端的 `model/lprnet.rknn`。

需要重新转换时，先激活既有 Python 3.8 / RKNN Toolkit2 1.3 环境，在本目录运行 `python convert_lprnet.py`。脚本依赖 `fix_lprnet_pow.py`，只对四个常量平方 Pow 节点进行等价替换，然后生成 RKNN。

输入为宽 94、高 24 的 BGR 三通道图像，模型配置包含 `(x - 127.5) / 127.5` 归一化。板端提交原始 UINT8 图像，不重复归一化。

详见 [模型说明](../docs/MODEL_NOTES.md) 和 [第三方来源](../THIRD_PARTY_NOTICES.md)。
