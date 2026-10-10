#!/usr/bin/env python3
"""Trusted local algorithm package tooling; no Python simulation algorithms."""
from __future__ import annotations

import argparse
import fcntl
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

PLATFORM = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PLATFORM / "algorithms"))
sys.path.insert(0, str(PLATFORM / "backend/src/ns3_factory_backend"))
from compile_catalog import load_package  # noqa: E402
from algorithm_installation import digest, read_release  # noqa: E402

NAME = re.compile(r"[a-z][a-z0-9_]{0,63}\Z")


def atomic_json(path: Path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode="w", dir=path.parent, encoding="utf-8", delete=False) as stream:
        temporary = Path(stream.name)
        try:
            json.dump(value, stream, ensure_ascii=False, sort_keys=True, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    os.replace(temporary, path)


def run(arguments, *, timeout=180, log: Path | None = None):
    if log:
        with log.open("a") as stream:
            stream.write("\n" + " ".join(map(str, arguments)) + "\n")
            stream.flush()
            result = subprocess.run(list(map(str, arguments)), stdout=stream, stderr=subprocess.STDOUT,
                                    timeout=timeout, check=False)
        if result.returncode:
            raise ValueError(f"检查或构建失败，当前启用版本未改变。详细日志：{log}")
        return ""
    result = subprocess.run(list(map(str, arguments)), capture_output=True, text=True,
                            timeout=timeout, check=False)
    if result.returncode:
        raise ValueError("命令失败：" + " ".join(map(str, arguments)) + "\n" + result.stderr[-6000:])
    if len(result.stdout) > 4 * 1024 * 1024:
        raise ValueError("命令输出超过部署保护预算。")
    return result.stdout


def config(args):
    data = json.loads(args.config.read_text())
    if data.get("schema") != "platform.protocolctl.v1":
        raise ValueError("请先 configure 配置本机工具链。")
    return data


def package_path(name: str):
    if not NAME.fullmatch(name):
        raise ValueError("包目录名只允许小写字母、数字与下划线。")
    directory = PLATFORM / "algorithms/user" / name
    if directory.is_symlink():
        raise ValueError("包目录不能是符号链接。")
    return directory


def inspect_package(directory: Path):
    files = list(directory.rglob("*"))
    if any(p.is_symlink() for p in files):
        raise ValueError("包中不能包含符号链接或包外依赖。")
    if any(p.name in {"CMakeLists.txt", "Makefile"} for p in files):
        raise ValueError("算法包不执行自定义构建脚本；请将源码放入 src/。")
    if sum(p.stat().st_size for p in files if p.is_file()) > 16 * 1024 * 1024:
        raise ValueError("算法包超过 16 MiB 部署保护预算。")
    if not (directory / "README.md").is_file() or not list((directory / "tests").glob("*.cpp")):
        raise ValueError("包须包含 README.md 和至少一个 tests/*.cpp 行为用例。")
    return load_package(directory)


def check(directory: Path, settings: dict, output: Path):
    metadata = inspect_package(directory)
    output.mkdir(parents=True, exist_ok=True)
    flags = [settings["cxx"], "-std=c++23", "-Wall", "-Wextra", "-Werror=return-type",
             "-I" + str(PLATFORM / "contracts/include"), "-I" + str(PLATFORM / "planning/include"),
             "-I" + str(directory), "-I" + str(directory / "include")]
    entry = output / "entry.cpp"
    entry_type = "BuildFlowMac" if metadata["interface"] == "planning-mac-v2" else "BuildMac"
    entry.write_text('#include <type_traits>\n#include "algorithm.hpp"\n'
                     'static_assert(std::is_same_v<decltype(&' + metadata["entry"]
                     + '), ns3_factory::planning::' + entry_type + '>);\nint main() {}\n')
    sources = metadata["sources"]
    log = output / "check.log"
    run(flags + [str(entry)] + sources + ["-o", str(output / "entry")], log=log)
    for index, case in enumerate(sorted((directory / "tests").glob("*.cpp"))):
        executable = output / f"test-{index}"
        run(flags + [str(case)] + sources + ["-o", str(executable)], log=log)
        run([executable], timeout=20, log=log)
    return metadata


def configure(args):
    build = args.build_root.resolve()
    install = args.install_root.resolve()
    if (build == install or build.is_relative_to(install) or install.is_relative_to(build)
            or build.is_relative_to(PLATFORM) or install.is_relative_to(PLATFORM)):
        raise ValueError("构建与安装根须分开，且位于 Platform 源码目录之外。")
    if not args.ns3_prefix.is_dir():
        raise ValueError("ns-3 安装前缀不存在。")
    cxx = shutil.which(args.cxx)
    if cxx is None:
        raise ValueError("找不到 C++ 编译器。")
    atomic_json(args.config, {"schema": "platform.protocolctl.v1", "build_root": str(build),
        "install_root": str(install), "ns3_prefix": str(args.ns3_prefix.resolve()), "cxx": cxx,
        "jobs": args.jobs})
    print("工具链已配置；仅使用本机依赖，不下载库。")


def new_package(args):
    directory = package_path(args.name)
    if directory.exists():
        raise ValueError("包目录已存在，未覆盖。")
    namespace = "user_" + args.name
    if args.interface == "planning-mac-v2":
        source = PLATFORM / "algorithms/examples/channel_aware"
        shutil.copytree(source, directory)
        for path in directory.rglob("*"):
            if path.is_file() and path.suffix in {".hpp", ".cpp", ".xml", ".md"}:
                path.write_text(path.read_text().replace("example_channel_aware", namespace)
                    .replace("example.channel-aware", "user." + args.name))
        print("已创建 v2 包：" + str(directory))
        return
    directory.mkdir(parents=True)
    source = (PLATFORM / "algorithms/examples/ordered_mac/algorithm.hpp").read_text()
    (directory / "algorithm.hpp").write_text(source.replace("example_mac", namespace))
    xml = (PLATFORM / "algorithms/examples/ordered_mac/algorithm.xml").read_text()
    xml = xml.replace("example.ordered-mac", "user." + args.name).replace("example_mac", namespace)
    (directory / "algorithm.xml").write_text(xml)
    (directory / "tests").mkdir()
    fixture = (PLATFORM / "tools/templates/planning_v1_test.cpp.in").read_text()
    (directory / "tests/determinism.cpp").write_text(fixture.replace("@NAMESPACE@", namespace))
    (directory / "README.md").write_text("# " + args.name + "\n\n"
        "基于平台排序教学样例；不是科研协议性能结论。当前使用 planning-mac-v1。\n"
        "修改 algorithm.hpp 算法主体，XML 仅开放实验参数。节点按真实 NodeId 绑定。\n"
        "不得 include runtime/internal、操作 ns-3、修改正式队列或使用墙钟随机数。\n"
        "新增实现可放 src/*.cpp，辅助头放 include/*.hpp；不执行包自带构建脚本。\n"
        "运行 protocolctl check 后 install；参数变更必须有行为测试。\n")
    print("已创建：" + str(directory))


def dependency_manifest(worker: Path):
    output = run(["ldd", worker])
    if "not found" in output:
        raise ValueError("候选程序缺少动态依赖。")
    paths = set()
    for line in output.splitlines():
        match = re.search(r"(?:=>\s*)?(/[^\s]+)", line)
        if match:
            paths.add(Path(match.group(1)).resolve(strict=True))
    return [{"path": str(p), "sha256": digest(p)} for p in sorted(paths)]


def install_package(args):
    settings = config(args)
    root = Path(settings["install_root"])
    root.mkdir(parents=True, exist_ok=True)
    # Serialize publication. Activation has a separate service-held lease.
    with (root / "install.lock").open("a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        build_root = Path(settings["build_root"])
        build_root.mkdir(parents=True, exist_ok=True)
        candidate = Path(tempfile.mkdtemp(prefix="candidate-", dir=build_root))
        package = package_path(args.name)
        inspect_package(package)
        # Existing packages are copied from the pinned active installation,
        # never re-read from modified developer sources during a new install.
        package_dirs = []
        active = root / "active.json"
        if active.exists():
            _, previous = read_release(root, json.loads(active.read_text())["release"])
            for item in previous["packages"]:
                path = root / "releases" / previous["worker_sha256"] / "packages" / item["directory"]
                installed = inspect_package(path)
                if any(installed[k] != item[k] for k in ("id", "version", "interface", "package_sha256")):
                    raise ValueError("旧安装保留的算法源码已变化，停止构建。")
                package_dirs.append(path)
        current = inspect_package(package)
        for manifest_path in (root / "releases").glob("*/manifest.json"):
            previous = json.loads(manifest_path.read_text())
            for item in previous["packages"]:
                if (item["id"], item["version"]) == (current["id"], current["version"]) and item["package_sha256"] != current["package_sha256"]:
                    raise ValueError("同 ID/版本已有不同源码，请升级版本；原安装未修改。")
        package_dirs = [p for p in package_dirs if (load_package(p)["id"], load_package(p)["version"]) != (current["id"], current["version"])]
        package_dirs.append(package)
        copies = []
        packages = []
        for index, original in enumerate(package_dirs):
            copied = candidate / "packages" / f"package-{index}"
            shutil.copytree(original, copied)
            metadata = check(copied, settings, candidate / f"check-{index}")
            packages.append({k: metadata[k] for k in ("id", "version", "interface", "package_sha256")})
            packages[-1]["directory"] = copied.name
            copies.append(copied)
        build = candidate / "build"
        log = candidate / "build.log"
        run(["cmake", "-S", PLATFORM, "-B", build, "-G", "Ninja", "-DBUILD_TESTING=OFF",
             "-DPLATFORM_ENABLE_NS3=ON", "-DCMAKE_CXX_COMPILER=" + settings["cxx"],
             "-DCMAKE_PREFIX_PATH=" + settings["ns3_prefix"],
             "-DPLATFORM_MAC_PLUGIN_DIRS=" + ";".join(map(str, copies))], log=log)
        run(["cmake", "--build", build, "--target", "platform_sim_worker", "-j", str(settings["jobs"])],
            timeout=1800, log=log)
        worker = build / "worker/platform_sim_worker"
        catalog = json.loads(run([worker, "--algorithm-catalog"]))
        for package in packages:
            if not any(all(item.get(k) == package[k] for k in ("id", "version", "interface", "package_sha256")) for item in catalog["algorithms"]):
                raise ValueError("真实程序目录缺少请求算法，拒绝安装。")
        identity = digest(worker)
        source = run(["git", "-C", PLATFORM, "rev-parse", "HEAD"]).strip()
        manifest = {"schema": "platform.algorithm-installation.v1", "worker_sha256": identity,
            "source_revision": source, "source_dirty": bool(run(["git", "-C", PLATFORM, "status", "--porcelain"]).strip()),
            "toolchain": run([settings["cxx"], "--version"]).splitlines()[0],
            "dependencies": dependency_manifest(worker), "packages": packages, "catalog": catalog}
        releases = root / "releases"
        releases.mkdir(exist_ok=True)
        release = releases / identity
        if not release.exists():
            staging = Path(tempfile.mkdtemp(prefix=".publish-", dir=releases))
            shutil.copy2(worker, staging / "platform_sim_worker")
            shutil.copytree(candidate / "packages", staging / "packages")
            atomic_json(staging / "manifest.json", manifest)
            os.rename(staging, release)
        read_release(root, identity)
        atomic_json(root / "pending.json", {"release": identity})
        print("InstalledPendingActivation " + identity)
        print("构建日志：" + str(log))
    if args.activate:
        activate_release(args, identity)


def activate_release(args, identity=None):
    root = Path(config(args)["install_root"])
    identity = identity or args.release or json.loads((root / "pending.json").read_text())["release"]
    with (root / "activation.lock").open("a+") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print("InstalledPendingActivation：受管服务仍持有旧版本；不终止运行。待运行结束并正常关闭服务后再 activate。")
            return
        read_release(root, identity)
        atomic_json(root / "active.json", {"release": identity})
        print("Active " + identity)
        print("新服务设置 PLATFORM_ALGORITHM_INSTALL_ROOT=" + str(root) + "；未修改或重启现有服务。")


def list_packages(args):
    settings = config(args)
    root = Path(settings["install_root"])
    active = json.loads((root / "active.json").read_text())["release"] if (root / "active.json").exists() else None
    for directory in sorted((PLATFORM / "algorithms/user").glob("*")):
        try:
            metadata = inspect_package(directory)
            print("Discovered", metadata["id"], metadata["version"], metadata["interface"], metadata["package_sha256"])
        except (OSError, ValueError, SyntaxError) as error:
            print("Invalid", directory.name, str(error))
    for manifest_path in sorted((root / "releases").glob("*/manifest.json")):
        try:
            worker, manifest = read_release(root, manifest_path.parent.name)
            # Display executable evidence, not merely an XML/manifest claim.
            catalog = json.loads(run([worker, "--algorithm-catalog"]))
            state = "Active" if manifest_path.parent.name == active else "InstalledPendingActivation"
            for item in catalog["algorithms"]:
                print(state, item["id"], item["version"], item["interface"], item["package_sha256"], manifest_path.parent.name)
        except (ValueError, OSError) as error:
            print("Invalid", manifest_path.parent.name, str(error))


def replay(args):
    settings = config(args)
    root = Path(settings["install_root"])
    release = args.release or json.loads((root / "active.json").read_text())["release"]
    worker, _ = read_release(root, release)
    snapshot = args.snapshot.resolve(strict=True)
    if snapshot.stat().st_size > 8 * 1024 * 1024:
        raise ValueError("单周期上下文超过 8 MiB 部署保护预算。")
    print(run([worker, "--replay-planning-context", snapshot]).strip())


def main():
    parser = argparse.ArgumentParser(description="受信任的服务器端算法安装工具；不执行 Python 仿真。")
    parser.add_argument("--config", type=Path, default=PLATFORM / ".runtime/protocolctl.json")
    commands = parser.add_subparsers(dest="command", required=True)
    setup = commands.add_parser("configure", help="一次性配置本机离线工具链")
    setup.add_argument("--build-root", type=Path, required=True)
    setup.add_argument("--install-root", type=Path, required=True)
    setup.add_argument("--ns3-prefix", type=Path, required=True)
    setup.add_argument("--cxx", default="c++")
    setup.add_argument("--jobs", type=int, choices=range(1, 9), default=2)
    for name in ("new", "check", "install"):
        command = commands.add_parser(name)
        command.add_argument("name")
        if name == "new":
            command.add_argument("--interface", choices=["planning-mac-v1", "planning-mac-v2"], default="planning-mac-v2")
        if name == "install":
            command.add_argument("--activate", action="store_true")
    commands.add_parser("list")
    activation = commands.add_parser("activate")
    activation.add_argument("--release", help="指定已保留的程序 SHA；默认待启用版本")
    replay_command = commands.add_parser("replay", help="使用相同 C++ 算法重放导出的规划上下文，不推进仿真")
    replay_command.add_argument("snapshot", type=Path)
    replay_command.add_argument("--release", help="指定已保留的程序 SHA；默认当前启用版本")
    args = parser.parse_args()
    try:
        if args.command == "configure":
            configure(args)
        elif args.command == "new":
            new_package(args)
        elif args.command == "check":
            settings = config(args)
            root = Path(settings["build_root"])
            root.mkdir(parents=True, exist_ok=True)
            output = Path(tempfile.mkdtemp(prefix="check-", dir=root))
            metadata = check(package_path(args.name), settings, output)
            print("Built", metadata["id"], metadata["version"], "检查日志：", output)
        elif args.command == "install":
            install_package(args)
        elif args.command == "activate":
            activate_release(args)
        elif args.command == "replay":
            replay(args)
        else:
            list_packages(args)
        return 0
    except (OSError, ValueError, KeyError, SyntaxError, subprocess.SubprocessError) as error:
        print("操作未完成：" + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
