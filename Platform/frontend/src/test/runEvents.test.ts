import { describe, expect, it, vi } from "vitest";
import { RunEventProjection, type EventSourceLike } from "../api/runEvents";

class Source implements EventSourceLike {
  listener: ((event: MessageEvent<string>) => void) | null = null;
  onerror: ((event: Event) => void) | null = null;
  closed = false;
  addEventListener(_type: string, listener: (event: MessageEvent<string>) => void) { this.listener = listener; }
  close() { this.closed = true; }
  emit(sequence: string, kind = "Transmission", time = sequence) {
    this.listener?.({
      lastEventId: sequence,
      data: JSON.stringify({ run_id: "run", sequence, trace: { occurred_at_ns: time, kind, payload: {} } }),
    } as MessageEvent<string>);
  }
}

describe("RunEventSequence projection", () => {
  it("deduplicates reconnect replay and preserves integers beyond JS safe range", () => {
    const sources: Source[] = [];
    const projections = vi.fn();
    const failures = vi.fn();
    const stream = new RunEventProjection("/events", projections, failures, () => {
      const source = new Source(); sources.push(source); return source;
    }, "9007199254740993");
    stream.connect();
    sources[0].emit("9007199254740994", "Reception", "9223372036854775807");
    sources[0].emit("9007199254740994", "Reception");
    expect(projections).toHaveBeenCalledTimes(1);
    expect(projections.mock.calls[0][0]).toMatchObject({
      latestSequence: "9007199254740994",
      latestSimulationTimeNs: "9223372036854775807",
      receptionCount: "1",
    });
    stream.close();
    stream.connect();
    sources[1].emit("9007199254740994", "Reception");
    sources[1].emit("9007199254740995", "CycleCommit");
    expect(projections).toHaveBeenCalledTimes(2);
    expect(failures).not.toHaveBeenCalled();
  });

  it("surfaces sequence gaps as protocol errors", () => {
    const source = new Source();
    const failure = vi.fn();
    const stream = new RunEventProjection("/events", vi.fn(), failure, () => source);
    stream.connect();
    source.emit("2");
    expect(source.closed).toBe(true);
    expect(failure.mock.calls[0][0].kind).toBe("ProtocolFailure");
  });

  it("rejects malformed event payloads and out-of-range simulation time", () => {
    const source = new Source();
    const failure = vi.fn();
    const stream = new RunEventProjection("/events", vi.fn(), failure, () => source);
    stream.connect();
    source.listener?.({
      lastEventId: "1",
      data: JSON.stringify({
        run_id: "run",
        sequence: "1",
        trace: {
          occurred_at_ns: "9223372036854775808",
          kind: "Reception",
          payload: [],
        },
      }),
    } as MessageEvent<string>);
    expect(source.closed).toBe(true);
    expect(failure.mock.calls[0][0].kind).toBe("ProtocolFailure");
  });
});
