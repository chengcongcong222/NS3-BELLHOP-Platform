"""Restore only archived commands; never consult mutable authoring drafts."""
from pathlib import Path
from pydantic import ValidationError

from .catalog import CatalogError, RunSnapshot
from .run_groups import canonical, digest
from .wire import StartRunCommand


def restore_member(member, root: Path):
    definition = member["definition"]
    checksum = member["definition_sha256"]
    if digest(definition) != checksum or not member.get("command"):
        raise CatalogError("RunDefinitionUnavailable", "归档缺少可验证的原始执行命令，不能按原配置重跑。")
    path = root / f"{checksum}.json"
    encoded = canonical(definition)
    try:
        if path.read_bytes() != encoded:
            raise CatalogError("RunDefinitionUnavailable", "冻结定义校验失败，未启动新的运行。")
    except OSError as error:
        raise CatalogError("RunDefinitionUnavailable", "原冻结定义文件不可读取，未启动新的运行。") from error
    try:
        command = StartRunCommand.model_validate({
            **member["command"], "run_id": member["run_id"],
            "authored_definition_path": str(path),
        })
    except (ValidationError, TypeError) as error:
        raise CatalogError("RunDefinitionUnavailable", "归档执行命令无效，未启动新的运行。") from error
    if (command.authored_definition_sha256 != checksum
            or command.execution.model_dump(mode="json") != member["execution"]):
        raise CatalogError("RunDefinitionUnavailable", "归档命令与冻结配置不一致。")
    snapshot = RunSnapshot(
        catalog_sequence=0, run_id=command.run_id,
        experiment_id=command.preset.experiment_id,
        experiment_version=command.preset.definition_version,
        scenario_id=command.preset.scenario_id,
        scenario_version=command.preset.definition_version,
        environment_asset_id=command.environment.asset_id,
        environment_format_version=command.environment.asset_format_version,
        lifecycle="Completed" if member["state"] == "Completed" else "Failed",
        event_stream_complete=False, failure=None, terminal_run=None,
        result_available=member["result"] is not None,
        authored_definition_path=str(path), authored_definition_sha256=checksum,
    )
    return command, snapshot
