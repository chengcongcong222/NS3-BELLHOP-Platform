import { ApiFailure } from "./client";
import type { DecimalString, RunEventDto } from "./types";

export interface RunProjectionState {
  eventCount: DecimalString;
  latestSequence: DecimalString;
  latestSimulationTimeNs: DecimalString | null;
  transmissionCount: DecimalString;
  channelOutcomeCount: DecimalString;
  channelSignalCount: DecimalString;
  channelNoArrivalCount: DecimalString;
  receptionCount: DecimalString;
  cycleCommitCount: DecimalString;
  timeline: readonly RunEventDto[];
}

export const emptyProjection: RunProjectionState = {
  eventCount: "0",
  latestSequence: "0",
  latestSimulationTimeNs: null,
  transmissionCount: "0",
  channelOutcomeCount: "0",
  channelSignalCount: "0",
  channelNoArrivalCount: "0",
  receptionCount: "0",
  cycleCommitCount: "0",
  timeline: [],
};

type ProjectionListener = (projection: RunProjectionState) => void;
type FailureListener = (failure: ApiFailure) => void;

export interface EventSourceLike {
  addEventListener(type: string, listener: (event: MessageEvent<string>) => void): void;
  close(): void;
  onerror: ((event: Event) => void) | null;
}

export type EventSourceFactory = (url: string) => EventSourceLike;

const canonicalPositive = /^[1-9][0-9]*$/;
const canonicalInt64 = /^(0|-?[1-9][0-9]*)$/;

function isInt64(value: string): boolean {
  if (!canonicalInt64.test(value)) return false;
  const parsed = BigInt(value);
  return parsed >= -(1n << 63n) && parsed <= (1n << 63n) - 1n;
}

export class RunEventProjection {
  private source: EventSourceLike | null = null;
  private state: RunProjectionState = emptyProjection;

  constructor(
    private readonly url: string,
    private readonly onProjection: ProjectionListener,
    private readonly onFailure: FailureListener,
    private readonly createSource: EventSourceFactory = (value) => new EventSource(value),
    initialSequence: DecimalString = "0",
  ) {
    if (!/^(0|[1-9][0-9]*)$/.test(initialSequence)) {
      throw new Error("initial RunEventSequence is not canonical");
    }
    this.state = { ...emptyProjection, latestSequence: initialSequence };
  }

  connect(): void {
    if (this.source) return;
    const source = this.createSource(this.url);
    this.source = source;
    source.addEventListener("run-event", (event) => this.accept(event));
    source.onerror = () => {
      // Native EventSource owns reconnect and Last-Event-ID. A transport error
      // is visible, but the source stays open for browser-managed reconnect.
      this.onFailure(
        new ApiFailure(
          "TransportUnavailable",
          "TransportUnavailable",
          "Run event stream is reconnecting.",
        ),
      );
    };
  }

  close(): void {
    this.source?.close();
    this.source = null;
  }

  private accept(event: MessageEvent<string>): void {
    try {
      if (!canonicalPositive.test(event.lastEventId)) throw new Error("bad id");
      const sequence = BigInt(event.lastEventId);
      const latest = BigInt(this.state.latestSequence);
      if (sequence <= latest) return;
      if (sequence !== latest + 1n) throw new Error("sequence gap");
      const parsed = JSON.parse(event.data) as unknown;
      if (!isRunEvent(parsed) || parsed.sequence !== event.lastEventId) {
        throw new Error("payload mismatch");
      }
      const next = {
        ...this.state,
        eventCount: (BigInt(this.state.eventCount) + 1n).toString(),
        latestSequence: event.lastEventId,
        latestSimulationTimeNs: parsed.trace.occurred_at_ns,
        timeline: [...this.state.timeline, parsed],
      };
      if (parsed.trace.kind === "Transmission") {
        next.transmissionCount = (BigInt(next.transmissionCount) + 1n).toString();
      } else if (parsed.trace.kind === "ChannelOutcome") {
        next.channelOutcomeCount = (BigInt(next.channelOutcomeCount) + 1n).toString();
      } else if (parsed.trace.kind === "Reception") {
        next.receptionCount = (BigInt(next.receptionCount) + 1n).toString();
      } else {
        next.cycleCommitCount = (BigInt(next.cycleCommitCount) + 1n).toString();
      }
      if (parsed.trace.kind === "ChannelOutcome") {
        const outcomeField = parsed.trace.payload.outcome.type === "Signal"
          ? "channelSignalCount"
          : "channelNoArrivalCount";
        next[outcomeField] = (BigInt(next[outcomeField]) + 1n).toString();
      }
      this.state = next;
      this.onProjection(next);
    } catch {
      this.close();
      this.onFailure(
        new ApiFailure(
          "ProtocolFailure",
          "ProtocolFailure",
          "Run event sequence or payload is invalid.",
        ),
      );
    }
  }
}

