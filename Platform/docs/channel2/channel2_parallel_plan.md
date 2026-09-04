# Channel 2 Parallel Development Plan

Both branches start from the P0-CH2-00 review commit published on
`feature/channel2-integration-baseline`. The integration owner reports the
immutable commit SHA after publication; both developers must fetch the branch
and verify that exact SHA before editing.

## Developer A — waveform/modem/BER

Branch: `feature/channel2-waveform-modem`

Owned paths:

- `Platform/phy/internal/` modem, waveform, artifact-facing Tx/Rx;
- `Platform/phy/tests/` waveform and BER tests;
- waveform-specific documentation under `Platform/docs/channel2/`.

First deliverable: production `WaveformTxPhy` and `WaveformRxPhy` implementing
the existing contracts, sharing `ITransmissionWaveformArtifacts`, with noise
disabled for the `Y=X*H` gate. Reuse existing packet adapter, modulation,
channel processor and true bit comparison. Do not change Environment asset
semantics or Runtime scheduling.

## Developer B — environment/Bellhop/CIR

Branch: `feature/channel2-environment-cir`

Owned paths:

- `Platform/environment/internal/` provider/import/normalization;
- `Platform/environment/tests/` and reference assets;
- environment-specific documentation under `Platform/docs/channel2/`.

First deliverable: prove that real Bellhop-derived canonical paths for a
reviewed asset become stable receiver-specific CIR input, with coverage,
units, phase, gain and delay provenance tested. Do not add modem policy,
sample buffers or online Bellhop execution to the provider.

## Shared integration area

The following require an integration-owner review and should not be edited
independently on either branch:

- `Platform/contracts/`;
- `Platform/runtime/`;
- `Platform/assembly/`;
- `physical_artifact_lifecycle.hpp`,
  `transmission_waveform_store.hpp`, and this architecture baseline;
- root/module CMake wiring that affects both branches.

Integration order is A/B branch tests first, then a small integration-owner
change in the shared area. Do not solve branch-local work by introducing a
second Runtime, new clock, global artifact store or Bellhop-specific public
contract.

## Integration gate

The gate is executable, never skipped:

1. one DigitalPacket becomes one original BitFrame and one X per
   TransmissionId;
2. at least two receivers resolve the identical immutable X;
3. each receiver applies its own canonical H using excess delays and absolute
   path gains;
4. with N/I disabled, Y is demodulated and payload bytes are recovered;
5. waveform BER is calculated from original versus recovered bits;
6. cycle completion releases the store;
7. Channel 1 Acceptance and all OFF/ON tests remain unchanged.

The baseline test proves items 1–6 at composition level. The first joint
integration must additionally execute them through ns-3 TxStart,
SignalArrival and SessionFinalize before Channel 2 is called production-ready.
