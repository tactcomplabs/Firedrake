#!/usr/bin/env bash
#
# Firedrake/tests/kernels/picorv32/tb/run_xsim.sh
#
# Copyright (C) 2026 Tactical Computing Laboratories, LLC
# All Rights Reserved
# contact@tactcomplabs.com
#
# See LICENSE.txt in the top level directory for licensing details
#
# Simulates picorv32_kernel with xsim (Vivado) and the test firmware:
#   run_xsim.sh <firmware.hex> <work-dir>
# Prints result=PASS or result=FAIL (from the testbench).
#

set -e
hex="$(realpath "$1")"
work="$2"
src="$(cd "$(dirname "$0")/.." && pwd)"
if [ -z "$hex" ] || [ -z "$work" ]; then
  echo "usage: $0 <firmware.hex> <work-dir>"
  exit 2
fi
for tool in xvlog xelab xsim; do
  if ! command -v $tool > /dev/null 2>&1; then
    echo "result=FAIL: $tool not found; source the Vivado settings"
    exit 1
  fi
done

mkdir -p "$work"
cd "$work"
xvlog --nolog "$src/picorv32.v" "$src/picorv32_kernel_control.v" "$src/picorv32_kernel.v"
xvlog --nolog -sv "$src/tb/picorv32_kernel_tb.sv"
xelab --nolog -debug off -snapshot picorv32_kernel_tb work.picorv32_kernel_tb
xsim --nolog picorv32_kernel_tb -R -testplusarg "firmware=$hex"

# EOF
