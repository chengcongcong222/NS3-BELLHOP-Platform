"""Explicit opt-in comparison against a pinned author checkout, no service.

Usage: python official_replay_equivalence_test.py --reference /path/to/python
       --probe /path/to/recorded_replay_probe [--blue /path/to/blue_1.mat]
Synthetic cases exercise numerics, not experimental evidence. --blue adds a
short, checksum-verified real recording; raw data never enter Git.
"""
import argparse
from fractions import Fraction
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import numpy as np

REVISION = "dbf373ca7cd07ac4eb1b24a191d76bea18f73560"


def compare(probe, replay, channel, fs, signal, start, label):
    h = channel["h_hat"]["real"] + 1j * channel["h_hat"]["imag"]
    ratio = Fraction(float(channel.get("f_resamp", np.array([[1.]]))[0, 0])).limit_denominator()
    phase = channel.get("phi_hat", channel.get("theta_hat"))
    request = {"fc": float(channel["params"]["fc"][0, 0]),
               "fs_delay": float(channel["params"]["fs_delay"][0, 0]),
               "fs_time": float(channel["params"]["fs_time"][0, 0]),
               "snapshots": h.shape[0], "delay_taps": h.shape[2],
               "h": np.column_stack((h[:, 0, :].ravel().real, h[:, 0, :].ravel().imag)).tolist(),
               "phase": [] if phase is None else phase[:, 0].tolist(),
               "delay_tracking": "phi_hat" in channel,
               "resample_up": ratio.numerator, "resample_down": ratio.denominator,
               "input": signal.tolist(), "fs": fs, "start": start}
    expected = replay(signal, fs, [0], channel, start=start)[:, 0]
    completed = subprocess.run([str(probe)], input=json.dumps(request), text=True,
                               capture_output=True, check=True)
    actual = np.array(json.loads(completed.stdout))
    assert actual.shape == expected.shape, (label, actual.shape, expected.shape)
    error = np.linalg.norm(actual - expected) / max(np.linalg.norm(expected), 1e-30)
    assert error <= 1e-6, (label, error)
    # Exact repeat, no fit/rotation/normalization/time alignment.
    repeated = subprocess.run([str(probe)], input=json.dumps(request), text=True,
                              capture_output=True, check=True)
    assert completed.stdout == repeated.stdout
    request["start"] = 10**9
    invalid = subprocess.run([str(probe)], input=json.dumps(request), text=True, capture_output=True)
    assert invalid.returncode != 0 and not invalid.stdout
    print(json.dumps({"case": label, "samples": len(actual), "relative_l2": error,
                      "repeat_exact": True, "out_of_coverage_rejected": True}))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--reference", type=Path, required=True)
    p.add_argument("--probe", type=Path, required=True)
    p.add_argument("--blue", type=Path)
    p.add_argument("--blue-elements", type=int, nargs="+", default=[0])
    args = p.parse_args()
    assert subprocess.check_output(["git", "-C", str(args.reference), "rev-parse", "HEAD"], text=True).strip() == REVISION
    spec = importlib.util.spec_from_file_location("official_replay", args.reference / "src/uwa_channels/replay.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    rng = np.random.default_rng(71)
    fs, fc, fd, ft = 16000, 4000, 1000, 8
    n, l = 20, 12
    h = (rng.normal(size=(n,1,l)) + 1j*rng.normal(size=(n,1,l))) / 20
    t = np.arange(3200)/fs
    signal = np.cos(2*np.pi*fc*t)*(0.7+0.3*np.sin(2*np.pi*70*t))
    base = {"version": np.array([[1.0]]), "params": {"fc": np.array([[fc]]),
        "fs_delay": np.array([[fd]]), "fs_time": np.array([[ft]])},
        "h_hat": {"real": h.real, "imag": h.imag}}
    phase = (0.2*np.sin(np.arange(2500)/300))[:,None]
    for label, extra in [("time_varying", {}), ("phase", {"theta_hat":phase}),
                         ("delay_drift", {"phi_hat":phase}),
                         ("phi_precedence", {"phi_hat":phase,"theta_hat":phase*30}),
                         ("resampling", {"phi_hat":phase,"f_resamp":np.array([[1.001]])})]:
        compare(args.probe,module.replay,{**base,**extra},fs,signal,120,label)
    if args.blue:
        import h5py
        with args.blue.open("rb") as stream:
            assert hashlib.file_digest(stream, "md5").hexdigest() == "6776481ad636e40b1d6038fa37121fb7"
        for element in args.blue_elements:
          with h5py.File(args.blue) as f:
            # Keep the complete tap axis and one selected physical sensor;
            # select enough early snapshots/phase for a short actual window.
            assert 0 <= element < f["h_hat"].shape[1]
            values=f["h_hat"][:20,element:element+1,:]
            channel={"version":np.array(f["version"]), "params":{k:np.array(f["params"][k]) for k in ("fc","fs_delay","fs_time")},
                     "h_hat":{"real":values["real"],"imag":values["imag"]}}
            for key in ("phi_hat","theta_hat"):
                if key in f: channel[key]=f[key][:20000,element:element+1]
            if "f_resamp" in f: channel["f_resamp"]=np.array(f["f_resamp"])
            fc=float(channel["params"]["fc"][0,0]); fs=int(4*fc)
            t=np.arange(int(fs*.05))/fs
            compare(args.probe,module.replay,channel,fs,np.cos(2*np.pi*fc*t)*np.sin(2*np.pi*200*t),100,f"blue_1_element_{element}")
    else:
        print("REAL_DATA_NOT_RUN (synthetic numerical comparisons only)")


if __name__ == "__main__":
    main()
