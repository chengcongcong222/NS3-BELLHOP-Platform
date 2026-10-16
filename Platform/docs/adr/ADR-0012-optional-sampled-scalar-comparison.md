# ADR-0012: Optional sampled-scalar comparison baseline

- Status: Proposed for integration review
- Scope: Channel 1/2 comparison experiments only
- Extends: ADR-0001 sections 2.17, 2.35 and 2.45; ADR-0003/0004

## Decision

The existing Channel 1 acceptance composition, modeled BER evidence, worker
protocol, and frontend defaults remain unchanged. A caller must explicitly
select a separate sampled-scalar PHY composition for comparison experiments.
It uses the existing Tx/channel/noise/Rx contracts and ns-3 event authority.

The optional transmitter retains one immutable, cycle-bounded copy of the
payload and the same explicitly selected payload-plus-CRC32 test frame used
by the waveform composition. The packet metadata and Runtime contracts do
not carry physical samples or the original payload to the receiver.

The optional receiver computes the frozen coherent BPSK-AWGN scalar bit-error
probability from aggregate loss, equivalent in-band noise power, occupied
bandwidth, and configured bit rate. An explicit run seed mixed with stable
TransmissionId and receiver NodeId drives independent per-bit draws. It
flips actual frame bits, validates the resulting CRC, and delivers recovered
bytes only on CRC success. Equal requests, configuration, and artifacts
produce equal outcomes; no global RNG, wall clock, or iteration-order seed
is used. Overlapping signals are unsupported rather than silently ignored.

Modeled RxQualityEvidence remains non-causal in the formal P0 acceptance
path. The optional comparison receiver does not publish modeled quality
evidence or modify the formal acceptance BER verdict. Its post-decision
observer reports sampled payload bit errors and CRC outcome separately.

Comparison metrics count only current-hop unicast target attempts. PER
includes no arrival, CRC failure, and observed payload corruption even if
the CRC happens to pass. BER divides observed payload-bit errors
by observed target payload bits; no-arrival contributes no invented bits and
is reported as missing BER coverage. Overheard receptions never enter either
denominator. Comparing runs requires the same ordered target
TransmissionId/PacketId/receiver identities and payload-bit count.

## Consequences

This is a simulation baseline, not hardware BER evidence and not a claim
that the waveform path always performs better. Full cross-channel evaluation
with matched noise bandwidth, many packets, and confidence intervals is a
separate experiment step. Public contracts and default acceptance behavior
are unchanged; integration ownership review is required before merging.
