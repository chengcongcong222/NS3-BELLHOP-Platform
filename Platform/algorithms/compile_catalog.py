"""Build-time compiler for trusted, header-only planning MAC packages. No XML execution."""
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
    if package["interface"] != "planning-mac-v1" or not re.fullmatch(r"[A-Za-z_]\w*(?:::[A-Za-z_]\w*)+", package["entry"]):
        raise ValueError("Unsupported interface or C++ entry point")
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
    if not (directory / "algorithm.hpp").is_file():
        raise ValueError("Package must contain algorithm.hpp")
    digest = hashlib.sha256(content)
    for header in headers:
        if not header.resolve().is_relative_to(directory):
            raise ValueError("Package headers must not escape the package")
        digest.update(header.relative_to(directory).as_posix().encode())
        digest.update(header.read_bytes())
    package["package_sha256"] = digest.hexdigest()
    package["header"] = str(directory / "algorithm.hpp")
    return package


def compile_packages(directories: list[Path]) -> str:
    packages = sorted((load_package(p) for p in directories), key=lambda p: (p["id"], p["version"]))
    identities = [(p["id"], p["version"]) for p in packages]
    if len(identities) != len(set(identities)):
        raise ValueError("Duplicate algorithm ID and version")
    quote = lambda s: json.dumps(s, ensure_ascii=False)
    lines = ["#pragma once", "#include <ns3_factory/planning/mac_algorithm.hpp>"]
    lines += ["#include " + quote(p["header"]) for p in packages]
    lines += ["namespace ns3_factory::planning {", "inline auto RegisteredAlgorithms() -> const std::vector<MacAlgorithm>& {", "static const std::vector<MacAlgorithm> algorithms = {"]
    for p in packages:
        fields = []
        for f in p["parameters"]:
            values = [quote(f[k]) for k in ("id", "label", "type", "unit", "default", "min", "max", "help")]
            values += ["true" if f["advanced"] else "false", "{" + ",".join(map(quote, f["choices"])) + "}"]
            fields.append("{" + ",".join(values) + "}")
        lines.append("{" + ",".join(quote(p[k]) for k in ("id", "version", "package_sha256", "label", "description")) + ",{" + ",".join(fields) + "}, &" + p["entry"] + "},")
    lines += ["}; return algorithms; }", "inline auto FindAlgorithm(const MacAlgorithmConfiguration& config) -> const MacAlgorithm* {", "for(const auto& a : RegisteredAlgorithms()) if(a.id == config.id && a.version == config.version && a.package_sha256 == config.package_sha256) return &a;", "return nullptr; }", "}"]
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("packages", nargs="+", type=Path)
    args = parser.parse_args()
    generated = compile_packages(args.packages)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text() != generated:
        args.output.write_text(generated)
