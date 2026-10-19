"""Versioned authoring suggestions, never defaults for saved/run definitions.

Changing a profile requires a new version. Historical explicit values always win.
These engineering configurations are not online adaptive waveform optimization.
"""
from copy import deepcopy


_PROFILES = [
    {
        "id": "bellhop-osdm", "version": 1, "physical_layer": "Channel2Waveform",
        "name": "Bellhop OSDM 标准配置",
        "osdm": {"block_length": 256, "vector_length": 16, "cyclic_prefix": 64,
                 "constellation": "Qpsk", "minimum_sync_correlation": 0.05,
                 "maximum_sync_search": 128},
    },
    {
        "id": "recorded-osdm", "version": 1, "physical_layer": "Channel3Recorded",
        "name": "实录 OSDM 标准配置",
        "osdm": {"block_length": 256, "vector_length": 16, "cyclic_prefix": 128,
                 "constellation": "Qpsk", "minimum_sync_correlation": 0.05,
                 "maximum_sync_search": 256},
    },
]


def experiment_presets() -> list[dict]:
    """Return detached values: a caller cannot mutate subsequent responses."""
    return deepcopy(_PROFILES)
