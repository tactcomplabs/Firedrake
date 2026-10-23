#
# Firedrake/tests/sdl/firedrake_test.py
#
# Copyright (C) 2026 Tactical Computing Laboratories, LLC
# All Rights Reserved
# contact@tactcomplabs.com
#
# See LICENSE.txt in the top level directory for licensing details
#
# Runs one Firedrake test scenario:
#   sst firedrake_test.py -- --scenario vadd|regfile --mode direct|links
#                            [--backend csim|sw_emu|hw_emu|hw] [--xclbin f] [--emconfig dir]
#                            [--elements N] [--runs R]
#

import argparse
import sys

import sst

ap = argparse.ArgumentParser()
ap.add_argument("--scenario", default="vadd", choices=["vadd", "regfile"])
ap.add_argument("--mode", default="direct", choices=["direct", "links"])
ap.add_argument("--backend", default="csim", choices=["csim", "sw_emu", "hw_emu", "hw"])
ap.add_argument("--xclbin", default="")
ap.add_argument("--emconfig", default="")
ap.add_argument("--elements", type=int, default=256)
ap.add_argument("--runs", type=int, default=1)
ap.add_argument("--verbose", type=int, default=0)
args = ap.parse_args(sys.argv[1:])

# Timing model shared with the checks below
KERNEL_CLOCK_HZ = 250e6     # 4000 ps per cycle
HOST_BW = 12e9              # B/s
HOST_LATENCY_PS = 1000000   # 1 us
LINK_PS = 1000              # SST link latency, 1 ns
VADD_FIXED_CYCLES = 70      # tests/kernels/vadd/vadd.firedrake.json

model_params = {
    "verbose": args.verbose,
    "backend": args.backend,
    "xclbin": args.xclbin,
    "emconfigDir": args.emconfig,
    "kernelClock": "250MHz",
    "hostBW": "12GB/s",
    "hostLatency": "1us",
}

host_params = {
    "verbose": args.verbose,
    "mode": args.mode,
    "scenario": args.scenario,
    "elements": args.elements,
    "runs": args.runs,
}

if args.scenario == "regfile":
    host = sst.Component("host", "firedraketest.FiredrakeHost")
    host.addParams(host_params)
    model = host.setSubComponent("model", "firedrakeregfile.FiredrakeRegfile")
    model.addParams(model_params)
else:
    nbytes = args.elements * 4
    model_params.update({
        "bufferSize.in1": nbytes,
        "bufferSize.in2": nbytes,
        "bufferSize.out": nbytes,
    })
    cycles = VADD_FIXED_CYCLES + args.elements
    kernel_ps = round(cycles * 1e12 / KERNEL_CLOCK_HZ)
    host_params["expectKernelPs"] = kernel_ps

    host = sst.Component("host", "firedraketest.FiredrakeHost")
    if args.mode == "direct":
        model = host.setSubComponent("model", "firedrakevadd.FiredrakeVadd")
    else:
        # the two input writes share the host link back to back; the kernel
        # starts when the second completes and done takes one link hop
        xfer_ps = round(nbytes * 1e12 / HOST_BW)
        start_ps = LINK_PS + 2 * xfer_ps + HOST_LATENCY_PS
        host_params["expectDonePs"] = start_ps + kernel_ps + LINK_PS
        shell = sst.Component("kernel", "firedrake.FiredrakeComponent")
        shell.addParams({"verbose": args.verbose, "runs": args.runs, "startAfter": ["in1", "in2"]})
        model = shell.setSubComponent("model", "firedrakevadd.FiredrakeVadd")
        model_params["arg.size"] = args.elements
        sst.Link("done").connect((model, "done", "1ns"), (host, "done", "1ns"))
    model.addParams(model_params)
    host.addParams(host_params)
    for i, port in enumerate(["in1", "in2", "out"]):
        sst.Link(f"buf_{port}").connect((host, f"port{i}", "1ns"), (model, port, "1ns"))

sst.setStatisticLoadLevel(1)
sst.setStatisticOutput("sst.statOutputConsole")
sst.enableAllStatisticsForAllComponents()

# EOF
