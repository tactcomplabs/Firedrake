#!/usr/bin/env python3
#
# firedrake-gen.py
#
# Copyright (C) 2026 Tactical Computing Laboratories, LLC
# All Rights Reserved
# contact@tactcomplabs.com
#
# See LICENSE.txt in the top level directory for licensing details
#
# Generates a Firedrake SST SubComponent for one Vitis kernel from its
# metadata: a kernel.xml, an .xo (zip containing kernel.xml), an .xclbin
# (via xclbinutil EMBEDDED_METADATA) or a Firedrake JSON kernel model.
#
# Outputs (in --out):
#   Firedrake<Name>.h / Firedrake<Name>.cc   the SubComponent
#   <name>.json                              the normalized kernel model
#

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from string import Template

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_TEMPLATES = os.path.join(os.path.dirname(SCRIPT_DIR), "templates")

PROTOCOLS = {
    "ap_ctrl_hs": "AP_CTRL_HS",
    "ap_ctrl_chain": "AP_CTRL_CHAIN",
    "ap_ctrl_none": "AP_CTRL_NONE",
    "user_managed": "USER_MANAGED",
}

# kernel.xml addressQualifier values
AQ_SCALAR, AQ_GLOBAL, AQ_STREAM = 0, 1, 4

# host C types with a direct C++ spelling for the typed accessors
C_TYPES = {
    "char", "signed char", "unsigned char", "short", "unsigned short", "int",
    "unsigned int", "unsigned", "long", "unsigned long", "long long",
    "unsigned long long", "float", "double", "bool", "int8_t", "uint8_t",
    "int16_t", "uint16_t", "int32_t", "uint32_t", "int64_t", "uint64_t",
}

UINT_BY_SIZE = {1: "uint8_t", 2: "uint16_t", 4: "uint32_t", 8: "uint64_t"}


class GenError(Exception):
    pass


# ---------------------------------------------------------------------------
# metadata readers
# ---------------------------------------------------------------------------
def parse_int(text, default=0):
    if text is None or text == "":
        return default
    return int(text, 0)


def kernel_from_xml(root, want):
    """Builds the kernel model from a kernel.xml or xclbin metadata tree"""
    kernels = [k for k in root.iter("kernel") if k.get("name")]
    if not kernels:
        raise GenError("no <kernel> element found in the metadata")
    if want:
        kernels = [k for k in kernels if k.get("name") == want]
        if not kernels:
            raise GenError(f"kernel '{want}' not found in the metadata")
    names = sorted({k.get("name") for k in kernels})
    if len(names) > 1:
        raise GenError(f"metadata holds several kernels ({', '.join(names)}); select one with --kernel")
    k = kernels[0]

    proto = k.get("hwControlProtocol")
    if proto is None:
        print(f"firedrake-gen: warning: kernel '{k.get('name')}' has no hwControlProtocol; assuming ap_ctrl_hs",
              file=sys.stderr)
        proto = "ap_ctrl_hs"
    if proto not in PROTOCOLS:
        raise GenError(f"unsupported hwControlProtocol '{proto}'")

    args = []
    for a in k.iter("arg"):
        aq = parse_int(a.get("addressQualifier"))
        typ = (a.get("type") or "").strip()
        if aq == AQ_GLOBAL:
            kind = "BUFFER"
        elif aq == AQ_STREAM:
            kind = "STREAM"
        elif aq == AQ_SCALAR:
            kind = "OUTPUT" if typ.endswith("*") else "SCALAR"
        else:
            raise GenError(f"argument '{a.get('name')}' has unsupported addressQualifier {aq}")
        args.append({
            "name": a.get("name"),
            "index": parse_int(a.get("id")),
            "kind": kind,
            "hostType": typ,
            "size": parse_int(a.get("size")),
            "offset": parse_int(a.get("offset")),
            "port": a.get("port") or "",
        })
    # xclbin metadata may list an argument once per compute unit
    uniq = {}
    for a in args:
        uniq.setdefault(a["index"], a)
    args = [uniq[i] for i in sorted(uniq)]
    return {"name": k.get("name"), "controlProtocol": proto, "args": args}


def read_metadata(path, want):
    ext = os.path.splitext(path)[1].lower()
    if ext == ".json":
        with open(path) as f:
            model = json.load(f)
        if want and model.get("name") != want:
            raise GenError(f"{path} describes kernel '{model.get('name')}', not '{want}'")
        return model
    if ext == ".xml":
        return kernel_from_xml(ET.parse(path).getroot(), want)
    if ext == ".xo":
        with zipfile.ZipFile(path) as z:
            xmls = [n for n in z.namelist() if os.path.basename(n) == "kernel.xml"]
            if not xmls:
                raise GenError(f"{path} contains no kernel.xml")
            return kernel_from_xml(ET.fromstring(z.read(xmls[0])), want)
    if ext == ".xclbin":
        tool = shutil.which("xclbinutil")
        if not tool:
            raise GenError("xclbinutil is required to read an .xclbin (source the Vitis or XRT settings)")
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "metadata.xml")
            subprocess.run([tool, "--quiet", "--force", "--input", path,
                            "--dump-section", f"EMBEDDED_METADATA:RAW:{out}"], check=True)
            return kernel_from_xml(ET.parse(out).getroot(), want)
    raise GenError(f"unsupported metadata file '{path}' (expected .xml, .xo, .xclbin or .json)")


