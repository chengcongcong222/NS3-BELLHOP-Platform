"""Prepare ONE continuous author-reference noise realization, outside Runtime.

Not a per-packet service. Packet windows must read slices of the frozen result;
they must not regenerate or renormalize noise. The output is relative to the
normalized recording, not calibrated acoustic pressure. alpha < 2 has no
finite theoretical variance; its scale is not presented as ordinary SNR.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import re

import numpy as np
import scipy
from scipy.io import loadmat

EXPECTED_NOISEGEN_SHA256 = "11f09a3b51ee10abd0fef8f4276e0b6a246cd9de705fd3e249212e0cb3966550"


def sha256(path: Path) -> str:
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def generate(reference: Path, noise: dict, samples: int, rate: int,
             elements: list[int], seed: int, scale: float) -> np.ndarray:
    if type(samples) is not int or not 1 <= samples <= 10_000_000 or not elements or len(elements) > 64:
        raise ValueError("Noise preparation sample/channel budget exceeded")
    if type(rate) is not int or rate <= 0 or not 0 <= seed <= 2**32-1:
        raise ValueError("Explicit positive sample rate and uint32 seed required")
    if len(set(elements)) != len(elements) or min(elements) < 0 or not np.isfinite(scale) or scale < 0:
        raise ValueError("Invalid receiver element or amplitude scale")
    if samples * len(elements) > 20_000_000:
        raise ValueError("Noise preparation memory budget exceeded")
    alpha = float(np.asarray(noise["alpha"]).ravel()[0])
    if not 0 < alpha <= 2:
        raise ValueError("Noise alpha must be in (0, 2]")
    source = reference / "src/uwa_channels/noisegen.py"
    if sha256(source) != EXPECTED_NOISEGEN_SHA256:
        raise ValueError("Reference noise implementation does not match the lock")
    spec = importlib.util.spec_from_file_location("pinned_uwa_noise", source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    # This CLI is a run-preparation process. Restore state as an extra safeguard
    # for test callers; production must not call this inside a multi-Run server.
    previous = np.random.get_state()
    try:
        np.random.seed(seed)
        output = module.noisegen((samples, len(elements)), rate, elements, noise) * scale
    finally:
        np.random.set_state(previous)
    if output.shape != (samples, len(elements)) or not np.isfinite(output).all():
        raise ValueError("Reference returned invalid noise samples")
    return np.asarray(output, dtype="<f8")


def load_noise(path: Path) -> dict:
    import h5py
    if h5py.is_hdf5(path):
        with h5py.File(path, "r") as file:
            return {key: np.array(file[key]) for key in ("Fs", "R", "alpha", "beta", "fc", "version")}
    # v5 MAT beta is already M x M x K, not the reversed HDF5 order.
    raw = loadmat(path)
    return {key: raw[key] for key in ("Fs", "R", "alpha", "beta", "fc", "version")}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--reference", type=Path, required=True)
    p.add_argument("--noise", type=Path, required=True)
    p.add_argument("--samples", type=int, required=True)
    p.add_argument("--sample-rate", type=int, required=True)
    p.add_argument("--elements", type=int, nargs="+", required=True)
    p.add_argument("--seed", type=int, required=True)
    p.add_argument("--scale", type=float, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--id", required=True)
    p.add_argument("--name", required=True)
    p.add_argument("--citation", required=True)
    p.add_argument("--license", required=True)
    args = p.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,79}", args.id) or not args.name.strip():
        raise ValueError("Noise resource identity and name required")
    if args.output.exists():
        raise ValueError("Noise artifact is immutable: choose a new output directory")
    noise = load_noise(args.noise)
    data = generate(args.reference, noise, args.samples, args.sample_rate,
                    args.elements, args.seed, args.scale)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.output.parent, prefix=".noise-preparation-") as temporary:
        directory = Path(temporary)
        binary = directory / "noise.f64le"
        data.tofile(binary)
        fnv = 14695981039346656037
        with binary.open("rb") as stream:
            for block in iter(lambda: stream.read(1 << 20), b""):
                for byte in block: fnv = ((fnv ^ byte) * 1099511628211) & ((1 << 64)-1)
        alpha = float(np.asarray(noise["alpha"]).ravel()[0])
        manifest = {"schema": "recorded-continuous-noise-v1", "shape": list(data.shape),
                    "asset_id": args.id, "version": 1, "name": args.name.strip(),
                    "citation": args.citation, "license": args.license,
                    "dtype": "little-endian-float64", "layout": "time,selected_element",
                    "sample_rate_hz": args.sample_rate, "elements": args.elements, "seed": args.seed,
                    "amplitude_scale": args.scale, "alpha": alpha,
                    "reference": "normalized-library-noise-not-calibrated-pressure",
                    "power_semantics": "variance" if alpha == 2 else "alpha-stable-pseudo-power",
                    "noise_input_sha256": sha256(args.noise), "samples_sha256": sha256(binary),
                    "samples_fnv1a64": f"{fnv:016x}", "bytes": binary.stat().st_size,
                    "center_frequency_hz": float(np.asarray(noise["fc"]).ravel()[0]),
                    "bandwidth_hz": float(np.asarray(noise["R"]).ravel()[0]),
                    "reference_noisegen_sha256": EXPECTED_NOISEGEN_SHA256,
                    "numpy": np.__version__, "scipy": scipy.__version__}
        (directory / "manifest.json").write_text(json.dumps(manifest, indent=2)+"\n")
        directory.rename(args.output)
    print(json.dumps(manifest))


if __name__ == "__main__":
    main()
