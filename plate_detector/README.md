# 车牌检测模型

包含 `model/plate_detect.onnx` 和 `model/plate_detect.rknn`。需要重新转换时，先激活既有 RKNN Toolkit2 1.3 环境，在本目录运行 `python convert_detector.py`。

模型转换导出三个原始检测头 `792`、`912`、`1032`。板端完成车牌候选解码、筛选、NMS 和坐标还原，再按四个角点准备识别输入。

输入为 RGB 640 × 640，缩放采用 letterbox，填充值为 114；模型配置包含除以 255 的归一化。输入原始 UINT8 像素时，不重复除以 255。

详见 [模型说明](../docs/MODEL_NOTES.md) 和 [第三方来源](../THIRD_PARTY_NOTICES.md)。
