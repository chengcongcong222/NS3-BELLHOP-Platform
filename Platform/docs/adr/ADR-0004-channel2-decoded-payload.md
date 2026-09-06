# ADR-0004: Channel 2 decoded payload delivery

- Status: Proposed for integration review; implemented on the waveform branch
- Scope: User-approved feature payload / Bellhop waveform integration
- Extends: ADR-0001 sections 2.31, 2.35 and ADR-0003

## Decision

An additive optional recovered payload on `RxDecodeResult` carries receiver
bytes. Runtime preserves routing identity from the transmission record but
uses recovered bytes for local delivery and relay. Existing scalar providers
omit it and retain their original delivery behavior. Waveform success requires
a recovered payload; failed waveform reception never delivers an original copy.

Receiver decisions use an explicitly selected PHY-internal CRC-32 trailer,
not original simulation bits. Original bits are only compared after decode
for BER observation. CRC is not error correction or a hardware frame standard;
its overhead is included in transmission time. Timing/phase sync stays ideal.

Reuse the existing feature encoder and packet queue. Load Bellhop arrivals
outside Runtime and query receiver-specific paths via the existing provider.
Add noise after the channel: `Y = H(X) + N`. No online Bellhop, new feature
schema, new simulation clock, or second scheduler is introduced.

## Compatibility and review

Old result factories and Channel 1 remain supported. The C++ object layout
changes, requiring consumers to rebuild; binary ABI compatibility is not
promised. Waveform samples remain internal. Shared contracts/runtime/assembly
changes require the integration owner's PR review before merging; this ADR
does not claim upstream acceptance.

## Implementation notes

- The explicit Channel 2 executor overload uses the existing feature application,
  Bellhop asset provider, runtime and ns-3 gateway. Default frontend/worker runs
  remain Channel 1.
- CRC32 framing is an internal, explicit demo profile, not a freeze of any
  external BIN/HIL format. Planning airtime accounts for its four-byte overhead.
- Normalized Bellhop paths are mapped to the first-arrival complex baseband
  reference, including relative carrier-delay phase. Absolute path gains are
  not normalized, aggregate TL is not reapplied, and first delay is not duplicated.
- Existing scalar NoiseObservation remains required by the outer request
  contract. Waveform noise comes solely from its explicit NoiseProfile, after H.
- Optional observer BER is post-decision simulation evidence, not Rx decision
  input or a hardware measurement. Legacy scalar BER evidence is not fabricated.
- `WaveformPipelineResult::noisy_received()` now explicitly retains raw H(X)+N,
  before ideal phase correction; the decoder works on its own corrected copy.
- See `Platform/phy/README.md` for reproducible commands and receiver limitations.
