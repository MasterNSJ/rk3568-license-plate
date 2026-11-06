# 本周优化实现与验证记录

## 当前完成情况

已实现：最终检测分数筛选、最小区域与角点检查、单目标整牌多帧确认、独立超时清除、采样间隔配置、PNG/BMP 无损存帧选择、分阶段计时、图片评测和板端批量运行入口。

已运行：Windows / MinGW 8.1 C++17 的 24 项逻辑测试，Python 图片评测器的 3 项测试，以及实际 C++ 投票实现的 6 组人工序列 × 3 组窗口，共 18 组回放。Qt 5.15.2 Windows Release 已编译通过。

已补充运行：ARM 后端与 Qt 交叉编译、RK3568 上的 C++ 测试、84 个受控输入 × 5 种阈值设置的 420 个计分组合、基于板端识别结果的时间序列复放，以及 PNG/BMP 各三档采样的 6 次 60 秒实机测试。通过现有 SSH 配置进入 Ubuntu，再通过 ADB 访问开发板。实验二进制部署到独立目录。

尚未验证：独立道路实拍视频、真实 TAXI 顶灯负样本、大规模独立测试集和 30 分钟连续运行。时间序列实验使用实图派生帧和实际板端 OCR 输出的复放，不能写成道路视频测试。

## 实测结果

- 输入来自上游公开示例的 7 张普通车牌场景，生成 50 个亮度、模糊、缩放和旋转变体；另有 30 个无牌实图裁剪及 4 个合成 TAXI 标牌。它们不是 84 张独立实拍照片。标注和来源见结果目录的 manifest/provenance。
- baseline 与阈值 0.2/0.3/0.4/0.5 的错误候选数为 8/8/8/7/2，50 个标注目标均检出，整牌正确数均为 28/50。原始 7 个输入正确 5/7。该小样本说明候选筛选减少了部分停车位数字误检，没有证明字符识别准确率提升。
- 34 个负输入在所有设置下均没有候选。这批负输入较简单，不能据此声称解决真实 TAXI 顶灯问题。
- 阈值 0.5 下，7 段各 10 次观测的派生序列中，窗口 1/3/5 的错误确认次数为 16/9/6，确认输出次数为 61/47/38，确认文本变化次数为 13/4/0。降低错误和跳变伴随更多待确认输出，部分系统性错误仍存在。
- 同为 250ms 采样间隔，PNG/BMP 的取帧至 UI 状态提交中位延迟为 1232/550.5ms，P95 为 1259/570ms，60 秒完成任务数为 42/78。该指标不等于预览帧率、纯 NPU 延迟或物理屏幕显示时间。
- PNG/BMP 编码路径在 2112×1568 控制图片的三次往返中均通过像素一致性检查。BMP 文件约 9.9MB，仍有存储带宽开销；本控制样本的低压缩 PNG 文件大小也约 10MB，不能笼统宣称 BMP 一定比本配置的 PNG 更大。

表格和逐请求计时见 `experiments/results/rk3568/`，板端输出复放见 `experiments/results/sequence_results_score05/`，摄像头性能见 `experiments/results/camera/`。公开结果不包含未标注现场摄像头照片。

原有 `dist/` 运行包仍是原版本。新源码在完成 ARM 编译前不能视为已更新的板端二进制。

本轮隔离部署位于板端 `/userdata/plate_weekly_20261106/`，已编译的 UI 和后端分别位于 `ui/`、`backend/`。进入 `ui/` 执行 `sh run_optimized.sh` 可启动实验预设。一般部署到原路径时，按原构建与部署说明更新二进制后再执行此入口。

## 参数

| 环境变量 | 默认值 | 含义 |
|---|---:|---|
| PLATE_FILTER_ENABLED | 1 | 0 恢复升级前的候选筛选路径 |
| PLATE_MIN_SCORE | 0.30 | 最终检测分数门槛，尚未通过实景调参 |
| PLATE_MIN_WIDTH / PLATE_MIN_HEIGHT | 16 / 6 | 原图坐标中的最小候选框尺寸 |
| PLATE_SAMPLE_MS | 1000 | Qt 抽帧间隔，仍保留忙时跳过机制 |
| PLATE_FRAME_FORMAT | png | png/bmp 无损编码选择 |
| PLATE_BACKEND_SCRIPT | 原后端路径 | 可覆盖后端启动脚本，供隔离部署 |
| PLATE_VOTE_WINDOW | 5 | 最近有效预测数量上限 |
| PLATE_VOTE_QUORUM | 窗口过半 | 整串号码确认票数，必须严格过半 |
| PLATE_TARGET_TTL_MS | 3000 | 未见有效目标后的超时时间 |
| PLATE_HISTORY_MS | 8000 | 历史投票最长保留时间 |
| PLATE_METRICS_FILE | 不写文件 | 可选 JSONL 计时日志路径 |
| PLATE_AUTO_START | 0 | 1 启动后自动开启预览，用于受控实验 |
| PLATE_RUN_SECONDS | 0 | 大于 0 时按秒自动退出 |

