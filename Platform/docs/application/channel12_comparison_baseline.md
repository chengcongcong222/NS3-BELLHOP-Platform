# Channel 1/2 target-packet metric baseline

This is an **opt-in comparison facility**, not a replacement for the P0
acceptance report or a claim that Channel 2 is better. The ordinary
AcceptanceRunExecutor constructor and frontend/worker defaults are unchanged.
See ADR-0005 for the boundary decision.

## Common accounting (step 1)

BuildTargetPacketMetrics consumes the run's TraceEvent stream plus per-receiver
payload-bit/CRC observations from either PHY. It selects only the intended
unicast receiver for each TransmissionId, then reports:

- BER = wrong **payload** bits / observed target payload bits. CRC bits are
  excluded from both numerator and denominator. An absent signal has no
  received bits, so it contributes no fabricated BER sample; the separate
  no_arrival_count shows that coverage gap.
- PER = failed target packets / attempted target packets. A packet fails if
  the signal never arrives, the CRC fails, or its decoded payload differs
  from the transmitted payload (including a hypothetical undetected CRC
  collision). Overheard and broadcast receptions are excluded.
- The collector rejects missing, duplicated, or contradictory reception
  evidence. ValidateSameTargetAttempts checks that the two runs contain the
  same ordered TransmissionId, PacketId, target NodeId, and payload-bit count.

The metric is a **current-hop target-packet** metric, not an end-to-end
application reliability figure. A run with no observed target bits has no BER
value; a run with no target attempts has no PER value.

## Optional sampled-scalar Channel 1 (step 2)

Construct AcceptanceRunExecutor with ScalarSamplingExecutionOptions containing
an explicit deterministic seed and a live ISampledScalarReceptionObserver.
The experiment must set RxQualityMode::kNone. The transmitter creates the
same 15-byte feature payload + CRC32 frame as the waveform test path, and the
airtime includes those four CRC bytes. The receiver uses the existing
coherent-BPSK/AWGN scalar formula to obtain a per-bit error probability from
aggregate channel loss, noise, bandwidth, and bit rate. It samples every
frame bit with a seed tied to TransmissionId and receiver NodeId, checks the
corrupted CRC, and delivers the recovered payload only on CRC success.

This mode still has **no transmitted waveform**. Its bit flips model a
memoryless link, not actual multipath interference or demodulation. It does
not put the modeled probability into the formal P0 BER evidence field.
The default Channel 1 path therefore retains its original semantics.

The waveform Channel 2 path supplies the same observation fields after
actual demodulation; both pass through the common collector. A test runs
the same two-cycle reference Bellhop scenario through both compositions and
verifies packet-identity alignment and scalar reproducibility:

```sh
cmake --build /home/administratorlzx/build/ns3-factory-platform-ns3 \
  --target platform_application_channel12_comparison_baseline_test
ctest --test-dir /home/administratorlzx/build/ns3-factory-platform-ns3 \
  --output-on-failure -R platform_application_channel12_comparison_baseline_test
```

That test validates the accounting plumbing; its particular BER/PER values
are **not** a fair performance ranking. A subsequent comparison experiment
must match scenario, packet set, effective noise power/bandwidth, and seeds
where meaningful, then use enough packets and uncertainty bounds before
claiming Channel 2 has lower BER/PER.
