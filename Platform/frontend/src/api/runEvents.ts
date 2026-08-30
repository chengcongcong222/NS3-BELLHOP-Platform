import { ApiFailure } from "./client";
import type { DecimalString, RunEventDto } from "./types";

export interface RunProjectionState {
  eventCount: DecimalString;
  latestSequence: DecimalString;
  latestSimulationTimeNs: DecimalString | null;
  transmissionCount: DecimalString;
  channelOutcomeCount: DecimalString;
  receptionCount: DecimalString;
  cycleCommitCount: DecimalString;
}

export const emptyProjection: RunProjectionState = {
  eventCount: "0",
  latestSequence: "0",
  latestSimulationTimeNs: null,
  transmissionCount: "0",
  channelOutcomeCount: "0",
  receptionCount: "0",
  cycleCommitCount: "0",
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
      };
      const field = {
        Transmission: "transmissionCount",
        ChannelOutcome: "channelOutcomeCount",
        Reception: "receptionCount",
        CycleCommit: "cycleCommitCount",
      }[parsed.trace.kind] as keyof RunProjectionState;
      next[field] = (BigInt(next[field] ?? "0") + 1n).toString();
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
  return (
    typeof trace.occurred_at_ns === "string" &&
    isInt64(trace.occurred_at_ns) &&
    ["Transmission", "ChannelOutcome", "Reception", "CycleCommit"].includes(
      String(trace.kind),
    ) &&
    typeof trace.payload === "object" &&
    trace.payload !== null &&
    !Array.isArray(trace.payload)
  );
}
