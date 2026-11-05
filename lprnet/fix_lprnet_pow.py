"""Replace exactly four LPRNet Pow(x, 2) nodes with equivalent Mul(x, x).

The original ONNX is never overwritten. All other nodes, initializers, graph
annotations, inputs, outputs, and model metadata are retained unchanged.
"""

import argparse
import copy
import os
from pathlib import Path
import tempfile

import numpy as np
import onnx
from onnx import numpy_helper


def fix_lprnet_pow(source, destination):
    source = Path(source).resolve()
    destination = Path(destination).resolve()
    if source == destination:
        raise ValueError("Use a different output path; preserve the original ONNX")

    original = onnx.load(str(source))
    model = copy.deepcopy(original)
    constants = {tensor.name: tensor for tensor in model.graph.initializer}
    pow_nodes = [node for node in model.graph.node if node.op_type == "Pow"]
    if len(pow_nodes) != 4:
        raise ValueError("Expected exactly four Pow nodes, found {}".format(len(pow_nodes)))

    # Validate every candidate before changing any node.
    for node in pow_nodes:
        if node.domain not in ("", "ai.onnx") or len(node.input) != 2 or len(node.output) != 1:
            raise ValueError("Unexpected Pow signature: " + node.name)
        if node.attribute:
            raise ValueError("Unexpected Pow attributes: " + node.name)
        exponent_tensor = constants.get(node.input[1])
        if exponent_tensor is None:
            raise ValueError("Pow exponent must be an initializer: " + node.name)
        exponent = numpy_helper.to_array(exponent_tensor)
        if exponent.size != 1 or not np.isfinite(exponent).all() or float(exponent.reshape(-1)[0]) != 2.0:
            raise ValueError("Pow exponent must be the constant 2: " + node.name)

    changed = []
    for node in pow_nodes:
        changed.append(node.name)
        node.op_type = "Mul"
        node.input[1] = node.input[0]

    # This intentionally retains the now-unused exponent initializers as well.
    # Verify that the entire graph differs only in these operator/input fields.
    expected = copy.deepcopy(original)
    for node in expected.graph.node:
        if node.op_type == "Pow":
            node.op_type = "Mul"
            node.input[1] = node.input[0]
    if model.SerializeToString() != expected.SerializeToString():
        raise RuntimeError("Unexpected additional graph modification")
    onnx.checker.check_model(model)

    destination.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary_name = tempfile.mkstemp(
        prefix=destination.stem + ".", suffix=".pending.onnx", dir=str(destination.parent))
    os.close(handle)
    temporary_path = Path(temporary_name)
    try:
        onnx.save(model, str(temporary_path))
        onnx.checker.check_model(onnx.load(str(temporary_path)))
        os.replace(str(temporary_path), str(destination))
    finally:
        if temporary_path.exists():
            temporary_path.unlink()
    return changed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    names = fix_lprnet_pow(args.source, args.destination)
    print("Replaced four Pow(x, 2) nodes with Mul(x, x): " + ", ".join(names))
    print("Saved: " + str(args.destination.resolve()))


if __name__ == "__main__":
    main()
