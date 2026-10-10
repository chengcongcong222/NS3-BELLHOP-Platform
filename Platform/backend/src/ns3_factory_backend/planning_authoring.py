"""Validation and freezing of explicit planning inputs, never a MAC planner."""
from __future__ import annotations

import math
import re


def _unsigned(value: object, *, maximum: int = (1 << 64) - 1,
              positive: bool = False) -> int:
    if not isinstance(value, str) or re.fullmatch(r"0|[1-9][0-9]*", value) is None:
        raise ValueError("必须使用规范的十进制整数字符串")
    parsed = int(value)
    if parsed > maximum or (positive and parsed == 0):
        raise ValueError("整数超出允许范围")
    return parsed


def planning_issues(payload: dict, nodes: list[dict] | None = None) -> list[str]:
    """Validate editable input; node references are checked again before freeze."""
    issues: list[str] = []
    if payload.get("execution_mode") != "PlanningV1":
        return ["规划配置必须明确选择 PlanningV1"]
    flow_scoped = (payload.get("mac_algorithm") or {}).get("interface") == "planning-mac-v2" if isinstance(payload.get("mac_algorithm"), dict) else False
    if payload.get("routing_mode") not in {"Direct", "ShortestPath"}:
        issues.append("规划执行必须明确选择已实现的路由")
    if payload.get("mac_mode") not in {"AdaptiveTdma", "Tdma", "RegisteredMac"}:
        issues.append("规划执行必须明确选择已实现的 MAC")
    if flow_scoped:
        try:
            _unsigned(payload.get("network_update_interval_cycles", "1"), positive=True)
        except ValueError as error:
            issues.append(f"网络结构刷新间隔无效：{error}")
    elif "network_update_interval_cycles" in payload and payload["network_update_interval_cycles"] != "1":
        issues.append("PlanningV1 每个业务周期重新规划，network_update_interval_cycles 仅可省略或填写字符串 1")
    # These compatibility fields are still frozen and parsed by the worker for
    # both workloads. Inactive application parameters must not bypass checking
    # and later fail during int/float conversion or narrow silently in C++.
    for field, maximum, minimum in (
        ("minimum_bearing_points", (1 << 32) - 1, 5),
        ("maximum_fusion_period_ns", (1 << 63) - 1, 1),
    ):
        if field not in payload:
            continue
        try:
            if _unsigned(payload[field], maximum=maximum) < minimum:
                raise ValueError(f"不得小于 {minimum}")
        except ValueError as error:
            issues.append(f"{field} 无效：{error}")
    for field in ("target_x_m", "target_y_m", "maximum_ber"):
        if field not in payload:
            continue
        value = payload[field]
        try:
            if type(value) not in (int, float) or not math.isfinite(value):
                raise ValueError("必须是有限数值，不能是文本或布尔值")
            if field == "maximum_ber" and not 0 < value < 1:
                raise ValueError("必须严格位于 0 与 1 之间")
        except (ValueError, OverflowError) as error:
            issues.append(f"{field} 无效：{error}")
    try:
        cycle = _unsigned(payload.get("planning_cycle_duration_ns"),
                          maximum=(1 << 63) - 1, positive=True)
        count = _unsigned(payload.get("simulation_cycle_count"), positive=True)
        if cycle * count > (1 << 63) - 1:
            raise ValueError("总运行时长超出纳秒范围")
    except ValueError as error:
        issues.append(f"规划周期无效：{error}")
    if payload.get("mac_mode") != "RegisteredMac" and "mac_algorithm" in payload:
        issues.append("非注册算法不能携带 mac_algorithm 参数")
    if payload.get("mac_mode") == "RegisteredMac":
        if any(key in payload for key in ("slot_duration_ns", "guard_interval_ns", "adaptive_tdma")):
            issues.append("算法参数请只在对应算法中设置，不能混用旧时隙参数")
        selection = payload.get("mac_algorithm")
        if not isinstance(selection, dict) or set(selection) != {"id", "version", "package_sha256", "interface", "parameters"}:
            issues.append("请选择已安装算法并填写对应参数")
    elif payload.get("mac_mode") == "AdaptiveTdma":
        if "slot_duration_ns" in payload or "guard_interval_ns" in payload:
            issues.append("AdaptiveTdma 不接受手工时隙或保护时间，请移除对应字段")
        adaptive = payload.get("adaptive_tdma")
        try:
            if not isinstance(adaptive, dict) or set(adaptive) != {"safety_margin_ns"}:
                raise ValueError("必须且只能包含 safety_margin_ns")
            _unsigned(adaptive["safety_margin_ns"], maximum=(1 << 63) - 1)
        except ValueError as error:
            issues.append(f"自适应 TDMA 参数无效：{error}")
    elif payload.get("mac_mode") == "Tdma":
        if payload.get("adaptive_tdma") is not None:
            issues.append("配置式 TDMA 不接受 adaptive_tdma 参数")
        try:
            _unsigned(payload.get("slot_duration_ns"), maximum=(1 << 63) - 1, positive=True)
            _unsigned(payload.get("guard_interval_ns"), maximum=(1 << 63) - 1)
        except ValueError as error:
            issues.append(f"配置式 TDMA 时隙参数无效：{error}")

    by_id: dict[int, dict] | None = None
    if nodes is not None:
        by_id = {}
        try:
            if not isinstance(nodes, list) or not nodes:
                raise ValueError("场景至少需要一个节点")
            for node in nodes:
                if not isinstance(node, dict):
                    raise ValueError("节点必须是对象")
                node_id = _unsigned(node.get("node_id"))
                if node_id in by_id:
                    raise ValueError("节点编号重复")
                if type(node.get("can_transmit")) is not bool or type(node.get("can_receive")) is not bool:
                    raise ValueError("节点收发能力必须是布尔值")
                by_id[node_id] = node
        except ValueError as error:
            issues.append(f"场景节点无效：{error}")
            return issues

    workload = payload.get("workload", "BearingFusion")
    if workload == "CommunicationOnly":
        if payload.get("fusion_receiver_node_id") is not None:
            issues.append("普通通信不使用融合接收点，请明确配置 flows")
        flows = payload.get("flows")
        if not isinstance(flows, list) or not flows:
            issues.append("普通通信必须配置至少一条显式业务流")
        else:
            seen: set[tuple[int, int]] = set()
            identities: set[int] = set()
            for index, flow in enumerate(flows):
                try:
                    expected = {"source_node_id", "destination_node_id", "packets_per_cycle"}
                    if flow_scoped:
                        expected.add("flow_id")
                    if not isinstance(flow, dict) or set(flow) != expected:
                        raise ValueError("业务流必须包含源、目的和每周期包数")
                    if flow_scoped:
                        identity = _unsigned(flow["flow_id"])
                        if identity in identities:
                            raise ValueError("业务流标识重复")
                        identities.add(identity)
                    source = _unsigned(flow["source_node_id"])
                    destination = _unsigned(flow["destination_node_id"])
                    count = flow["packets_per_cycle"]
                    if type(count) is not int or not 1 <= count <= 1000:
                        raise ValueError("每周期包数必须是 1 至 1000 的整数")
                    if source == destination:
                        raise ValueError("源节点和目的节点不能相同")
                    if not flow_scoped and (source, destination) in seen:
                        raise ValueError("同一方向业务流不能重复")
                    seen.add((source, destination))
                    if by_id is not None:
                        if source not in by_id or destination not in by_id:
                            raise ValueError("业务流引用了不存在的节点")
                        if not by_id[source]["can_transmit"] or not by_id[destination]["can_receive"]:
                            raise ValueError("业务流端点不具备对应收发能力")
                except ValueError as error:
                    issues.append(f"业务流 {index + 1} 无效：{error}")
    elif workload == "BearingFusion":
        if payload.get("flows") not in (None, []):
            issues.append("方位融合使用应用报告来源，不接受普通通信 flows")
        try:
            receiver = _unsigned(payload.get("fusion_receiver_node_id"))
            if by_id is not None:
                if receiver not in by_id or not by_id[receiver]["can_receive"]:
                    raise ValueError("融合接收点必须存在且具备接收能力")
                if not any(node_id != receiver and node["can_transmit"] for node_id, node in by_id.items()):
                    raise ValueError("方位融合至少需要一个可发送报告的其他节点")
        except ValueError as error:
            issues.append(f"融合接收点无效：{error}")
    return issues


