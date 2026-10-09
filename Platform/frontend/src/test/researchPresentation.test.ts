import { describe, expect, it } from "vitest";
import { defaultEventKinds, displayEvents, qualityAt, communicationLinks, communicationProgress, runPhysicalMode } from "../domain/runtimePresentation";
import type { RunEventDto } from "../api/types";
import { eventCurve } from "../domain/studio";
import { result } from "./fixtures";
const tx: RunEventDto = { run_id: "r", sequence: "1", trace: { kind: "Transmission", occurred_at_ns: "0", payload: { transmission_id: "1", packet_id: "3", sender_node_id: "0", target: { type: "Unicast", node_id: "2" }, started_at_ns: "0", ended_at_ns: "10" } } };
const arrival: RunEventDto = { run_id: "r", sequence: "2", trace: { kind: "SignalArrival", occurred_at_ns: "20", payload: { transmission_id: "1", receiver_node_id: "2" } } };
const reception: RunEventDto = { run_id: "r", sequence: "3", trace: { kind: "Reception", occurred_at_ns: "30", payload: { transmission_id: "1", packet_id: "3", reception_id: "1", receiver_node_id: "2", disposition: "NotDecoded", quality: null, waveform: { payload_bit_count: "100", bit_error_count: "2", crc_ok: false, signal_power_upa2: 100, noise_power_upa2: 1 } } } };
describe("research presentation invariants", () => {
  it("places both ends of communication arrows outside UUV glyphs", () => {
    const values = eventCurve({ x: 0, y: 0 }, { x: 160, y: 0 }).match(/-?\d+(?:\.\d+)?/g)!.map(Number);
    expect(Math.hypot(values[0], values[1])).toBeGreaterThan(25);
    expect(Math.hypot(values[4] - 160, values[5])).toBeGreaterThan(25);
    expect(eventCurve({ x: 0, y: 0 }, { x: 0, y: 0 })).not.toMatch(/NaN|Infinity/);
  });
  it("defaults to arrival/reception/fusion and applies one filter without mutating authoritative data", () => {
    const events = [tx, arrival, reception], frozen = JSON.stringify(events), kinds = new Set(defaultEventKinds);
    expect(displayEvents(events, kinds).map(e => e.sequence)).toEqual(["2", "3"]);
    const quality = qualityAt(events, true);
    kinds.delete("Reception");
    expect(displayEvents(events, kinds).map(e => e.sequence)).toEqual(["2"]);
    expect(qualityAt(events, true)).toEqual(quality);
    expect(JSON.stringify(events)).toBe(frozen);
    expect(communicationLinks(events, reception, displayEvents(events, kinds))).toEqual([]);
    expect(communicationLinks(events, arrival, displayEvents(events, kinds))[0]).toMatchObject({ sender: "0", receiver: "2", phase: "SignalArrival" });
  });
  it("has no future BER before reception and sums real bit errors rather than percentages", () => {
    expect(qualityAt([tx, arrival], true).ber).toBe("—");
    expect(qualityAt([tx, arrival, reception], true)).toMatchObject({ ber: "0.02", snr: "20 dB", bits: "100", errors: "2", crcFail: 1 });
    expect(qualityAt([reception], false).ber).toBe("—");
  });
  it("does not fabricate finite SNR when no noise is generated", () => {
    const r = structuredClone(reception);
    if (r.trace.kind === "Reception") r.trace.payload.waveform!.noise_power_upa2 = 0;
    expect(qualityAt([r], true).snr).toBe("未加噪声");
  });
  it("supports two senders and two different receivers without synthesizing a cooperation group", () => {
    const second = structuredClone(tx); second.sequence = "4";
    if (second.trace.kind === "Transmission") { second.trace.payload.transmission_id = "9"; second.trace.payload.sender_node_id = "5"; second.trace.payload.target = { type: "Unicast", node_id: "6" }; }
    const another = structuredClone(arrival); another.sequence = "5";
    if (another.trace.kind === "SignalArrival") { another.trace.payload.transmission_id = "9"; another.trace.payload.receiver_node_id = "6"; }
    const events = [tx, second, arrival, another];
    expect(communicationLinks(events, another, displayEvents(events, new Set(defaultEventKinds))).map(l => [l.sender, l.receiver])).toEqual([["0", "2"], ["5", "6"]]);
    expect(communicationLinks(events, another, [])).toHaveLength(0);
  });
  it("does not carry an earlier SNR or modeled BER across a missing latest observation", () => {
    const modeled = structuredClone(reception), missing = structuredClone(reception);
    if (modeled.trace.kind === "Reception") modeled.trace.payload.quality = { signal_to_noise_ratio_db: 12.345, eb_n0_db: 10, bit_error_rate: .001, source: "Modeled" };
    if (missing.trace.kind === "Reception") missing.trace.payload.waveform = null;
    expect(qualityAt([modeled, missing], false)).toMatchObject({ snr: "—", ber: "—", receptions: 2, evaluated: 1, missing: 1 });
    expect(qualityAt([reception, missing], true)).toMatchObject({ snr: "—", ber: "0.02", bits: "100", receptions: 2, evaluated: 1, missing: 1 });
  });
  it("resolves historical B from captured provenance and keeps absent mode unknown", () => {
    expect(runPhysicalMode({ ...result, provenance: { physical_layer: "Channel2Waveform" } }, undefined, [])).toBe("B");
    expect(runPhysicalMode(undefined, undefined, [reception])).toBe("B");
    expect(runPhysicalMode({ ...result, provenance: null, visualization: undefined }, undefined, [reception])).toBe("B");
    expect(runPhysicalMode(undefined, undefined, [])).toBe("unknown");
    expect(qualityAt([reception], "unknown")).toMatchObject({ ber: "—", snr: "—", evaluated: 0 });
  });
  it("joins reception progress by TransmissionId and only the observed, enabled events", () => {
    const kinds = new Set(defaultEventKinds);
    const prefix = [tx, arrival];
    expect(communicationProgress(prefix, arrival, displayEvents(prefix, kinds))?.receivers).toEqual([{ node: "2", arrivedAt: "20" }]);
    const events = [tx, arrival, reception];
    expect(communicationProgress(events, reception, displayEvents(events, kinds))?.receivers).toEqual([{ node: "2", arrivedAt: "20", completedAt: "30", outcome: "解码失败 · 不交付" }]);
    kinds.delete("Reception");
    expect(communicationProgress(events, reception, displayEvents(events, kinds))).toBeNull();
    expect(communicationProgress([arrival], arrival, [arrival])).toBeNull();
  });
  it("shows one broadcast send with multiple real receivers, not multiple invented sends", () => {
    const broadcast = structuredClone(tx);
    if (broadcast.trace.kind === "Transmission") broadcast.trace.payload.target = { type: "Broadcast" };
    const secondArrival = structuredClone(arrival); secondArrival.sequence = "4";
    if (secondArrival.trace.kind === "SignalArrival") secondArrival.trace.payload.receiver_node_id = "6";
    const events = [broadcast, arrival, secondArrival];
    const progress = communicationProgress(events, secondArrival, displayEvents(events, new Set(defaultEventKinds)));
    expect(progress?.sender).toBe("0");
    expect(progress?.target.type).toBe("Broadcast");
    expect(progress?.receivers.map(r => r.node)).toEqual(["2", "6"]);
    expect(communicationProgress([broadcast, arrival], arrival, [arrival])?.receivers).toHaveLength(1);
  });
});