def validate(model):
    if not re.fullmatch(r"[A-Za-z_]\w*", model.get("name", "")):
        raise GenError(f"invalid kernel name '{model.get('name')}'")
    if model.get("controlProtocol") not in PROTOCOLS:
        raise GenError(f"invalid controlProtocol '{model.get('controlProtocol')}'")
    seen = set()
    for a in model["args"]:
        if not re.fullmatch(r"[A-Za-z_]\w*", a["name"]):
            raise GenError(f"invalid argument name '{a['name']}'")
        if a["name"] in seen or a["name"] in ("done", "timer"):
            raise GenError(f"argument name '{a['name']}' is duplicated or reserved")
        seen.add(a["name"])
        if a["kind"] not in ("SCALAR", "OUTPUT", "BUFFER", "STREAM"):
            raise GenError(f"argument '{a['name']}' has invalid kind '{a['kind']}'")


# ---------------------------------------------------------------------------
# code generation
# ---------------------------------------------------------------------------
def cxx_str(s):
    return json.dumps(s)  # a JSON string is a valid C++ string literal here


def class_suffix(name):
    return "".join(p[:1].upper() + p[1:] for p in re.split(r"_+", name) if p)


def scalar_type(arg):
    """C++ type for a scalar/output typed accessor, or None for raw bytes"""
    t = arg["hostType"].rstrip("*").strip()
    t = re.sub(r"^const\s+", "", t)
    if t in C_TYPES:
        return t
    return UINT_BY_SIZE.get(arg["size"])


def needs_header(arg):
    return "<" in arg["hostType"] or "::" in arg["hostType"]


