#
# Firedrake/tests/kernels/picorv32/package_kernel.tcl
#
# Copyright (C) 2026 Tactical Computing Laboratories, LLC
# All Rights Reserved
# contact@tactcomplabs.com
#
# See LICENSE.txt in the top level directory for licensing details
#
# Packages picorv32_kernel (picorv32.v and its wrapper) as a Vitis RTL
# kernel:
#   vivado -mode batch -source package_kernel.tcl -tclargs <out.xo> <work-dir>
# The kernel interface is described by kernel.xml (package_xo -kernel_xml),
# which is also the Firedrake metadata for the kernel.
#

if { [llength $argv] != 2 } {
  puts "usage: vivado -mode batch -source package_kernel.tcl -tclargs <out.xo> <work-dir>"
  exit 2
}
set xo_path  [file normalize [lindex $argv 0]]
set work_dir [file normalize [lindex $argv 1]]
set src_dir  [file dirname [file normalize [info script]]]
set kernel   picorv32_kernel
set ip_dir   $work_dir/ip
set part     xcu50-fsvh2104-2-e

file delete -force $ip_dir $xo_path
file mkdir $work_dir

create_project -force ${kernel}_pkg $work_dir/project -part $part
add_files -norecurse [list \
  $src_dir/picorv32.v \
  $src_dir/picorv32_kernel_control.v \
  $src_dir/picorv32_kernel.v]
set_property top $kernel [current_fileset]
update_compile_order -fileset sources_1

ipx::package_project -root_dir $ip_dir -vendor tactcomplabs.com -library firedrake -taxonomy /KernelIP \
  -import_files -set_current true
set core [ipx::current_core]
set_property name $kernel $core
set_property display_name $kernel $core
set_property version 1.0 $core
set_property description "PicoRV32 RV32IM core as a Vitis RTL kernel (Firedrake test)" $core

# the inferred interfaces: m_axi_gmem, s_axi_control, ap_clk, ap_rst_n, interrupt
foreach up [ipx::get_user_parameters -of_objects $core] {
  ipx::remove_user_parameter [get_property NAME $up] $core
}
ipx::associate_bus_interfaces -busif m_axi_gmem -clock ap_clk $core
ipx::associate_bus_interfaces -busif s_axi_control -clock ap_clk $core

# control register map (kernel.xml offsets); mem is the m_axi_gmem pointer
set mem_map    [ipx::add_memory_map -quiet s_axi_control $core]
set addr_block [ipx::add_address_block -quiet reg0 $mem_map]
set_property range 0x1000 $addr_block
set_property slave_memory_map_ref s_axi_control [ipx::get_bus_interfaces -of_objects $core s_axi_control]

proc add_reg { block name offset size desc } {
  set reg [ipx::add_register -quiet $name $block]
  set_property address_offset $offset $reg
  set_property size $size $reg
  set_property description $desc $reg
  return $reg
}
add_reg $addr_block CTRL       0x000 32 "Control signals"
add_reg $addr_block GIER       0x004 32 "Global Interrupt Enable Register"
add_reg $addr_block IP_IER     0x008 32 "IP Interrupt Enable Register"
add_reg $addr_block IP_ISR     0x00C 32 "IP Interrupt Status Register"
set reg [add_reg $addr_block mem 0x010 64 "mem buffer address"]
set_property value m_axi_gmem [ipx::add_register_parameter -quiet ASSOCIATED_BUSIF $reg]
add_reg $addr_block max_cycles 0x018 32 "Watchdog limit in cycles (0: none)"
add_reg $addr_block cycles     0x020 32 "Cycles of the last run"
add_reg $addr_block status     0x028 32 "Run status: bit 0 trap, bit 1 watchdog"

set_property sdx_kernel true $core
set_property sdx_kernel_type rtl $core
set_property ipi_drc {ignore_freq_hz true} $core
set_property supported_families {} $core
set_property auto_family_support_level level_2 $core

ipx::create_xgui_files $core
ipx::update_checksums $core
ipx::check_integrity -kernel $core
ipx::save_core $core
close_project

package_xo -force -xo_path $xo_path -kernel_name $kernel -ip_directory $ip_dir -kernel_xml $src_dir/kernel.xml
puts "packaged $xo_path"

# EOF
