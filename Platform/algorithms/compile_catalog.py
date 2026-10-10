"""Build-time compiler for trusted planning MAC packages. No XML execution."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET

IDENTIFIER = re.compile(r"[a-z][a-z0-9_.-]{0,95}\Z")
FIELD = re.compile(r"[a-z][a-z0-9_]{0,63}\Z")
INTEGER = re.compile(r"(?:0|-?[1-9][0-9]*)\Z")
NUMBER = re.compile(r"-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?\Z")


def scalar_valid(field: dict, value: str) -> bool:
    kind = field["type"]
    if len(value) > 128:
        return False
    if kind in {"integer", "duration_ns"}:
        return bool(INTEGER.fullmatch(value)) and -(2**63) <= int(value) < 2**63 and int(field["min"]) <= int(value) <= int(field["max"])
    if kind == "number":
        return bool(NUMBER.fullmatch(value)) and math.isfinite(float(value)) and float(field["min"]) <= float(value) <= float(field["max"])
    if kind == "boolean":
        return value in {"true", "false"}
    return value in field.get("choices", [])


def load_package(directory: Path) -> dict:
    directory = directory.resolve(strict=True)
    xml = directory / "algorithm.xml"
    if xml.is_symlink():
        raise ValueError("Algorithm descriptors must not be symbolic links")
    content = xml.read_bytes()
    if len(content) > 65536 or b"<!" in content or b"\x00" in content:
        raise ValueError("XML must be bounded UTF-8 without DTD, entities or declarations")
    root = ET.fromstring(content.decode("utf-8"))
    required = {"id", "version", "interface", "label", "entry"}
    if root.tag != "algorithm" or set(root.attrib) != required:
        raise ValueError("Unsupported algorithm descriptor")
    package = dict(root.attrib)
    if not IDENTIFIER.fullmatch(package["id"]) or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", package["version"]):
        raise ValueError("Invalid stable algorithm ID or version")
    if package["interface"] not in {"planning-mac-v1", "planning-mac-v2"} or not re.fullmatch(r"[A-Za-z_]\w*(?:::[A-Za-z_]\w*)+", package["entry"]):
        raise ValueError("Unsupported interface or C++ entry point")
    v2 = package["interface"] == "planning-mac-v2"
    if v2:
        requires, outputs = root.findall("requires"), root.findall("output")
        if len(requires) != 1 or requires[0].attrib != {"knowledge": "global-simulation"}:
            raise ValueError("v2 requires explicit global-simulation knowledge")
        if len(outputs) != 1 or outputs[0].attrib != {"kind": "serial-flow-schedule-v1"} or len(outputs[0]):
            raise ValueError("v2 output must be serial-flow-schedule-v1")
        inputs = []
        supported = {"node-state-v1", "flow-demand-v1", "routing-view-v1", "link-prediction-v1", "previous-feedback-v1"}
        for item in requires[0]:
            if item.tag != "input" or set(item.attrib) != {"name", "required"} or len(item):
                raise ValueError("Invalid v2 input capability")
            if item.attrib["name"] not in supported or item.attrib["required"] not in {"true", "false"}:
                raise ValueError("Unsupported v2 capability")
            inputs.append({"name": item.attrib["name"], "required": item.attrib["required"] == "true"})
        names = [i["name"] for i in inputs]
        mandatory = {"node-state-v1", "flow-demand-v1", "routing-view-v1"}
        if len(names) != len(set(names)) or not mandatory <= {i["name"] for i in inputs if i["required"]}:
            raise ValueError("v2 requires distinct node, flow and routing capabilities")
        package.update(knowledge="global-simulation", output="serial-flow-schedule-v1",
                       inputs=sorted(inputs, key=lambda i: i["name"]))
    descriptions = root.findall("description")
    if len(descriptions) != 1 or descriptions[0].attrib or len(descriptions[0]):
        raise ValueError("One plain description is required")
    package["description"] = (descriptions[0].text or "").strip()
    if not package["label"].strip() or len(package["label"]) > 80 or not package["description"] or len(package["description"]) > 400:
        raise ValueError("Provide a short user-facing label and description")
    fields = []
    for element in root:
        if element.tag == "description":
            continue
        if v2 and element.tag in {"requires", "output"}:
            continue
        if element.tag != "parameter" or set(element.attrib) - {"id", "label", "type", "unit", "default", "min", "max", "advanced", "help"}:
            raise ValueError("Unknown descriptor field")
        field = {"unit": "", "min": "", "max": "", "help": "", "advanced": "false", **element.attrib}
        if not {"id", "label", "type", "default"} <= field.keys() or not FIELD.fullmatch(field["id"]):
            raise ValueError("Invalid parameter identity")
        if field["id"] in [f["id"] for f in fields] or field["advanced"] not in {"true", "false"}:
            raise ValueError("Duplicate parameter or invalid advanced flag")
        if field["type"] not in {"duration_ns", "integer", "number", "boolean", "enum"}:
            raise ValueError("Unsupported parameter type")
        field["choices"] = []
        for option in element:
            if option.tag != "option" or set(option.attrib) != {"value"} or len(option):
                raise ValueError("Only enum options are supported")
            field["choices"].append(option.attrib["value"])
        if len(set(field["choices"])) != len(field["choices"]) or len(field["choices"]) > 32:
            raise ValueError("Invalid enum options")
        if field["type"] == "enum":
            if not field["choices"] or field["min"] or field["max"]:
                raise ValueError("Enum needs options, not numeric bounds")
        elif field["choices"]:
            raise ValueError("Options are only valid for enum")
        if field["type"] in {"duration_ns", "integer", "number"}:
            numeric = INTEGER if field["type"] != "number" else NUMBER
            if not numeric.fullmatch(field["min"]) or not numeric.fullmatch(field["max"]) or not scalar_valid(field, field["min"]) or not scalar_valid(field, field["max"]):
                raise ValueError("Invalid numeric bounds")
            if field["type"] == "duration_ns" and (int(field["min"]) < 0 or field["unit"] != "s"):
                raise ValueError("Durations use nonnegative nanoseconds and seconds in UI")
        if not scalar_valid(field, field["default"]):
            raise ValueError("Default is outside the parameter domain")
        if any(len(str(v)) > 400 for k, v in field.items() if k != "choices"):
            raise ValueError("Parameter text too long")
        field["advanced"] = field["advanced"] == "true"
        fields.append(field)
    if len(fields) > 32:
        raise ValueError("At most 32 parameters per package")
    package["parameters"] = fields
    headers = sorted(directory.rglob("*.hpp"))
    sources = sorted((directory / "src").rglob("*.cpp"))
    if not (directory / "algorithm.hpp").is_file():
        raise ValueError("Package must contain algorithm.hpp")
    digest = hashlib.sha256(content)
    for header in headers:
        if not header.resolve().is_relative_to(directory):
            raise ValueError("Package headers must not escape the package")
        digest.update(header.relative_to(directory).as_posix().encode())
        digest.update(header.read_bytes())
    # Preserve the published header-only v1 hash. Multi-source packages also
    # bind every compiled source; resource files are not implicitly executable.
    for source in sources:
        if source.is_symlink() or not source.resolve().is_relative_to(directory):
            raise ValueError("Package sources must not escape the package")
        digest.update(source.relative_to(directory).as_posix().encode())
        digest.update(source.read_bytes())
    if v2:
        # Canonical XML and every package file, including resources/tests. No
        # machine path, mtime, or ambiguous filename/content concatenation.
        digest = hashlib.sha256(b"planning-mac-v2-package\x00")
        for path in sorted(directory.rglob("*")):
            if path.is_symlink():
                raise ValueError("v2 package links are forbidden")
            if not path.is_file():
                continue
            name = path.relative_to(directory).as_posix().encode()
            data = ET.canonicalize(content.decode(), strip_text=True).encode() if path == xml else path.read_bytes()
            for value in (name, data):
                digest.update(len(value).to_bytes(8, "little"))
                digest.update(value)
    package["package_sha256"] = digest.hexdigest()
    package["header"] = str(directory / "algorithm.hpp")
    package["sources"] = [str(source) for source in sources]
    return package


def compile_packages(directories: list[Path]) -> str:
    packages = sorted((load_package(p) for p in directories), key=lambda p: (p["id"], p["version"]))
    identities = [(p["id"], p["version"]) for p in packages]
    if len(identities) != len(set(identities)):
        raise ValueError("Duplicate algorithm ID and version")
    quote = lambda s: json.dumps(s, ensure_ascii=False)
    lines = ["#pragma once", "#include <ns3_factory/planning/sdk.hpp>"]
    lines += ["#include " + quote(p["header"]) for p in packages]
    lines += ["namespace ns3_factory::planning {"]
    def declaration(p):
        fields = []
        for f in p["parameters"]:
            values = [quote(f[k]) for k in ("id", "label", "type", "unit", "default", "min", "max", "help")]
            values += ["true" if f["advanced"] else "false", "{" + ",".join(map(quote, f["choices"])) + "}"]
            fields.append("{" + ",".join(values) + "}")
        result = "{" + ",".join(quote(p[k]) for k in ("id", "version", "package_sha256", "label", "description")) + ",{" + ",".join(fields) + "}, &" + p["entry"]
        if p["interface"] == "planning-mac-v2":
            inputs = ["{" + quote(i["name"]) + "," + ("true" if i["required"] else "false") + "}" for i in p["inputs"]]
            result += "," + quote(p["knowledge"]) + "," + quote(p["output"]) + ",{" + ",".join(inputs) + "}"
        return result + "},"
    for version, name, kind in [("planning-mac-v1", "Algorithms", "MacAlgorithm"), ("planning-mac-v2", "FlowAlgorithms", "FlowMacAlgorithm")]:
        lines += [f"inline auto Registered{name}() -> const std::vector<{kind}>& {{", f"static const std::vector<{kind}> algorithms = {{"]
        lines += [declaration(p) for p in packages if p["interface"] == version]
        finder = "FindAlgorithm" if version == "planning-mac-v1" else "FindFlowAlgorithm"
        lines += ["}; return algorithms; }", f"inline auto {finder}(const MacAlgorithmConfiguration& config) -> const {kind}* {{",
                  f"if(config.interface != {quote(version)}) return nullptr;",
                  f"for(const auto& a : Registered{name}()) if(a.id == config.id && a.version == config.version && a.package_sha256 == config.package_sha256) return &a;",
                  "return nullptr; }"]
    lines += ["}"]
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cmake-output", type=Path)
    parser.add_argument("packages", nargs="+", type=Path)
    args = parser.parse_args()
    generated = compile_packages(args.packages)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text() != generated:
        args.output.write_text(generated)
    if args.cmake_output:
        def cmake_quote(value):
            if any(char in value for char in (';', '\n', '\r', '$', '"', '\\')):
                raise ValueError("Unsupported package path for CMake")
            return '"' + value + '"'
        packages = [load_package(p) for p in args.packages]
        text = "set(PLATFORM_ALGORITHM_SOURCES\n" + "".join(
            "  " + cmake_quote(s) + "\n" for p in packages for s in p["sources"]) + ")\n"
        text += "set(PLATFORM_ALGORITHM_INCLUDES\n" + "".join(
            "  " + cmake_quote(str(p.resolve() / "include")) + "\n" for p in args.packages) + ")\n"
        if not args.cmake_output.exists() or args.cmake_output.read_text() != text:
            args.cmake_output.write_text(text)
