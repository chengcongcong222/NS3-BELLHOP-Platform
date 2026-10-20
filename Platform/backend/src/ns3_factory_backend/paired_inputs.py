"""Exact shared source input ledgers for deterministic COMM and bearing reports."""
import asyncio
import hashlib
import json
import os
from pathlib import Path
import tempfile
from copy import deepcopy

from .catalog import CatalogError


def paired_inputs(definitions, commands):
    common = []
    for definition in definitions:
        value = deepcopy(definition)
        experiment = value["experiment"]
        if (value.get("execution_mode") != "PlanningV1" or
                experiment.get("workload") not in {"CommunicationOnly","BearingFusion"}):
            raise CatalogError("InvalidRequest", "A/B common source freeze requires supported cycle-start input.")
        for key in ("physical_layer", "waveform_modulation", "waveform_noise",
                    "waveform_noise_psd_db_re_1upa2_per_hz", "osdm"):
            experiment.pop(key, None)
        common.append(value)
    left = commands[0].execution.model_dump(mode="json")
    right = commands[1].execution.model_dump(mode="json")
    for value in (left, right):
        value.pop("physical_layer")
        value.pop("rx_quality_mode")
        for key in ("waveform_modulation", "waveform_noise", "waveform_noise_psd_db_re_1upa2_per_hz"):
            value.pop(key)
    if common[0] != common[1] or left != right:
        raise CatalogError("DraftConflict", "A/B inputs changed during freeze; neither member started.")
    if common[0]["experiment"]["workload"] == "BearingFusion":
        workload = common[0].get("bearing_inputs")
        if not workload or not workload.get("packets"):
            raise CatalogError("InvalidRequest","A/B 方位实验缺少共同观测冻结清单。")
    else:
        workload = communication_workload(common[0], commands[0])
    return {"definition": common[0], "execution": left}, workload


async def freeze_bearing_pair(worker, definitions, commands):
    """Call the production source encoder offline; zero simulation admissions."""
    ledgers = []
    for command in commands:
        process = await asyncio.create_subprocess_exec(str(worker),"--freeze-bearing-inputs",
            stdin=asyncio.subprocess.PIPE,stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE)
        try:
            stdout, stderr = await asyncio.wait_for(
                process.communicate((command.model_dump_json()+"\n").encode()),30)
        except BaseException:
            if process.returncode is None:
                process.kill()
            await process.wait()
            raise
        if process.returncode or len(stdout)>16*1024*1024:
            raise CatalogError("InvalidRequest","共同方位观测预计算失败；未创建运行。"+
                               stderr.decode(errors="replace")[-1500:])
        ledgers.append(json.loads(stdout))
    if ledgers[0] != ledgers[1]:
        raise CatalogError("DraftConflict","A/B 生成了不同的源观测；未创建运行。")
    frozen, factories = [], []
    for original, command in zip(definitions,commands,strict=True):
        definition = {**deepcopy(original),"bearing_inputs":ledgers[0]}
        encoded = json.dumps(definition,ensure_ascii=False,sort_keys=True,separators=(",",":"),allow_nan=False).encode()
        digest = hashlib.sha256(encoded).hexdigest()
        path = Path(command.authored_definition_path).with_name(digest+".json")
        if path.exists():
            if path.read_bytes() != encoded:
                raise CatalogError("ImmutableResourceConflict","Frozen bearing definition conflict.")
        else:
            fd, temporary = tempfile.mkstemp(prefix=".bearing-",dir=path.parent)
            try:
                with os.fdopen(fd,"wb") as stream:
                    stream.write(encoded); stream.flush(); os.fsync(stream.fileno())
                os.chmod(temporary,0o444)
                os.replace(temporary,path)
            finally:
                if os.path.exists(temporary):
                    os.unlink(temporary)
        captured = command.model_copy(update={"authored_definition_path":str(path),"authored_definition_sha256":digest})
        factories.append(lambda run_id, captured=captured: captured.model_copy(update={"run_id":run_id}))
        frozen.append(definition)
    return tuple(factories),tuple(frozen)


def communication_workload(definition, command):
    cycles = int(command.execution.simulation_cycle_count)
    flows = sorted(definition["experiment"]["flows"], key=lambda f: (
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
    return {
        "generator": "CommunicationTrafficApplication/COMM-cycle-start-v1",
        "packets": packets, "count": str(count)}
