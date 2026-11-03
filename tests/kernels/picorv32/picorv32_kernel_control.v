//
// _picorv32_kernel_control_v_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//
// s_axi_control register file for picorv32_kernel (ap_ctrl_hs), laid out
// as in kernel.xml:
//
//   0x00  control    bit 0 ap_start (R/W/COH), bit 1 ap_done (R/COR),
//                    bit 2 ap_idle (R), bit 3 ap_ready (R/COR),
//                    bit 7 auto_restart (R/W)
//   0x04  GIE        bit 0 global interrupt enable
//   0x08  IER        bit 0 ap_done, bit 1 ap_ready
//   0x0c  ISR        bit 0 ap_done, bit 1 ap_ready (R/TOW)
//   0x10  mem        buffer address [31:0]
//   0x14  mem        buffer address [63:32]
//   0x18  max_cycles watchdog limit (0: none)
//   0x20  cycles     cycles of the last run (R)
//   0x28  status     bit 0 trapped, bit 1 watchdog expired (R)
//

`timescale 1 ns / 1 ps
`default_nettype none

module picorv32_kernel_control #(
  parameter integer ADDR_WIDTH = 12
) (
  input  wire                  clk,
  input  wire                  rst_n,

  input  wire                  s_axi_awvalid,
  output wire                  s_axi_awready,
  input  wire [ADDR_WIDTH-1:0] s_axi_awaddr,
  input  wire                  s_axi_wvalid,
  output wire                  s_axi_wready,
  input  wire [31:0]           s_axi_wdata,
  input  wire [3:0]            s_axi_wstrb,
  output wire                  s_axi_bvalid,
  input  wire                  s_axi_bready,
  output wire [1:0]            s_axi_bresp,
  input  wire                  s_axi_arvalid,
  output wire                  s_axi_arready,
  input  wire [ADDR_WIDTH-1:0] s_axi_araddr,
  output wire                  s_axi_rvalid,
  input  wire                  s_axi_rready,
  output wire [31:0]           s_axi_rdata,
  output wire [1:0]            s_axi_rresp,

  output wire                  interrupt,
  output wire                  ap_start,
  input  wire                  ap_done,   // one-cycle pulse, with ap_ready
  input  wire                  ap_ready,  // one-cycle pulse
  input  wire                  ap_idle,
  output wire [63:0]           mem,
  output wire [31:0]           max_cycles,
  input  wire [31:0]           cycles,
  input  wire [31:0]           status
);

  localparam [ADDR_WIDTH-1:0] ADDR_CTRL       = 'h00;
  localparam [ADDR_WIDTH-1:0] ADDR_GIE        = 'h04;
  localparam [ADDR_WIDTH-1:0] ADDR_IER        = 'h08;
  localparam [ADDR_WIDTH-1:0] ADDR_ISR        = 'h0c;
  localparam [ADDR_WIDTH-1:0] ADDR_MEM_LO     = 'h10;
  localparam [ADDR_WIDTH-1:0] ADDR_MEM_HI     = 'h14;
  localparam [ADDR_WIDTH-1:0] ADDR_MAX_CYCLES = 'h18;
  localparam [ADDR_WIDTH-1:0] ADDR_CYCLES     = 'h20;
  localparam [ADDR_WIDTH-1:0] ADDR_STATUS     = 'h28;

  localparam [1:0] WRIDLE = 2'd0, WRDATA = 2'd1, WRRESP = 2'd2;
  localparam [1:0] RDIDLE = 2'd0, RDDATA = 2'd1;

  reg  [1:0]            wstate;
  reg  [1:0]            rstate;
  reg  [ADDR_WIDTH-1:0] waddr;
  reg  [31:0]           rdata;

  reg                   int_ap_start;
  reg                   int_ap_done;
  reg                   int_ap_ready;
  reg                   int_auto_restart;
  reg                   int_gie;
  reg  [1:0]            int_ier;
  reg  [1:0]            int_isr;
  reg  [63:0]           int_mem;
  reg  [31:0]           int_max_cycles;

  wire                  aw_hs = s_axi_awvalid & s_axi_awready;
  wire                  w_hs  = s_axi_wvalid & s_axi_wready;
  wire                  ar_hs = s_axi_arvalid & s_axi_arready;
  wire [31:0]           wmask = { {8{s_axi_wstrb[3]}}, {8{s_axi_wstrb[2]}}, {8{s_axi_wstrb[1]}}, {8{s_axi_wstrb[0]}} };

  // ---------------------------------------------------------------------------
  // AXI4-Lite write channel
  // ---------------------------------------------------------------------------
  assign s_axi_awready = rst_n && wstate == WRIDLE;
  assign s_axi_wready  = wstate == WRDATA;
  assign s_axi_bvalid  = wstate == WRRESP;
  assign s_axi_bresp   = 2'b00;

  always @(posedge clk) begin
    if (!rst_n) begin
      wstate <= WRIDLE;
    end else begin
      case (wstate)
        WRIDLE:  if (s_axi_awvalid) wstate <= WRDATA;
        WRDATA:  if (s_axi_wvalid)  wstate <= WRRESP;
        WRRESP:  if (s_axi_bready)  wstate <= WRIDLE;
        default: wstate <= WRIDLE;
      endcase
    end
  end

  always @(posedge clk) begin
    if (aw_hs)
      waddr <= s_axi_awaddr;
  end

  // ---------------------------------------------------------------------------
  // AXI4-Lite read channel
  // ---------------------------------------------------------------------------
  assign s_axi_arready = rst_n && rstate == RDIDLE;
  assign s_axi_rvalid  = rstate == RDDATA;
  assign s_axi_rdata   = rdata;
  assign s_axi_rresp   = 2'b00;

  always @(posedge clk) begin
    if (!rst_n) begin
      rstate <= RDIDLE;
    end else begin
      case (rstate)
        RDIDLE:  if (s_axi_arvalid) rstate <= RDDATA;
        RDDATA:  if (s_axi_rready)  rstate <= RDIDLE;
        default: rstate <= RDIDLE;
      endcase
    end
  end

  always @(posedge clk) begin
    if (ar_hs) begin
      case (s_axi_araddr)
        ADDR_CTRL:       rdata <= { 24'd0, int_auto_restart, 3'd0, int_ap_ready, ap_idle, int_ap_done, int_ap_start };
        ADDR_GIE:        rdata <= { 31'd0, int_gie };
        ADDR_IER:        rdata <= { 30'd0, int_ier };
        ADDR_ISR:        rdata <= { 30'd0, int_isr };
        ADDR_MEM_LO:     rdata <= int_mem[31:0];
        ADDR_MEM_HI:     rdata <= int_mem[63:32];
        ADDR_MAX_CYCLES: rdata <= int_max_cycles;
        ADDR_CYCLES:     rdata <= cycles;
        ADDR_STATUS:     rdata <= status;
        default:         rdata <= 32'd0;
      endcase
    end
  end

  // ---------------------------------------------------------------------------
  // registers
  // ---------------------------------------------------------------------------
  assign interrupt  = int_gie & (|int_isr);
  assign ap_start   = int_ap_start;
  assign mem        = int_mem;
  assign max_cycles = int_max_cycles;

  wire ctrl_write = w_hs && waddr == ADDR_CTRL;
  wire ctrl_read  = ar_hs && s_axi_araddr == ADDR_CTRL;

  always @(posedge clk) begin
    if (!rst_n) begin
      int_ap_start     <= 1'b0;
      int_ap_done      <= 1'b0;
      int_ap_ready     <= 1'b0;
      int_auto_restart <= 1'b0;
      int_gie          <= 1'b0;
      int_ier          <= 2'b0;
      int_isr          <= 2'b0;
      int_mem          <= 64'd0;
      int_max_cycles   <= 32'd0;
    end else begin
      if (ctrl_write && s_axi_wstrb[0] && s_axi_wdata[0])
        int_ap_start <= 1'b1;
      else if (ap_ready)
        int_ap_start <= int_auto_restart;

      if (ap_done)
        int_ap_done <= 1'b1;
      else if (ctrl_read)
        int_ap_done <= 1'b0;

      if (ap_ready)
        int_ap_ready <= 1'b1;
      else if (ctrl_read)
        int_ap_ready <= 1'b0;

      if (ctrl_write && s_axi_wstrb[0])
        int_auto_restart <= s_axi_wdata[7];

      if (w_hs && waddr == ADDR_GIE && s_axi_wstrb[0])
        int_gie <= s_axi_wdata[0];

      if (w_hs && waddr == ADDR_IER && s_axi_wstrb[0])
        int_ier <= s_axi_wdata[1:0];

      // ISR: set by events, toggled by writing 1
      if (int_ier[0] && ap_done)
        int_isr[0] <= 1'b1;
      else if (w_hs && waddr == ADDR_ISR && s_axi_wstrb[0])
        int_isr[0] <= int_isr[0] ^ s_axi_wdata[0];
      if (int_ier[1] && ap_ready)
        int_isr[1] <= 1'b1;
      else if (w_hs && waddr == ADDR_ISR && s_axi_wstrb[0])
        int_isr[1] <= int_isr[1] ^ s_axi_wdata[1];

      if (w_hs && waddr == ADDR_MEM_LO)
        int_mem[31:0] <= (s_axi_wdata & wmask) | (int_mem[31:0] & ~wmask);
      if (w_hs && waddr == ADDR_MEM_HI)
        int_mem[63:32] <= (s_axi_wdata & wmask) | (int_mem[63:32] & ~wmask);
      if (w_hs && waddr == ADDR_MAX_CYCLES)
        int_max_cycles <= (s_axi_wdata & wmask) | (int_max_cycles & ~wmask);
    end
  end

endmodule

`default_nettype wire

// EOF