只对单一候选目标投票。出现多个候选时清空投票并提示待确认，不能宣称已实现通用多目标跟踪。位置关联使用 IoU，会受到相机运动、车辆速度和低采样率影响。

号码“多帧已确认”仅代表投票规则满足，仍可能一致地识别错误，不代表身份核验或经过校准的正确概率。当前实现保留原始预测，不通过人工替字修饰结果。

## 主机测试复现

```sh
python3 experiments/run_host_experiments.py --cxx g++
python3 -m unittest discover -s tests -p 'test_*.py' -v
```

Windows 可通过 `--cxx` 指定 MinGW g++.exe。回放直接执行 `experiments/replay_consensus.cpp` 中调用的同一 C++ 类，不用 Python 重写投票算法。输出中的 `first_confirmation_ms` 来自人工指定的 1000ms 时间戳，不是测得的处理延迟。

已记录结果见 `experiments/results/host/report.json` 和 `consensus_metrics.csv`。例如孤立错误序列中，窗口 1/3/5 的错误确认次数分别为 2/0/0，首次确认时间分别为 0/2000/3000ms。三者确认输出次数分别为 10/7/6，必须同时报告覆盖率，不能单独用错误次数证明普遍准确率提升。交替错误与系统性错误仍存在。

## 真实图片实验

自行准备清晰标注的 JSON 清单，图片不要上传到公共仓库：

```json
[
  {"id":"positive_001","image":"images/car.png","plates":[{"text":"实际号码","bbox":[100,200,300,260]}]},
  {"id":"negative_001","image":"images/taxi.png","plates":[]}
]
```

`bbox` 必须是原图坐标的 `[x1,y1,x2,y2]`。真实车牌建议覆盖倾斜、远距离、弱光，负样本包含顶灯、广告和普通文字。调参与最终评测尽量使用不同图片。

先在 Ubuntu 使用原来的构建流程编译并部署升级源码，再运行：

```sh
python3 experiments/run_board_images.py /绝对路径/manifest.json \
  --serial b01129195659df85 --output /绝对路径/board_results
```

脚本通过 ADB 启动常驻后端，运行 baseline 和 score=0.2/0.3/0.4/0.5，共五种设置，保留原始 JSON、模型日志与逐请求计时。负样本错误候选数和错误号码数分别统计。检测采用 IoU≥0.5 的一对一匹配，整牌正确率以所有标注车牌为分母。当前匹配算法是按 IoU 贪心匹配，密集重叠目标场景需额外检查关联。

`host_round_trip_ms` 包含 USB/ADB 往返开销，不能当作板端推理耗时。

## 摄像头采样与性能实验

板端关闭其他本项目实例后运行，取景应尽量一致。此命令会显示应用并自动打开摄像头：

```sh
PLATE_TEST_SECONDS=60 sh experiments/run_sampling_test.sh /userdata/plate_weekly/run_001
python3 experiments/summarize_sampling.py /userdata/plate_weekly/run_001
```

每种采样间隔运行 60 秒。应分别测试同一静止车牌、轻微反光、移出画面，并记录场景起止时间。不同窗口的真实视频对照需保持输入一致，不能直接将三次不同拍摄当作严格配对实验。

后端 `results.json` 记录预处理、检测调用、后处理、裁剪、识别调用、CTC 解码；`timing.json` 记录模型初始化、读图、结果保存与后端总耗时。Qt JSONL 记录帧转换、编码和取帧至 UI 状态提交时间。UI 提交时间没有测量物理屏幕实际显示时刻，模型调用也不是纯 NPU 算子时间。

## 本周实验完成的判据

真实图片、真实视频和板端采样实验只有在取得原始样本清单、设备运行日志和可复算结果后才标记为完成。合成序列、测试夹具和历史数据都不得替代本轮硬件实测。
