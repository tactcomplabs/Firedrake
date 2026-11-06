#!/usr/bin/env python3
#
# bin2hex.py
#
# Copyright (C) 2026 Tactical Computing Laboratories, LLC
# All Rights Reserved
# contact@tactcomplabs.com
#
# See LICENSE.txt in the top level directory for licensing details
#
# Converts a raw little-endian binary to one 32-bit hex word per line, for
# $readmemh in the picorv32_kernel testbench:
#   bin2hex.py firmware.bin firmware.hex
#

import sys

with open(sys.argv[1], "rb") as f:
    data = f.read()
data += b"\0" * (-len(data) % 4)
with open(sys.argv[2], "w") as f:
    for i in range(0, len(data), 4):
        f.write(f"{int.from_bytes(data[i:i + 4], 'little'):08x}\n")

# EOF
