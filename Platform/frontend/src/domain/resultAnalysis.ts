import type { ExperimentDto, ResultDto, RunEventDto, TransmissionTraceDto } from "../api/types";
import acceptanceBaseline from "../../../acceptance/acceptance4_baseline_v1.json";

// Requirements are reference text, never an inferred experiment configuration.
export const acceptanceReference = acceptanceBaseline.hard_requirements;

export function matchingRunExperiment(result: ResultDto, experiment?: ExperimentDto | null) {
  return experiment?.experiment_id === result.experiment_id && experiment.version === result.experiment_version ? experiment : null;
}

export type PhysicalMode = "A" | "B" | "C" | "unknown";

export function physicalMode(result: ResultDto, implementation?: string | null): PhysicalMode {
  const provenance = result.provenance;
  const recorded = provenance?.physical_layer ?? implementation;
  if (recorded === "Channel3Recorded") return "C";
  if (recorded === "Channel1Abstract" || recorded === "Channel1") return "A";
  if (recorded === "Channel2Waveform" || recorded === "Channel2") return "B";
  // Actual waveform evidence identifies B; absence of evidence cannot identify A.
  return result.visualization?.waveform.length ? "B" : "unknown";
}

export function sampleSummary(values: readonly number[]) {
  const finite = values.filter(Number.isFinite);
  if (!finite.length) return null;
  return {
    count: finite.length,
    minimum: finite.reduce((a, b) => Math.min(a, b)),
    mean: finite.reduce((sum, value) => sum + value, 0) / finite.length,
    maximum: finite.reduce((a, b) => Math.max(a, b)),
  };
}

/** Received waveform powers, never a scalar channel-model substitute. */
export function waveformSignalToNoise(powers?: { signal_power_upa2?: number; noise_power_upa2?: number;
  signal_power_relative?: number; noise_power_relative?: number } | null): number | "no-noise" | "no-signal" | null {
  const signal = powers?.signal_power_relative ?? powers?.signal_power_upa2;
  const noise = powers?.noise_power_relative ?? powers?.noise_power_upa2;
  if (signal === undefined || noise === undefined || !Number.isFinite(signal) || !Number.isFinite(noise)
      || signal < 0 || noise < 0) return null;
  if (noise === 0) return "no-noise";
  if (signal === 0) return "no-signal";
  return 10 * (Math.log10(signal) - Math.log10(noise));
}

export function waveformSummary(result: ResultDto) {
  const records = result.visualization?.waveform ?? [];
  const bits = records.reduce((sum, item) => sum + BigInt(item.payload_bit_count), 0n);
  const errors = records.reduce((sum, item) => sum + BigInt(item.bit_error_count), 0n);
  return {
    frames: records.length,
    bits,
    errors,
    ber: bits ? Number(errors) / Number(bits) : null,
    crcPassed: records.filter(item => item.crc_ok).length,
    crcFailed: records.filter(item => !item.crc_ok).length,
  };
}

export function resultEventStatistics(result: ResultDto, input: readonly RunEventDto[], streamComplete = false) {
  // A result drawer must not accidentally mix another Run's event history.
  const events = [...new Map(input.filter(item => item.run_id === result.run_id).map(item => [item.sequence, item])).values()];
  const transmissions = new Map<string, TransmissionTraceDto>();
  for (const { trace } of events) if (trace.kind === "Transmission") transmissions.set(trace.payload.transmission_id, trace);
  const links = new Set<string>();
  let targetAttempts = 0;
  for (const trace of transmissions.values()) if (trace.payload.target.type === "Unicast") {
    targetAttempts += 1;
    links.add(`${trace.payload.sender_node_id}:${trace.payload.target.node_id}`);
  }
  const targetsDecoded = new Set<string>();
  const deliveredPairs = new Set<string>();
  const receiveDurationSeconds: number[] = [];
  const delay: number[] = [], loss: number[] = [], paths: number[] = [], snr: number[] = [], modeledBer: number[] = [];
  const waveformSnr: number[] = [];
  let waveformNoNoise = 0, waveformNoSignal = 0, waveformPowerMissing = 0;
  let localDelivery = 0, overheard = 0, notDecoded = 0, relay = 0;
  for (const { trace } of events) {
    if (trace.kind === "ChannelOutcome" && trace.payload.outcome.type === "Signal") {
      delay.push(Number(BigInt(trace.payload.outcome.first_arrival_delay_ns)) / 1e9);
      loss.push(trace.payload.outcome.aggregate_transmission_loss_db);
      paths.push(Number(trace.payload.outcome.path_count));
    }
    if (trace.kind !== "Reception") continue;
    const payload = trace.payload;
    const tx = transmissions.get(payload.transmission_id);
    const receivedSnr = waveformSignalToNoise(payload.waveform);
    if (typeof receivedSnr === "number") waveformSnr.push(receivedSnr);
    else if (receivedSnr === "no-noise") waveformNoNoise++;
    else if (receivedSnr === "no-signal") waveformNoSignal++;
    else waveformPowerMissing++;
    if (payload.quality) {
      snr.push(payload.quality.signal_to_noise_ratio_db);
      if (payload.quality.source === "Modeled") modeledBer.push(payload.quality.bit_error_rate);
    }
    if (payload.disposition === "LocalDelivery") {
      localDelivery += 1;
      deliveredPairs.add(`${payload.packet_id}:${payload.receiver_node_id}`);
    } else if (payload.disposition === "Overheard") overheard += 1;
    else if (payload.disposition === "NotDecoded") notDecoded += 1;
    else relay += 1;
    if (tx?.payload.target.type === "Unicast" && tx.payload.target.node_id === payload.receiver_node_id) {
      if (payload.disposition !== "NotDecoded") targetsDecoded.add(payload.transmission_id);
      const elapsed = BigInt(trace.occurred_at_ns) - BigInt(tx.payload.started_at_ns);
      if (elapsed >= 0n) receiveDurationSeconds.push(Number(elapsed) / 1e9);
    }
  }
  // The result has independent authoritative counts. A partially received SSE
  // history may be charted, but must not silently become a run-wide denominator.
  const countsMatch = BigInt(transmissions.size) === BigInt(result.projection.transmission_count)
    && BigInt(localDelivery + overheard + notDecoded + relay) === BigInt(result.projection.reception_count)
    && BigInt(delay.length) === BigInt(result.projection.channel_signal_count)
    && BigInt(localDelivery) === BigInt(result.projection.local_delivery_count)
    && BigInt(events.filter(item => item.trace.kind === "ChannelOutcome" && item.trace.payload.outcome.type === "NoArrival").length) === BigInt(result.projection.channel_no_arrival_count)
    && BigInt(events.filter(item => item.trace.kind === "CycleCommit").length) === BigInt(result.projection.cycle_count)
    && events.filter(item => item.trace.kind === "Fusion").length === result.fusion_results.length;
  const complete = streamComplete && countsMatch;
  return {
    events, transmissions, links, targetAttempts, targetsDecoded: targetsDecoded.size,
    targetDecodeRatio: complete && targetAttempts ? targetsDecoded.size / targetAttempts : null,
    deliveredPairs: deliveredPairs.size, localDelivery, overheard, notDecoded, relay,
    delay, loss, paths, snr, waveformSnr, waveformNoNoise, waveformNoSignal, waveformPowerMissing,
    modeledBer, receiveDurationSeconds, complete,
  };
}

