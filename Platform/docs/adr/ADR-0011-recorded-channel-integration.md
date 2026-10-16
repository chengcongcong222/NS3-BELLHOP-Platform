# ADR-0011: Recorded-channel integration and OSDM execution

- Status: Accepted for implementation; delivery gates remain open.
- Authorization: user-approved six-stage integration, 2026-10-16.
- Baseline: `7468e59df88348c304a0eb3f03ecc0be67d58b0b`.
- Incoming: `34ca091242e85a0a5cafffc071358c6c93309be6` (includes CIR branch).

## Scientific boundary

Mode A remains the scalar model, mode B remains Bellhop-based waveform
simulation. OSDM is a modem, not a channel or a fourth execution mode.
Mode C means replay of a measured channel estimate, not a new measurement
and not a Bellhop fallback. The UWA library author's format and reference
implementation are the reference, not an international standard.

Freeze upstream source revisions and input checksums before numerical
comparison. Recorded array elements are not independent network nodes;
independent trials are not simultaneous receivers. A recording is directed:
never fabricate its reverse or missing physical fan-out links.

Normalized coefficients do not establish absolute transmission loss or
received sound pressure. Synchronization-relative lag is not absolute
propagation delay. Any modeled flight-time reference and amplitude
calibration must be explicit and frozen. Without a usable delay reference,
offer waveform analysis only, not a physical MAC timing claim.

Official time-varying replay, including recorded phase/drift/resampling,
is the delivery criterion. The incoming packet-frozen sparse snapshot
provider stays a diagnostic adapter and must not be labeled equivalent.
Do not add trajectory Doppler on top of Doppler already in a recording.

## Execution boundary

Preserve ns-3 as the sole clock, phase order, closed cycles, real physical
fan-out and one Tx waveform per Transmission. Noise and samples remain
run-owned transient data; never serialize matrices into traces or DTOs.
No per-packet Python process, HTTP lookup, new scheduler or service.

Any recorded-response extension must be additive, explicitly tagged and
validated. It must carry a real bounded receive-time envelope and an opaque
run-owned operator identity, not invent scalar loss to satisfy a static
response contract. Legacy scalar/static-multipath behavior is unchanged.
Unknown coverage is an error, not NoArrival. No zero-delay +1 ns workaround.

Planning and execution use the same frozen source, but planners do not
receive future recording samples as an implicit oracle. Unsupported
prediction/calibration requirements fail preflight. Existing v1/v2/v3
definitions remain readable; new recorded definitions require a version.

OSDM airtime includes CRC, block padding, cyclic prefix and training.
Synchronization failure is a normal NotDecoded reception, not a failed
Run. No BER is reported without bit decisions. The training receiver and
its noise-knowledge assumptions must be identified; ideal true-channel
equalization remains an explicit research option.

## Product and release boundary

Reuse the unified workbench, immutable resource references and existing
run/result chain. Mode C hides Bellhop controls and exposes record segment,
receiver element/trial, supported band, modem, noise and timing assumptions.
Unknown geometry is illustrative, not a fabricated metric scene. No new
array-combining algorithm or measured-bearing claim is introduced.

Delivery requires pinned reference numerical tests, actual user paths,
OSDM/Bellhop integration, A/B/trajectory/algorithm regressions and negative
coverage tests. Float64 relative L2 <= 1e-6 is a proposed project criterion,
not an upstream guarantee; no fitted phase/gain/time alignment may hide an
error. Release only a tested build to the existing main entry after backup
and idle verification. Keep a rollback; no parallel deployment.

## Compatibility impact

Adding OSDM configuration expands backend/worker/frontend enum validation;
unknown enum values must fail rather than become BPSK. Appended optional
configuration preserves old source aggregate initialization where possible.
Any later channel-envelope extension must enumerate affected contracts and
tests here before changing ADR-0001 static-response semantics.

The additive contract is `RecordedChannelResponse`, a third channel outcome
with a run-local operator key, explicit recording sample offset, reference
delay and complete receive duration. It contains no absolute TL or static
paths. `ReceivedSignal` accepts either static or recorded response; scalar
PHY explicitly rejects recorded responses. Query receives actual encoded
duration. Recorded traces identify a modeled timing reference and normalized
gain; they must not serialize these as Bellhop Signal fields. Receiver window,
closed-cycle overlap and checked time arithmetic remain the same authority.
Tests cover overflow, identity, interval boundaries, scalar rejection and
serialization of both old and new response kinds.

Authored v4 selects a directed two-endpoint recording; v1/v2/v3 keep their paths.
A recorded scenario cannot carry a Bellhop spatial domain or moving trajectory.
Backend SHA256 and worker binary integrity checks precede use; repeat validates
channel and continuous noise. Noise is prepared by a pinned offline CLI into an
immutable seeded realization, selected explicitly and sliced on the run clock.
Changing the experiment seed does not replace this selected realization.
There is no scientific Python dependency in the serving or per-packet path.
C displays finite-window power ratios as non-calibrated, not SPL or alpha-stable
theoretical SNR. See `docs/channel3/README.md` for user/admin instructions.
