"""Build the minimal LPRNet compatibility fix with the existing RKNN 1.3 SDK.

Keep model/lprnet.onnx unchanged. Export to a temporary RKNN file, and replace
model/lprnet.rknn atomically only after a successful build/export/release.
"""

import os
from pathlib import Path
import tempfile

from rknn.api import RKNN
from fix_lprnet_pow import fix_lprnet_pow


def check(ret, operation):
    if ret != 0:
        raise RuntimeError("{} failed, return code {}".format(operation, ret))


def main():
    root = Path(__file__).resolve().parent
    model_dir = root / "model"
    source = model_dir / "lprnet.onnx"
    compatible = model_dir / "lprnet_compat.onnx"
    output = model_dir / "lprnet.rknn"

    print("1. Replace only the four constant-square Pow nodes", flush=True)
    changed = fix_lprnet_pow(source, compatible)
    print("Changed: " + ", ".join(changed), flush=True)

    handle, pending_name = tempfile.mkstemp(
        prefix="lprnet.export.", suffix=".pending.rknn", dir=str(model_dir))
    os.close(handle)
    pending = Path(pending_name)
    try:
        rknn = RKNN(verbose=True)
        try:
            print("2. Configure RK3568 and the original normalization", flush=True)
            rknn.config(
                target_platform="rk3568",
                mean_values=[[127.5, 127.5, 127.5]],
                std_values=[[127.5, 127.5, 127.5]])

            print("3. Load the compatibility ONNX", flush=True)
            check(rknn.load_onnx(model=str(compatible)), "load_onnx")

            print("4. Build with the original floating-point configuration", flush=True)
            check(rknn.build(do_quantization=False), "build")

            print("5. Export to a temporary RKNN file", flush=True)
            check(rknn.export_rknn(str(pending)), "export_rknn")
            if not pending.is_file() or pending.stat().st_size == 0:
                raise RuntimeError("RKNN export did not produce a nonempty file")
        finally:
            rknn.release()

        os.replace(str(pending), str(output))
        print("Conversion successful: {} ({} bytes)".format(output, output.stat().st_size), flush=True)
    finally:
        if pending.exists():
            pending.unlink()


if __name__ == "__main__":
    main()
