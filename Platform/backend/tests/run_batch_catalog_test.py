"""Control-plane tests only: not physical A/B or browser acceptance evidence."""
import asyncio

import pytest

from ns3_factory_backend.catalog import CatalogError, InMemoryRunCatalog
from gateway_test import _command
from helpers import failure_result, success_result


class SerialGateway:
    def __init__(self, fail_first=False):
        self.entered = [asyncio.Event(), asyncio.Event()]
        self.release = [asyncio.Event(), asyncio.Event()]
        self.calls = []
        self.active = 0
        self.peak = 0
        self.fail_first = fail_first

    async def run(self, command, callback):
        index = len(self.calls)
        self.calls.append(command.run_id)
        self.active += 1
        self.peak = max(self.peak, self.active)
        self.entered[index].set()
        try:
            await self.release[index].wait()
            return (failure_result if self.fail_first and index == 0 else success_result)(command.run_id)
        finally:
            self.active -= 1

    async def shutdown(self):
        for release in self.release:
            release.set()


def catalog(gateway):
    ids = iter(["run-a", "run-b", "run-c", "run-d"])
    return InMemoryRunCatalog(gateway, lambda: next(ids))


@pytest.mark.parametrize("fail_first", [False, True])
def test_pair_sequential_and_single_admission(fail_first):
    async def check():
        gateway = SerialGateway(fail_first)
        runs = catalog(gateway)
        pair = await runs.create_batch((_command, _command))
        assert [x.lifecycle for x in pair] == ["Created", "Created"]
        await asyncio.wait_for(gateway.entered[0].wait(), 1)
        assert not gateway.entered[1].is_set()
        with pytest.raises(CatalogError, match="active Run"):
            await runs.create(_command)
        gateway.release[0].set()
        await asyncio.wait_for(gateway.entered[1].wait(), 1)
        assert (await runs.get("run-a")).lifecycle == ("Failed" if fail_first else "Completed")
        gateway.release[1].set()
        assert (await asyncio.wait_for(runs.wait_terminal("run-b"), 1)).result_available
        assert gateway.peak == 1
        await runs.shutdown()
    asyncio.run(check())


def test_invalid_second_member_creates_nothing():
    async def check():
        gateway = SerialGateway()
        runs = catalog(gateway)
        def invalid(run_id):
            raise ValueError("B preflight failed")
        with pytest.raises(ValueError, match="preflight"):
            await runs.create_batch((_command, invalid))
        assert await runs.list_runs() == ()
        assert gateway.calls == []
        valid = await runs.create(_command)
        assert valid.catalog_sequence == 1
        gateway.release[0].set()
        await asyncio.wait_for(runs.wait_terminal(valid.run_id), 1)
        await runs.shutdown()
    asyncio.run(check())


def test_pending_cancel_never_kills_active_worker():
    async def check():
        gateway = SerialGateway()
        runs = catalog(gateway)
        await runs.create_batch((_command, _command))
        with pytest.raises(CatalogError):
            await runs.cancel_pending("run-a")
        canceled = await runs.cancel_pending("run-b")
        assert canceled.failure.code == "CanceledBeforeStart"
        assert not canceled.result_available
        gateway.release[0].set()
        await asyncio.wait_for(runs.wait_terminal("run-a"), 1)
        assert gateway.calls == ["run-a"]
        await runs.shutdown()
    asyncio.run(check())


def test_shutdown_does_not_launch_queued_member():
    async def check():
        gateway = SerialGateway()
        runs = catalog(gateway)
        await runs.create_batch((_command, _command))
        await asyncio.wait_for(gateway.entered[0].wait(), 1)
        await runs.shutdown()
        assert gateway.calls == ["run-a"]
        assert (await runs.get("run-b")).failure.code == "Interrupted"
        with pytest.raises(CatalogError, match="shutting down"):
            await runs.create(_command)
    asyncio.run(check())


def test_immediate_shutdown_prevents_even_first_spawn():
    async def check():
        gateway = SerialGateway()
        runs = catalog(gateway)
        await runs.create_batch((_command, _command))
        await runs.shutdown()
        assert gateway.calls == []
        assert all(run.failure.code == "Interrupted" for run in await runs.list_runs())
    asyncio.run(check())


def test_duplicate_ids_and_wrong_command_identity_are_atomic():
    async def check():
        gateway = SerialGateway()
        runs = InMemoryRunCatalog(gateway, lambda: "run-same")
        with pytest.raises(CatalogError):
            await runs.create_batch((_command, _command))
        assert await runs.list_runs() == ()
        with pytest.raises(CatalogError):
            await runs.create(lambda _: _command("run-wrong"))
        assert await runs.list_runs() == ()
        assert not gateway.calls
        await runs.shutdown()
    asyncio.run(check())
