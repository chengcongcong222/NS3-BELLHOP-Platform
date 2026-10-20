"""Exact shared-input check for the existing deterministic COMM source only."""
from copy import deepcopy

from .catalog import CatalogError


def paired_inputs(definitions, commands):
    common = []
    for definition in definitions:
        value = deepcopy(definition)
        experiment = value["experiment"]
        if (value.get("execution_mode") != "PlanningV1" or
                experiment.get("workload") != "CommunicationOnly"):
            raise CatalogError("InvalidRequest", "A/B common packet freeze requires PlanningV1 CommunicationOnly.")
        experiment.pop("physical_layer")
        common.append(value)
    left = commands[0].execution.model_dump(mode="json")
    right = commands[1].execution.model_dump(mode="json")
    for value in (left, right):
        value.pop("physical_layer")
        value.pop("rx_quality_mode")
    if common[0] != common[1] or left != right:
        raise CatalogError("DraftConflict", "A/B inputs changed during freeze; neither member started.")
    cycles = int(commands[0].execution.simulation_cycle_count)
    flows = sorted(common[0]["experiment"]["flows"], key=lambda f: (
        int(f["source_node_id"]), int(f["destination_node_id"]), int(f.get("flow_id", 0))))
    count = cycles * sum(int(f["packets_per_cycle"]) for f in flows)
    if not 0 < count <= 100_000:
        raise CatalogError("InvalidRequest", "Paired workload manifest supports 1–100000 generated packets.")
    packets = []
    # This is the exact current CommunicationTrafficApplication source contract,
    # not a random-seed surrogate. Production verification must compare sends to
    # this list; queueing/airtime may change which packets are actually transmitted.
    for cycle in range(cycles):
        for flow in flows:
            for _ in range(int(flow["packets_per_cycle"])):
                packets.append({"cycle": str(cycle), "packet_id": str(1_000_000 + len(packets)),
                    "source_node_id": str(flow["source_node_id"]),
                    "destination_node_id": str(flow["destination_node_id"]),
                    "flow_id": str(flow["flow_id"]) if "flow_id" in flow else None,
                    "payload_hex": "434f4d4d"})
    return {"definition": common[0], "execution": left}, {
        "generator": "CommunicationTrafficApplication/COMM-cycle-start-v1",
        "packets": packets, "count": str(count)}
