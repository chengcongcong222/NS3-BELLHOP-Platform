# External CIR adapter (Channel 3)

Channel 3 imports a measured or externally generated complex-baseband channel
impulse response. It does not run Bellhop and it does not import an already
received/noisy waveform. The imported response is converted to the same
`ChannelFieldResponse` boundary used by Channel 2, after which the existing
waveform PHY applies the channel, adds configured receiver noise, and
demodulates.

## Normalized package V1

The adapter reads one JSON descriptor and one headerless binary tensor. No
interpretation defaults are inferred. The binary tensor order is:

```text
[snapshot][receiver][delay][I,Q]
```

`I` and `Q` are either IEEE-754 float32 or float64 values in the explicitly
declared byte order. A minimal descriptor is:

```json
{
  "format_version": 1,
  "data_file": "channel.cir.bin",
  "scalar_encoding": "float64",
  "byte_order": "little",
  "complex_layout": "iq_interleaved",
  "tensor_layout": "snapshot_receiver_delay",
  "delay_bin_count": 256,
  "receiver_count": 1,
  "snapshot_count": 1,
  "delay_sample_rate_hz": 6000.0,
  "snapshot_rate_hz": 1.0,
  "center_frequency_hz": 25000.0,
  "bandwidth_hz": 4000.0,
  "center_frequency_tolerance_hz": 0.0,
  "cir_origin_delay_ns": 0,
  "snapshot_origin_ns": 0,
  "minimum_tap_magnitude": 0.0,
  "sender_node_id": 1,
  "receiver_bindings": [
    {"node_id": 2, "receiver_index": 0}
  ]
}
```

For one snapshot, the response is treated as quasi-static for the run. With
multiple snapshots, the query emission time selects
`floor((time - snapshot_origin) * snapshot_rate_hz)`. Queries outside the
recorded time, node, carrier, or bandwidth coverage fail explicitly; the
adapter never clamps or fabricates a direct path.

The Underwater Acoustic Channel Library stores compressed HDF5/MAT tensors
`h_hat[K,M,T]` together with `phi_hat` or `theta_hat`. Those source files
must first be unpacked with the library's own replay/unpack implementation and
exported to this normalized package. Direct compressed MAT import is not part
of V1 because ignoring the phase/delay trajectory would produce a physically
incorrect channel.

Use the offline converter after installing the official Python toolbox:

```bash
python -m pip install uwa-channels
python Platform/adapters/tools/export_uwa_channel.py \
  --input blue_1.mat \
  --output-prefix ./blue_1_platform \
  --snapshot-rate-hz 20 \
  --array-indices 0,2,4 \
  --receiver-node-ids 2,3,4 \
  --sender-node-id 1 \
  --bandwidth-hz 6250 \
  --frequency-tolerance-hz 0 \
  --cir-origin-delay-ns 0 \
  --snapshot-origin-ns 0 \
  --minimum-tap-magnitude 1e-12 \
  --buffer-left 0.1 \
  --buffer-right 0.1 \
  --scalar-encoding float64 \
  --byte-order little
```

The buffer sizes and physical metadata are command-line inputs rather than
