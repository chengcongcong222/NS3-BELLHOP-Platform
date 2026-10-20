import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { expect, test } from "vitest";
import type { RecordedProxyResolved } from "../api/types";
import { ProxyResultEvidence, type ProxyReceptionEvidence } from "../features/ProxyResultEvidence";

const source: RecordedProxyResolved = {
  nodes: [{node_id:"0",can_transmit:true,can_receive:false},{node_id:"1",can_transmit:true,can_receive:false},
    {node_id:"2",can_transmit:false,can_receive:true}],
  resolved_links: [], rx_noise: [{rx_id:"2",mode:"None"}],
  assumptions: {real_joint_multitransmitter_measured:false,geometry_drives_channel:false,link_temporal_relation:"SyntheticOffsets"},
};
const rows: ProxyReceptionEvidence[] = [
  {transmission_id:"1000",sender_node_id:"0",receiver_node_id:"2",at_ns:"1000000000",
    disposition:"NotDecoded",mixture:{signal_count:"2",sample_count:"3000",mixture_power_relative:1,noise_power_relative:0},waveform:null},
  {transmission_id:"1001",sender_node_id:"1",receiver_node_id:"2",at_ns:"2000000000",
    disposition:"LocalDelivery",mixture:null,waveform:{payload_bit_count:"32",bit_error_count:"0",crc_ok:true}},
];

test("proxy result filters real sender/receiver and does not invent BER for undecoded mixtures", async () => {
  render(<ProxyResultEvidence source={source} receptions={rows} />);
  expect(screen.getByText(/2 次接收，1 次产生比特判决/)).toBeTruthy();
  await userEvent.selectOptions(screen.getByLabelText("筛选发送节点"),"0");
  expect(screen.getByText(/1 次接收，0 次产生比特判决/).textContent).toContain("实际 BER：不可用");
  expect(screen.queryByText("业务交付")).toBeNull();
  expect(screen.getByText("未产生判决")).toBeTruthy();
  await userEvent.selectOptions(screen.getByLabelText("筛选发送节点"),"1");
  expect(screen.getByText("业务交付")).toBeTruthy();
  expect(screen.queryByText("未产生判决")).toBeNull();
});

test("missing archived reception evidence is explicitly unavailable", () => {
  render(<ProxyResultEvidence source={source} />);
  expect(screen.getByText(/未保存逐链路接收记录/).textContent).toContain("实际 BER：不可用");
  expect(screen.getByText(/不代表实测多发一收海试/)).toBeTruthy();
});

test("receiver and sender filters compose into an exact directed link", async () => {
  render(<ProxyResultEvidence source={{...source,nodes:[...source.nodes,
    {node_id:"3",can_transmit:false,can_receive:true}]}}
    receptions={[...rows,{...rows[1],transmission_id:"1002",receiver_node_id:"3"}]} />);
  await userEvent.selectOptions(screen.getByLabelText("筛选接收节点"),"3");
  expect(screen.getByText(/1 次接收，1 次产生比特判决/)).toBeTruthy();
  await userEvent.selectOptions(screen.getByLabelText("筛选发送节点"),"0");
  expect(screen.getByText(/0 次接收，0 次产生比特判决/).textContent).toContain("实际 BER：不可用");
  expect(screen.queryByText("业务交付")).toBeNull();
});
