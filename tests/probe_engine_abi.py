#!/usr/bin/env python3
"""Compare a real engine-header virtual call under MinGW and the MSVC ABI.

This is a compatibility gate, not a networking test. A mismatch exits 1 and
keeps assembly/results in build/abi-probe for inspection.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mingw", default="x86_64-w64-mingw32-g++-posix")
    parser.add_argument("--clang", default="clang++-18")
    parser.add_argument("--output", type=Path, default=ROOT / "build/abi-probe")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    source = args.output / "engine_virtual_call.cpp"
    source.write_text(
        '#include "Games/Skyrim/Components/BaseFormComponent.h"\n'
        'extern "C" void CallEngineInit(BaseFormComponent* value) { value->Init(); }\n'
    )
    offsets = {}
    versions = {}
    for name, compiler, flags in (
        ("mingw", args.mingw, ["-fno-devirtualize", "-mms-bitfields"]),
        ("msvc_abi", args.clang, ["--target=x86_64-pc-windows-msvc"]),
    ):
        assembly = args.output / f"{name}.s"
        subprocess.run([compiler, "-O1", *flags, "-I", str(ROOT / "Code/client"),
                        "-S", str(source), "-o", str(assembly)], check=True)
        body = assembly.read_text().split("CallEngineInit:", 1)[1]
        match = re.search(r"(?:call|jmp)\w*\s+\*(\d+)\(%rax\)", body)
        if not match:
            raise RuntimeError(f"Could not identify the virtual call in {assembly}; inspect manually")
        offsets[name] = int(match[1])
        versions[name] = subprocess.check_output([compiler, "--version"], text=True).splitlines()[0]
    compatible = offsets["mingw"] == offsets["msvc_abi"]
    result = {"compatible": compatible, "virtual_call": "BaseFormComponent::Init",
              "offset_bytes": offsets, "compilers": versions}
    (args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    if not compatible:
        print("FAIL: these builds call different vtable slots on the same engine type.")
        print("Resolve engine ABI compatibility before treating network tests as game validation.")
    return 0 if compatible else 1


if __name__ == "__main__":
    raise SystemExit(main())
