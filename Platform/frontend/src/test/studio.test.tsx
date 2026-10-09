import { describe, expect, it } from "vitest";
import { screen } from "@testing-library/react";
import { eventCurve, positionAt, replayDelay, seekTime, studioEvents } from "../domain/studio";
import { acceptance4Result as result } from "./acceptance4Golden";
import { installApi, renderRoute } from "./render";
import type { RunEventDto } from "../api/types";

describe("R4 real-evidence studio", () => {
  it("interpolates only between formal trajectory samples and never extrapolates", () => {
    const points = [{ node_id: "0", at_ns: "0", x_meters: 1, y_meters: 2, z_meters: -3 }, { node_id: "0", at_ns: "20", x_meters: 9, y_meters: 4, z_meters: -3 }];
    expect(positionAt(points, "0", "20")?.x_meters).toBe(9);
    expect(positionAt(points, "0", "10")?.x_meters).toBe(5);
    expect(positionAt(points, "1", "20")).toBeNull();
  });
  it("adapts the deterministic canvas curve without legacy DTOs", () => {
    expect(eventCurve({ x: 0, y: 0 }, { x: 100, y: 0 })).toBe("M 28.5 9.1 Q 50.0 16.0 71.5 9.1");
  });
  it("keeps channel query separate from physical arrival and preserves trace identity", () => {
    const event: RunEventDto = { run_id: "r", sequence: "9007199254740993", trace: { kind: "ChannelOutcome", occurred_at_ns: "1000000000", payload: { transmission_id: "0", receiver_node_id: "1", outcome: { type: "Signal", first_arrival_delay_ns: "10000000", aggregate_transmission_loss_db: 30, path_count: "2" } } } };
    const rows = studioEvents([event]);
    expect(rows).toHaveLength(1);
    expect(rows[0].title).toContain("信道查询");
    expect(rows[0].kind).toBe("ChannelOutcome");
    expect(rows[0].event).toBe(event);
    expect(rows[0].at).toBe("1000000000");
  });
  it("renders the formal Tx to arrival to decode to fusion sequence", () => {
    const events: RunEventDto[] = [
      { run_id: "r", sequence: "1", trace: { kind: "Transmission", occurred_at_ns: "100", payload: { transmission_id: "7", packet_id: "9", sender_node_id: "0", target: { type: "Unicast", node_id: "2" }, started_at_ns: "100", ended_at_ns: "120" } } },
      { run_id: "r", sequence: "2", trace: { kind: "ChannelOutcome", occurred_at_ns: "100", payload: { transmission_id: "7", receiver_node_id: "2", outcome: { type: "Signal", first_arrival_delay_ns: "50", aggregate_transmission_loss_db: 31, path_count: "2" } } } },
      { run_id: "r", sequence: "3", trace: { kind: "SignalArrival", occurred_at_ns: "150", payload: { transmission_id: "7", receiver_node_id: "2" } } },
      { run_id: "r", sequence: "4", trace: { kind: "Reception", occurred_at_ns: "170", payload: { reception_id: "4", transmission_id: "7", packet_id: "9", receiver_node_id: "2", disposition: "LocalDelivery", quality: null } } },
      { run_id: "r", sequence: "5", trace: { kind: "Fusion", occurred_at_ns: "200", payload: { fusion_sequence: "1", fusion_center_node_id: "2", started_at_ns: "100", completed_at_ns: "200", observation_count: "6", estimated_target_x_meters: 10, estimated_target_y_meters: 20 } } },
    ];
    const rows = studioEvents(events);
    expect(rows.map(row => row.kind)).toEqual(["Transmission", "ChannelOutcome", "SignalArrival", "Reception", "Fusion"]);
    expect(rows[2].title).toContain("实际到达");
    expect(BigInt(rows[2].at)).toBeGreaterThan(BigInt(rows[0].at));
    expect(rows[4].title).toContain("6 个有效报告");
  });
  it("seeks by simulation time and scales adjacent recorded intervals", () => {
    const rows = [0, 1, 100].map(n => ({ key: String(n), kind: "Transmission", title: "t", at: String(BigInt(n) * 1000000000n) }));
    expect(seekTime(rows, 90)).toBe(2);
    expect(replayDelay("0", "1000000000", 2).milliseconds).toBe(500);
    expect(replayDelay("0", "100000000000", 1).compressed).toBe(true);
  });
  it("shows projected waveform and fusion source evidence without inventing bearings", async () => {
    const f = result.fusion_results[0];
    installApi({ [`/runs/${result.run_id}/results`]: { body: { ...result, visualization: { positions_complete: true, generated_feature_count: "8", positions: [], fusion_sources: [{ fusion_sequence: f.fusion_sequence, node_id: "0", observation_sequence: "3" }], waveform: [{ transmission_id: "99", receiver_node_id: "0", payload_bit_count: "120", bit_error_count: "6", crc_ok: false }] } } } });
    renderRoute(`/results/${result.run_id}`);
    expect((await screen.findAllByText("6 / 120")).length).toBeGreaterThan(0);
    expect(screen.getAllByText("0.05").length).toBeGreaterThan(0);
    expect(screen.getByText("失败")).toBeTruthy();
    expect(screen.getByText(/N0 \/ #3/)).toBeTruthy();
    expect(screen.queryByText("Pass")).toBeNull();
    expect(screen.getByText(/逐条方位值.*不从发送端报告补造/)).toBeTruthy();
  });
});
