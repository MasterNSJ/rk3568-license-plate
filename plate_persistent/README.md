# 常驻识别后端

在 Ubuntu 的本目录执行 `sh build_native.sh`，输出 `install/plate_persistent`。默认从并列的 `plate_detector/model` 和 `lprnet/model` 读取 RKNN 模型。

Qt 通过 `run_stream.sh` 启动常驻进程，两个模型在进程初始化时加载，后续请求复用上下文。命令行单图入口为 `sh run_image.sh /绝对路径/照片.jpg [结果目录]`。

完整环境与操作见 [构建部署说明](../docs/DEPLOYMENT.md)，模型约定见 [模型说明](../docs/MODEL_NOTES.md)。
