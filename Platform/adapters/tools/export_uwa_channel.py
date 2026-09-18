#!/usr/bin/env python3
"""Export an unpacked UWA Channel Library MAT file for Platform Channel 3."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


def csv_ints(value: str) -> list[int]:
    try:
        result = [int(item) for item in value.split(",")]
    except ValueError as error:
        raise argparse.ArgumentTypeError("expected comma-separated integers") from error
    if not result or any(item < 0 for item in result):
        raise argparse.ArgumentTypeError("indices and node IDs must be non-negative")
    return result


def finite_nonnegative(value: str) -> float:
    try:
        number = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("expected a number") from error
    if not math.isfinite(number) or number < 0.0:
        raise argparse.ArgumentTypeError("expected a finite non-negative number")
    return number


def finite_positive(value: str) -> float:
    number = finite_nonnegative(value)
    if number <= 0.0:
        raise argparse.ArgumentTypeError("expected a finite positive number")
    return number


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Unpack an Underwater Acoustic Channel Library MAT file with its "
            "official Python package and export Platform Channel 3 JSON+BIN."
        )
    )
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output-prefix", type=Path, required=True)
    parser.add_argument("--snapshot-rate-hz", type=finite_positive, required=True)
    parser.add_argument("--array-indices", type=csv_ints, required=True)
    parser.add_argument("--receiver-node-ids", type=csv_ints, required=True)
    parser.add_argument("--sender-node-id", type=int, required=True)
    parser.add_argument("--bandwidth-hz", type=finite_positive, required=True)
    parser.add_argument(
        "--frequency-tolerance-hz", type=finite_nonnegative, required=True
    )
    parser.add_argument("--cir-origin-delay-ns", type=int, required=True)
    parser.add_argument("--snapshot-origin-ns", type=int, required=True)
    parser.add_argument(
        "--minimum-tap-magnitude", type=finite_nonnegative, required=True
    )
    parser.add_argument("--buffer-left", type=finite_nonnegative, required=True)
    parser.add_argument("--buffer-right", type=finite_nonnegative, required=True)
    parser.add_argument(
        "--scalar-encoding", choices=("float32", "float64"), required=True
    )
    parser.add_argument("--byte-order", choices=("little", "big"), required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.sender_node_id < 0:
        raise SystemExit("sender-node-id must be non-negative")
    if args.cir_origin_delay_ns < 0:
        raise SystemExit("cir-origin-delay-ns must be non-negative")
    if len(args.array_indices) != len(args.receiver_node_ids):
        raise SystemExit(
            "array-indices and receiver-node-ids must contain the same count"
        )
    if len(set(args.receiver_node_ids)) != len(args.receiver_node_ids):
        raise SystemExit("receiver-node-ids must be unique")
    if len(set(args.array_indices)) != len(args.array_indices):
        raise SystemExit("array-indices must be unique")

    try:
        import numpy as np
        from uwa_channels import load_channel, unpack
    except ImportError as error:
        raise SystemExit(
            "Install the official converter dependency first: "
            "python -m pip install uwa-channels"
        ) from error

    channel = load_channel(str(args.input))
    fs_delay = float(channel["params"]["fs_delay"][0, 0])
    center_frequency_hz = float(channel["params"]["fc"][0, 0])
    if args.snapshot_rate_hz > fs_delay:
        raise SystemExit("snapshot-rate-hz cannot exceed the CIR delay sample rate")

    unpacked = unpack(
        args.snapshot_rate_hz,
        args.array_indices,
        channel,
        buffer_left=args.buffer_left,
        buffer_right=args.buffer_right,
    )
    if unpacked.ndim != 3:
        raise SystemExit("official unpack returned an unexpected tensor rank")
    delay_bins, receiver_count, snapshot_count = unpacked.shape
    if receiver_count != len(args.receiver_node_ids):
        raise SystemExit("official unpack returned an unexpected receiver count")
    if not np.all(np.isfinite(unpacked.real)) or not np.all(
        np.isfinite(unpacked.imag)
    ):
        raise SystemExit("unpacked CIR contains non-finite samples")

    # Official unpack returns [delay, receiver, time]. Runtime V1 consumes
    # [snapshot, receiver, delay, I/Q].
    runtime = np.transpose(unpacked, (2, 1, 0))
    endian = "<" if args.byte_order == "little" else ">"
    scalar = "f4" if args.scalar_encoding == "float32" else "f8"
    encoded = np.empty(runtime.shape + (2,), dtype=np.dtype(endian + scalar))
    encoded[..., 0] = runtime.real
    encoded[..., 1] = runtime.imag

    prefix = args.output_prefix
    prefix.parent.mkdir(parents=True, exist_ok=True)
    binary_path = prefix.with_suffix(".cir.bin")
    descriptor_path = prefix.with_suffix(".cir.json")
    encoded.tofile(binary_path)

    descriptor = {
        "format_version": 1,
        "data_file": binary_path.name,
        "scalar_encoding": args.scalar_encoding,
        "byte_order": args.byte_order,
        "complex_layout": "iq_interleaved",
        "tensor_layout": "snapshot_receiver_delay",
        "delay_bin_count": int(delay_bins),
        "receiver_count": int(receiver_count),
        "snapshot_count": int(snapshot_count),
        "delay_sample_rate_hz": fs_delay,
        "snapshot_rate_hz": args.snapshot_rate_hz,
        "center_frequency_hz": center_frequency_hz,
        "bandwidth_hz": args.bandwidth_hz,
        "center_frequency_tolerance_hz": args.frequency_tolerance_hz,
        "cir_origin_delay_ns": args.cir_origin_delay_ns,
        "snapshot_origin_ns": args.snapshot_origin_ns,
        "minimum_tap_magnitude": args.minimum_tap_magnitude,
        "sender_node_id": args.sender_node_id,
        "receiver_bindings": [
            {"node_id": node_id, "receiver_index": receiver_index}
            for receiver_index, node_id in enumerate(args.receiver_node_ids)
        ],
        "source": {
            "kind": "uwa_channels_unpacked",
            "input_file": args.input.name,
            "array_indices": args.array_indices,
            "buffer_left": args.buffer_left,
            "buffer_right": args.buffer_right,
        },
    }
    descriptor_path.write_text(
        json.dumps(descriptor, indent=2) + "\n", encoding="utf-8"
    )
    print(descriptor_path)
    print(binary_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