function isRunEvent(value: unknown): value is RunEventDto {
  if (typeof value !== "object" || value === null) return false;
  const record = value as Record<string, unknown>;
  if (typeof record.run_id !== "string" || typeof record.sequence !== "string") {
    return false;
  }
  if (typeof record.trace !== "object" || record.trace === null) return false;
  const trace = record.trace as Record<string, unknown>;
  if (typeof trace.occurred_at_ns !== "string" || !isInt64(trace.occurred_at_ns)) {
    return false;
  }
  if (!isObject(trace.payload)) return false;
  if (trace.kind === "CycleCommit") return isCycleCommit(trace.payload);
  if (trace.kind === "Transmission") return isTransmission(trace.payload);
  if (trace.kind === "ChannelOutcome") return isChannelOutcome(trace.payload);
  if (trace.kind === "Reception") return isReception(trace.payload);
  return false;
}

const canonicalUnsigned = /^(0|[1-9][0-9]*)$/;
const isObject = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);
const isUnsigned = (value: unknown): value is string =>
  typeof value === "string" && canonicalUnsigned.test(value) && BigInt(value) <= (1n << 64n) - 1n;
const isFiniteNumber = (value: unknown): value is number =>
  typeof value === "number" && Number.isFinite(value);

function isCycleCommit(payload: Record<string, unknown>): boolean {
  return isUnsigned(payload.cycle_id) &&
    isUnsigned(payload.base_snapshot_version) &&
    isUnsigned(payload.committed_snapshot_version) &&
    typeof payload.committed_at_ns === "string" && isInt64(payload.committed_at_ns);
}

function isTransmission(payload: Record<string, unknown>): boolean {
  if (!isUnsigned(payload.transmission_id) || !isUnsigned(payload.packet_id) ||
      !isUnsigned(payload.sender_node_id) || !isObject(payload.target) ||
      typeof payload.started_at_ns !== "string" || !isInt64(payload.started_at_ns) ||
      typeof payload.ended_at_ns !== "string" || !isInt64(payload.ended_at_ns)) return false;
  return payload.target.type === "Broadcast" ||
    (payload.target.type === "Unicast" && isUnsigned(payload.target.node_id));
}

function isChannelOutcome(payload: Record<string, unknown>): boolean {
  if (!isUnsigned(payload.transmission_id) || !isUnsigned(payload.receiver_node_id) ||
      !isObject(payload.outcome)) return false;
  if (payload.outcome.type === "NoArrival") return true;
  return payload.outcome.type === "Signal" &&
    typeof payload.outcome.first_arrival_delay_ns === "string" &&
    isInt64(payload.outcome.first_arrival_delay_ns) &&
    isFiniteNumber(payload.outcome.aggregate_transmission_loss_db) &&
    isUnsigned(payload.outcome.path_count);
}

function isReception(payload: Record<string, unknown>): boolean {
  if (!isUnsigned(payload.reception_id) || !isUnsigned(payload.transmission_id) ||
      !isUnsigned(payload.packet_id) || !isUnsigned(payload.receiver_node_id) ||
      !["NotDecoded", "Overheard", "LocalDelivery", "RelayEnqueue"].includes(String(payload.disposition))) {
    return false;
  }
  if (payload.quality === null) return true;
  return isObject(payload.quality) &&
    isFiniteNumber(payload.quality.signal_to_noise_ratio_db) &&
    isFiniteNumber(payload.quality.eb_n0_db) &&
    isFiniteNumber(payload.quality.bit_error_rate) &&
    ["Modeled", "Measured", "External"].includes(String(payload.quality.source));
}
