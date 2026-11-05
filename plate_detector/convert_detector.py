from pathlib import Path
from rknn.api import RKNN

root = Path(__file__).resolve().parent
source = root / "model" / "plate_detect.onnx"
output = root / "model" / "plate_detect.rknn"

def check(ret, step):
    if ret != 0:
        raise RuntimeError("{}失败，返回码：{}".format(step, ret))

rknn = RKNN(verbose=True)
try:
    print("1. 配置 RK3568", flush=True)
    rknn.config(
        target_platform="rk3568",
        mean_values=[[0, 0, 0]],
        std_values=[[255, 255, 255]]
    )

    print("2. 加载三个原始检测头", flush=True)
    check(rknn.load_onnx(
        model=str(source),
        outputs=["792", "912", "1032"]
    ), "加载模型")

    print("3. 构建浮点 RKNN 模型", flush=True)
    check(rknn.build(do_quantization=False), "构建模型")

    print("4. 导出 RKNN 文件", flush=True)
    check(rknn.export_rknn(str(output)), "导出模型")
    print("转换成功：{}".format(output), flush=True)
finally:
    rknn.release()