export type BerEvidenceSource = "Modeled" | "Measured" | "External" | "Waveform" | "Unspecified";

// Compare the declared current-hop targets only. Overheard frames must not
// improve the acceptance-reference BER. This function never creates a verdict.
export function targetBerReference(result: ResultDto, stats: ReturnType<typeof resultEventStatistics>, mode: PhysicalMode) {
  const values = new Map<BerEvidenceSource, { maximum: number; count: number }>();
  const framesByReceiver = new Map<string, NonNullable<ResultDto["visualization"]>["waveform"]>();
  const receptionsByReceiver = new Map<string, RunEventDto[]>();
  for (const frame of result.visualization?.waveform ?? []) {
    const key = `${frame.transmission_id}:${frame.receiver_node_id}`;
    const bucket = framesByReceiver.get(key) ?? []; bucket.push(frame); framesByReceiver.set(key, bucket);
  }
  for (const event of stats.events) if (event.trace.kind === "Reception") {
    const key = `${event.trace.payload.transmission_id}:${event.trace.payload.receiver_node_id}`;
    const bucket = receptionsByReceiver.get(key) ?? []; bucket.push(event); receptionsByReceiver.set(key, bucket);
  }
  let missing = 0;
  for (const tx of stats.transmissions.values()) {
    const target = tx.payload.target;
    if (target.type !== "Unicast") continue;
    let source: BerEvidenceSource | undefined, value: number | undefined;
    if (mode === "B" || mode === "C") {
      const frames = framesByReceiver.get(`${tx.payload.transmission_id}:${target.node_id}`) ?? [];
      if (frames.length === 1 && BigInt(frames[0].payload_bit_count) > 0n) {
        source = "Waveform";
        value = Number(frames[0].bit_error_count) / Number(frames[0].payload_bit_count);
      }
    } else {
      const receptions = receptionsByReceiver.get(`${tx.payload.transmission_id}:${target.node_id}`) ?? [];
      if (receptions.length === 1 && receptions[0].trace.kind === "Reception" && receptions[0].trace.payload.quality) {
        source = receptions[0].trace.payload.quality.source;
        value = receptions[0].trace.payload.quality.bit_error_rate;
      }
    }
    if (!source || value === undefined || !Number.isFinite(value) || value < 0 || value > 1) { missing++; continue; }
    const previous = values.get(source);
    values.set(source, { maximum: Math.max(previous?.maximum ?? value, value), count: (previous?.count ?? 0) + 1 });
  }
  const groups = [...values].map(([source, samples]) => ({ source, maximum: samples.maximum, count: String(samples.count) }));
  // Historical reports keep the measured number even when source annotations
  // are unavailable. Do not label such a number Modeled, Measured or Waveform.
  if (!groups.length && mode !== "B" && mode !== "C" && result.acceptance_report?.maximum_ber !== null && result.acceptance_report?.maximum_ber !== undefined) {
    groups.push({ source: "Unspecified", maximum: result.acceptance_report.maximum_ber, count: result.acceptance_report.evaluated_target_receptions });
  }
  return { groups, missing, complete: stats.complete && stats.targetAttempts > 0 && missing === 0, targetAttempts: stats.targetAttempts };
}
