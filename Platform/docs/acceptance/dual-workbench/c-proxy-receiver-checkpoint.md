# C proxy receiver and binding preflight checkpoint

2026-10-20; base `7717119`. **P0_DUAL_WORKBENCH_BLOCKED**.
This is not delivery of the requested four-stage C product.

## Implemented

`POST /authoring/recorded-proxy/validate` accepts the strict
`ns3-factory-recorded-proxy-bindings-v1` request model in `recorded_proxy.py`.
It is read-only: no Run, resource publication, draft mutation or Worker start.
It explicitly returns `execution_ready: false`. The response is a resolved
binding preview, **not** an executable authored-v5 RunDefinition.

- Enumerate every non-self Tx-capable to Rx-capable physical pair, independently
  of application flows. Missing overhearing/reverse bindings are errors, not
  inferred channels or fabricated NoArrival.
- Explicit, global-same and ordered-pool assignment with per-edge overrides.
  Canonical numeric node ordering makes input node order irrelevant.
- Verify channel/noise resource files through existing SHA256 inspection.
  Freeze the inspected manifests and hashes in the response, with synthetic
  relationship labels; no sample tensors appear in HTTP.
- Require explicit receiver-owned noise selection, compatible band/clock,
  positive modeled delay, adequate recording coverage including a conservative
  tail allowance, and no geometry fields in the physical binding request.
- Request collection limits are parser guards, **not measured supported network
  capacities**. Full encoded-waveform, schedule, peak-memory and Worker admission
  checks remain necessary before any run endpoint can be enabled.

The new `RecordedProxyWaveformChannel` can be supplied to `WaveformRxPhy`.
The receiver obtains interferers' emitted waveform artifacts, not their source
truth bits; keeps artifact ownership during processing; and passes the received
mixture through the existing BPSK/BFSK/OSDM decoder and CRC. Truth comparison is
still after receiver decisions. Old v4 retains its isolated-reception rejection
and its unchanged `RecordedWaveformChannel::Process`.

## Explicit oscillator convention

The proxy experiment uses packet-reset Tx oscillators. Each directed recording
is replayed using the existing library-compatible real-passband operator.
Outputs are placed on a common receiver passband grid using modeled arrival
times, summed, then downconverted in the desired arrival's local oscillator
frame and resampled. This preserves the single-link v4 numerical convention
without directly adding packet-local complex baseband arrays.

This is a **synthetic oscillator/timing assumption**, not recovered simultaneous
phase or transmitter identity from the UWA sea trial. D3 remains NO-GO.
Fractional passband offsets and fractional recording start offsets are rejected,
not rounded. Future scheduling must respect both grids or supply a separately
validated fractional-delay transformation. Arbitrary TDMA schedules have not
yet been certified compatible.

One receiver has one shared continuous passband noise realization. Each desired
receive window slices that same realization by absolute time and adds it once,
after signal summation. Independent receivers may explicitly choose independent
realizations; assigning the same noise elsewhere must remain a synthetic
correlation assumption.

## Executed checks and a retained negative result

- Backend binding/API tests: 11 passed using small synthetic binary resources.
  Cover full fan-out, canonical ordering, explicit missing-link rejection,
  per-edge overrides, corruption, incompatible metadata, geometry exclusion and
  no Worker admission. These are not official-UWA numerical evidence.
- C++ Debug receiver tests exercise three modulations and two payload sizes:
  exact isolated proxy/v4 waveform and noise equality; unequal amplitudes;
  complete phase cancellation; a one-passband-sample non-integer-carrier-cycle
  offset; separated and partial overlap; unchanged noise; repeat equality;
  invalid clocks and noise ownership; real decoder/CRC outcomes.
- Initial cancellation assertion failed for a four-byte test frame. Hard
  decisions after cancellation can form another valid CRC codeword. The test
  now **retains** this case and verifies its nonzero payload bit errors instead
  of using truth to force NotDecoded. Five-byte fixtures provide deterministic
  interference-failure / interference-removal-success checks. CRC success is
  not proof of zero errors or of interference absence.
- Existing recorded bridge and waveform PHY tests passed.
- Full backend regression: 364 passed, 15 skipped; the two real old-C Worker
  cases were explicitly run separately, not counted as passed skips.
- Real ns-3 old-v4 Blue E2E: noiseless and continuous-noise cases passed,
  including exact-config repetition and resource-negative checks. This does
  not prove new-v5 Worker execution or a fresh official replay comparison.

Commands:

```text
cmake --build /tmp/dual-workbench-build-off --target platform_phy_recorded_proxy_receiver_test platform_phy_recorded_waveform_channel_test platform_phy_waveform_phy_test -j2
ctest --test-dir /tmp/dual-workbench-build-off -R 'platform_phy_(recorded_proxy_receiver|recorded_waveform_channel|waveform_phy)_test' --output-on-failure
PYTHONPATH=Platform/backend/src /tmp/dual-workbench-build-off/backend/venv/bin/python -m pytest Platform/backend/tests/recorded_proxy_test.py -q
```

Real-worker tests additionally used the ON worker, `PLATFORM_RECORDED_TEST_ASSET`
and `PLATFORM_RECORDED_TEST_NOISE` pointing to temporary copies of the existing
Blue element-0 and continuous-noise assets. No platform service was started,
stopped, updated or switched.

## Required next integration, not delivered

1. Authored-v5 executable schema, immutable resource freeze/repeat validation,
   Worker loader and receiver-owned shared noise assembly.
2. Protocol/scheduling preflight for exact sample-grid alignment, closed-cycle
   envelopes and measured aggregate resource budgets; causal/half-duplex ns-3
   integration tests using the actual multi-node execution path.
3. Mixed-reception trace provenance and power semantics. Current mixture signal
   power includes interferers and must not be presented as desired-link SNR.
4. Logical canvas, global pool/override editor, save/reopen, result views and
   layout non-causality checks. No proxy UI has been enabled.
5. Full U/P/R gates, official recording mixture references and old-trace
   equivalence. BearingFusion paired common observations remain separate work.
