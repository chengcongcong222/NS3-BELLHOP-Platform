"""Opt-in numerical reference test; no Runtime or service is started."""
import importlib.util
from pathlib import Path
import sys
import numpy as np

source = Path(__file__).resolve().parents[1] / "tools/prepare_recorded_noise.py"
spec = importlib.util.spec_from_file_location("preparation", source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
reference = Path(sys.argv[1])
beta = np.zeros((2, 2, 3))
beta[0, 0, 0] = 1.; beta[1, 0, 0] = .5; beta[1, 1, 1] = .75
for alpha in (2., 1.7):
    noise = {"Fs": np.array([[8000.]]), "R": np.array([[2000.]]), "fc": np.array([[2000.]]),
             "alpha": np.array([[alpha]]), "version": np.array([[1.]]), "beta": beta}
    first = module.generate(reference, noise, 4000, 8000, [0, 1], 61, .1)
    second = module.generate(reference, noise, 4000, 8000, [0, 1], 61, .1)
    assert np.array_equal(first, second)
    # Reordering output elements must not create new latent noise streams.
    reordered = module.generate(reference, noise, 4000, 8000, [1, 0], 61, .1)
    assert np.array_equal(first[:, ::-1], reordered)
    # Shared time samples, not different packet-local RNG realizations.
    assert np.array_equal(first[100:300][50:], first[150:300])
    for invalid in ([-1], [2], [0, 0]):
        try:
            module.generate(reference, noise, 10, 8000, invalid, 61, .1)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid array selection accepted")
    print(f"alpha={alpha}: deterministic continuous noise and array covariance mapping passed")