def gen(model, overrides, csim, templates_dir):
    name = model["name"]
    cls = "Firedrake" + class_suffix(name)
    lib = "firedrake" + name.lower()
    args = model["args"]

    # kernel.xml records Vitis' view of the argument types (void* for every
    # m_axi pointer, unsigned types for some scalars); argTypes restores the
    # source types for the typed accessors and the csim declaration
    for arg_name, typ in overrides.get("argTypes", {}).items():
        match = [a for a in args if a["name"] == arg_name]
        if not match:
            raise GenError(f"overrides: argTypes names unknown argument '{arg_name}'")
        match[0]["hostType"] = typ

    buffer_sizes = overrides.get("bufferSizes", {})
    for b in buffer_sizes:
        if not any(a["name"] == b and a["kind"] == "BUFFER" for a in args):
            raise GenError(f"overrides: bufferSizes names unknown buffer '{b}'")
    cycles = str(overrides.get("kernelCycles", "1"))

    # ELI documentation
    params, ports = [], []
    for a in args:
        if a["kind"] == "SCALAR":
            params.append(f'{{ {cxx_str("arg." + a["name"])}, '
                          f'{cxx_str("Initial value of scalar " + a["name"] + " (" + a["hostType"] + ")")}, "0" }},')
        elif a["kind"] == "BUFFER":
            params.append(f'{{ {cxx_str("bufferSize." + a["name"])}, '
                          f'{cxx_str("Bytes allocated for buffer " + a["name"])}, '
                          f'{cxx_str(str(buffer_sizes.get(a["name"], 0)))} }},')
            ports.append(f'{{ {cxx_str(a["name"])}, '
                         f'{cxx_str("m_axi buffer " + a["name"] + " (" + a["hostType"] + ", " + a["port"] + ")")}, '
                         f'{{ "SST::Firedrake::DataEvent" }} }},')
    params.append(f'{{ "kernelCycles", {cxx_str("Kernel cycles per run")}, {cxx_str(cycles)} }},')
    ports.append('{ "done", "Kernel run completion", { "SST::Firedrake::DoneEvent" } },')

    # typed control accessors
    accessors = []
    for a in args:
        if a["kind"] not in ("SCALAR", "OUTPUT"):
            continue
        t = scalar_type(a)
        if t is None:
            continue  # wider than 64 bits: use setScalar()/getScalar()
        if a["kind"] == "SCALAR":
            accessors.append(f'  /// Sets s_axilite scalar `{a["name"]}` ({a["hostType"]}, offset 0x{a["offset"]:x})\n'
                             f'  void set_{a["name"]}( {t} value ) {{ setScalarAs<{t}>( {cxx_str(a["name"])}, value ); }}\n')
        accessors.append(f'  /// Returns s_axilite {"scalar" if a["kind"] == "SCALAR" else "output"} `{a["name"]}`\n'
                         f'  {t} get_{a["name"]}() {{ return getScalarAs<{t}>( {cxx_str(a["name"])} ); }}\n')

    # kernel description table
    rows = []
    for a in args:
        rows.append(f'      {{ {cxx_str(a["name"])}, {a["index"]}u, ArgKind::{a["kind"]}, {cxx_str(a["hostType"])}, '
                    f'0x{a["size"]:x}u, 0x{a["offset"]:x}u, {cxx_str(a["port"])} }},')
    sizes = "".join(f'  d.bufferSizes[{cxx_str(k)}] = {int(v)}u;\n' for k, v in buffer_sizes.items())

    # csim trampoline
    if csim:
        if any(a["kind"] == "STREAM" for a in args):
            raise GenError(f"kernel '{name}' has axis stream arguments; the csim backend cannot drive them")
        include = overrides.get("csimInclude")
        if include:
            decl = f'#include {cxx_str(include)}\n'
        else:
            bad = [a["name"] for a in args if needs_header(a)]
            if bad:
                raise GenError(f"arguments {', '.join(bad)} use HLS/C++ types; set 'csimInclude' in the overrides to "
                               f"the header declaring '{name}'")
            proto = ", ".join(f'{a["hostType"]} {a["name"]}' for a in args)
            linkage = 'extern "C" ' if overrides.get("csimLinkage", "c") == "c" else ""
            decl = f'{linkage}void {name}( {proto} );\n'
        call = []
        for a in args:
            if a["kind"] in ("BUFFER", "OUTPUT"):
                call.append(f'a.buffer<{a["hostType"]}>( {a["index"]} )')
            else:
                call.append(f'a.scalar<{a["hostType"]}>( {a["index"]} )')
        csim_decl = decl
        csim_fn = (f'static void csimInvoke( CsimArgs& a ) {{\n'
                   f'  ::{name}( {", ".join(call)} );\n'
                   f'}}\n')
        csim_ptr = "&csimInvoke"
    else:
        csim_decl = "// built without HLS sources: no csim backend\n"
        csim_fn = ""
        csim_ptr = "nullptr"

    subst = {
        "NAME": name,
        "CLASS": cls,
        "LIB": lib,
        "GUARD": f"_SST_FIREDRAKE_{name.upper()}_H_",
        "PROTOCOL": model["controlProtocol"],
        "PROTOCOL_ENUM": PROTOCOLS[model["controlProtocol"]],
        "ELI_PARAMS": "\n    ".join(params),
        "ELI_PORTS": "\n    ".join(ports),
        "ACCESSORS": "\n".join(accessors),
        "ARG_ROWS": "\n".join(rows),
        "BUFFER_SIZES": sizes,
        "KERNEL_CYCLES": cxx_str(cycles),
        "CSIM_DECL": csim_decl,
        "CSIM_FN": csim_fn,
        "CSIM_PTR": csim_ptr,
    }
    out = {}
    for tmpl, suffix in (("Subcomponent.h.in", ".h"), ("Subcomponent.cc.in", ".cc")):
        with open(os.path.join(templates_dir, tmpl)) as f:
            out[cls + suffix] = Template(f.read()).substitute(subst)
    return out


def main():
    ap = argparse.ArgumentParser(description="Generate a Firedrake SST SubComponent for a Vitis kernel")
    ap.add_argument("--metadata", required=True, help="kernel.xml, .xo, .xclbin or Firedrake .json")
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("--kernel", help="kernel to select when the metadata holds several")
    ap.add_argument("--name", help="expected kernel name (error if the metadata differs)")
    ap.add_argument("--overrides", help="JSON overrides: kernelCycles, bufferSizes, argTypes, csimInclude, csimLinkage")
    ap.add_argument("--csim", action="store_true", help="emit the csim trampoline (HLS sources are linked)")
    ap.add_argument("--templates", default=DEFAULT_TEMPLATES, help="template directory")
    opts = ap.parse_args()

    try:
        model = read_metadata(opts.metadata, opts.kernel)
        validate(model)
        if opts.name and opts.name != model["name"]:
            raise GenError(f"metadata describes kernel '{model['name']}', expected '{opts.name}'")
        overrides = {}
        if opts.overrides:
            with open(opts.overrides) as f:
                overrides = json.load(f)
        files = gen(model, overrides, opts.csim, opts.templates)
    except (GenError, OSError, ET.ParseError, zipfile.BadZipFile, subprocess.CalledProcessError,
            json.JSONDecodeError) as e:
        print(f"firedrake-gen: error: {e}", file=sys.stderr)
        return 1

    for a in model["args"]:
        if a["kind"] == "STREAM":
            print(f"firedrake-gen: warning: argument '{a['name']}' is an axis stream; it has no host port",
                  file=sys.stderr)

    os.makedirs(opts.out, exist_ok=True)
    files[model["name"] + ".json"] = json.dumps(model, indent=2) + "\n"
    for fname, text in files.items():
        path = os.path.join(opts.out, fname)
        with open(path, "w") as f:
            f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())

# EOF