def freeze_planning_configuration(payload: dict) -> dict:
    """Convert validated HTTP decimal strings to the authored JSON integer form."""
    return {
        "planning_cycle_duration_ns": int(payload["planning_cycle_duration_ns"]),
        "adaptive_tdma": ({"safety_margin_ns": int(payload["adaptive_tdma"]["safety_margin_ns"])}
                          if payload["mac_mode"] == "AdaptiveTdma" else None),
        "flows": sorted([{
            **({"flow_id": int(flow["flow_id"])} if "flow_id" in flow else {}),
            "source_node_id": int(flow["source_node_id"]),
            "destination_node_id": int(flow["destination_node_id"]),
            "packets_per_cycle": flow["packets_per_cycle"],
        } for flow in payload.get("flows") or []], key=lambda flow: (
            flow["source_node_id"], flow["destination_node_id"], flow.get("flow_id", 0))),
        "fusion_receiver_node_id": (int(payload["fusion_receiver_node_id"])
                                    if payload.get("fusion_receiver_node_id") is not None else None),
    }


V3_INTEGER_FIELDS = frozenset({"version", "scenario_source_revision", "experiment_source_revision",
    "format_version", "node_id", "source_node_id", "destination_node_id", "flow_id", "packets_per_cycle",
    "fusion_center_node_id", "fusion_receiver_node_id", "time_ns", "planning_cycle_duration_ns",
    "bit_rate_bps", "network_update_interval_cycles", "minimum_bearing_points", "maximum_fusion_period_ns",
    "deterministic_seed", "slot_duration_ns", "guard_interval_ns", "safety_margin_ns"})


def v3_integer_boundary(value, *, encode: bool):
    """Adapter only. XML strings/semver are untouched; never convert floats or bools."""
    if isinstance(value, list):
        return [v3_integer_boundary(item, encode=encode) for item in value]
    if not isinstance(value, dict):
        return value
    result = {}
    for key, item in value.items():
        if key == "mac_algorithm":
            result[key] = item
        elif key in V3_INTEGER_FIELDS and item is not None:
            if encode:
                if type(item) is not int or not 0 <= item < (1 << 64):
                    raise ValueError(f"Invalid integer in frozen definition: {key}")
                result[key] = str(item)
            else:
                result[key] = _unsigned(item)
        else:
            result[key] = v3_integer_boundary(item, encode=encode)
    return result
