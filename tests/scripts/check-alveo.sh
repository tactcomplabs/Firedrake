#!/usr/bin/env bash
#
# Firedrake/tests/scripts/check-alveo.sh
#
# Copyright (C) 2026 Tactical Computing Laboratories, LLC
# All Rights Reserved
# contact@tactcomplabs.com
#
# See LICENSE.txt in the top level directory for licensing details
#
# Checks that XRT can see an Alveo card running the given platform shell:
#   check-alveo.sh <platform> [card-match]
# card-match (default u50) is matched case-insensitively against the
# device listing when the platform name itself is not listed.
#

platform="$1"
card="${2:-u50}"
if [ -z "$platform" ]; then
  echo "usage: $0 <platform> [card-match]"
  exit 2
fi

if [ -z "$XILINX_XRT" ]; then
  echo "FAIL: XILINX_XRT is not set; source /opt/xilinx/xrt/setup.sh"
  exit 1
fi

if command -v xrt-smi > /dev/null 2>&1; then
  tool=xrt-smi
elif command -v xbutil > /dev/null 2>&1; then
  tool=xbutil
else
  echo "FAIL: neither xrt-smi nor xbutil found in PATH"
  exit 1
fi

# xrt-smi lists the base shell (xilinx_u50_gen3x16_xdma_base_5), not the
# platform (xilinx_u50_gen3x16_xdma_5_202210_1)
shell=$(echo "$platform" | sed -E 's/_([0-9]+)_[0-9]{6}_[0-9]+$/_base_\1/')

listing=$("$tool" examine 2>&1)
echo "$listing"

if echo "$listing" | grep -qE "(^|[^[:alnum:]_])($platform|$shell)([^[:alnum:]_]|$)"; then
  echo "PASS: found a device running $shell ($platform)"
  exit 0
fi
if echo "$listing" | grep -qi "$card"; then
  echo "FAIL: found a $card device, but it is not running $shell ($platform)"
  echo "      flash the card to the $platform shell (xbmgmt / xrt-smi program) or set FIREDRAKE_U50_PLATFORM"
  exit 1
fi
echo "FAIL: XRT lists no $card device; check the driver and that the card is flashed"
exit 1

# EOF
