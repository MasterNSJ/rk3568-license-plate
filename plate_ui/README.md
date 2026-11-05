# Qt 界面

在 Windows Qt Creator 中打开 `plate_ui.pro` 可进行界面开发。项目使用 Qt 5.15.2、Widgets、Multimedia、MultimediaWidgets 和 C++17。

板端程序需要在 Ubuntu 使用厂商提供的 ARM qmake 重新编译；Windows 构建的 EXE 不用于部署到 RK3568。

- `mainwindow.cpp/.h/.ui`：界面、摄像头状态与识别任务调度。
- `framesaver.cpp/.h`：后台保存摄像头帧。
- `persistentbackend.cpp/.h`：常驻识别进程与请求协议。
- `run.sh`：项目板端 Wayland 启动环境。

Qt 默认通过 `/userdata/plate_persistent/run_stream.sh` 调用后端。单图选图目录为 `/userdata/plate_persistent/images`；结果及临时帧位于 `/userdata/plate_ui` 下。

完整操作见 [构建部署说明](../docs/DEPLOYMENT.md)，系统结构见 [根 README](../README.md)。
