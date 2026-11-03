//
// _picorv32_kernel_v_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//
// Vitis RTL kernel (ap_ctrl_hs) wrapping the unmodified PicoRV32 core
// (picorv32.v).  The core's address space is a MEM_BYTES window onto the
// `mem` buffer: core address A is device address mem + (A % MEM_BYTES).
//
// ap_start releases the core from reset at PROGADDR_RESET (0).  The run
// ends when the core traps (ebreak, or an illegal or misaligned access) or
// when `max_cycles` (if non-zero) cycles have elapsed.  The bridge accepts
// no new core request once the run is ending, and the core is held in reset
// only after its last AXI transaction has completed, so every run leaves
// m_axi_gmem idle.  `cycles` and `status` report the run.
//

`timescale 1 ns / 1 ps
`default_nettype none

module picorv32_kernel #(
  parameter integer MEM_BYTES = 65536
) (
  input  wire        ap_clk,
  input  wire        ap_rst_n,
  output wire        interrupt,

  // AXI4 master: the `mem` buffer, one 32-bit beat per core access
  output wire        m_axi_gmem_awvalid,
  input  wire        m_axi_gmem_awready,
  output wire [63:0] m_axi_gmem_awaddr,
  output wire [7:0]  m_axi_gmem_awlen,
  output wire [2:0]  m_axi_gmem_awsize,
  output wire [1:0]  m_axi_gmem_awburst,
  output wire        m_axi_gmem_wvalid,
  input  wire        m_axi_gmem_wready,
  output wire [31:0] m_axi_gmem_wdata,
  output wire [3:0]  m_axi_gmem_wstrb,
  output wire        m_axi_gmem_wlast,
  input  wire        m_axi_gmem_bvalid,
  output wire        m_axi_gmem_bready,
  input  wire [1:0]  m_axi_gmem_bresp,
  output wire        m_axi_gmem_arvalid,
  input  wire        m_axi_gmem_arready,
  output wire [63:0] m_axi_gmem_araddr,
  output wire [7:0]  m_axi_gmem_arlen,
  output wire [2:0]  m_axi_gmem_arsize,
  output wire [1:0]  m_axi_gmem_arburst,
  input  wire        m_axi_gmem_rvalid,
  output wire        m_axi_gmem_rready,
  input  wire [31:0] m_axi_gmem_rdata,
  input  wire        m_axi_gmem_rlast,
  input  wire [1:0]  m_axi_gmem_rresp,

  // AXI4-Lite slave: control registers
  input  wire        s_axi_control_awvalid,
  output wire        s_axi_control_awready,
  input  wire [11:0] s_axi_control_awaddr,
  input  wire        s_axi_control_wvalid,
  output wire        s_axi_control_wready,
  input  wire [31:0] s_axi_control_wdata,
  input  wire [3:0]  s_axi_control_wstrb,
  output wire        s_axi_control_bvalid,
  input  wire        s_axi_control_bready,
  output wire [1:0]  s_axi_control_bresp,
  input  wire        s_axi_control_arvalid,
  output wire        s_axi_control_arready,
  input  wire [11:0] s_axi_control_araddr,
  output wire        s_axi_control_rvalid,
  input  wire        s_axi_control_rready,
  output wire [31:0] s_axi_control_rdata,
  output wire [1:0]  s_axi_control_rresp
);

  localparam [31:0] ADDR_MASK = MEM_BYTES - 1;

  localparam [31:0] STATUS_TRAP    = 32'd1;
  localparam [31:0] STATUS_TIMEOUT = 32'd2;

  // ---------------------------------------------------------------------------
  // control registers
  // ---------------------------------------------------------------------------
  wire        ap_start;
  reg         ap_done;
  wire [63:0] mem;
  wire [31:0] max_cycles;
  reg  [31:0] cycles;
  reg  [31:0] status;

  localparam [1:0] S_IDLE = 2'd0, S_RUN = 2'd1, S_DRAIN = 2'd2, S_DONE = 2'd3;
  reg  [1:0]  state;

  picorv32_kernel_control control (
    .clk           (ap_clk),
    .rst_n         (ap_rst_n),
    .s_axi_awvalid (s_axi_control_awvalid),
    .s_axi_awready (s_axi_control_awready),
    .s_axi_awaddr  (s_axi_control_awaddr),
    .s_axi_wvalid  (s_axi_control_wvalid),
    .s_axi_wready  (s_axi_control_wready),
    .s_axi_wdata   (s_axi_control_wdata),
    .s_axi_wstrb   (s_axi_control_wstrb),
    .s_axi_bvalid  (s_axi_control_bvalid),
    .s_axi_bready  (s_axi_control_bready),
    .s_axi_bresp   (s_axi_control_bresp),
    .s_axi_arvalid (s_axi_control_arvalid),
    .s_axi_arready (s_axi_control_arready),
    .s_axi_araddr  (s_axi_control_araddr),
    .s_axi_rvalid  (s_axi_control_rvalid),
    .s_axi_rready  (s_axi_control_rready),
    .s_axi_rdata   (s_axi_control_rdata),
    .s_axi_rresp   (s_axi_control_rresp),
    .interrupt     (interrupt),
    .ap_start      (ap_start),
    .ap_done       (ap_done),
    .ap_ready      (ap_done),
    .ap_idle       (state == S_IDLE),
    .mem           (mem),
    .max_cycles    (max_cycles),
    .cycles        (cycles),
    .status        (status)
  );

  // ---------------------------------------------------------------------------
  // PicoRV32
  // ---------------------------------------------------------------------------
  reg         core_resetn;
  wire        trap;
  wire        mem_valid;
  wire        mem_instr;
  reg         mem_ready;
  wire [31:0] mem_addr;
  wire [31:0] mem_wdata;
  wire [3:0]  mem_wstrb;
  reg  [31:0] mem_rdata;

  picorv32 #(
    .ENABLE_COUNTERS (1),
    .BARREL_SHIFTER  (1),
    .ENABLE_MUL      (1),
    .ENABLE_DIV      (1),
    .CATCH_MISALIGN  (1),
    .CATCH_ILLINSN   (1),
    .PROGADDR_RESET  (32'h0000_0000)
  ) core (
    .clk         (ap_clk),
    .resetn      (core_resetn),
    .trap        (trap),
    .mem_valid   (mem_valid),
    .mem_instr   (mem_instr),
    .mem_ready   (mem_ready),
    .mem_addr    (mem_addr),
    .mem_wdata   (mem_wdata),
    .mem_wstrb   (mem_wstrb),
    .mem_rdata   (mem_rdata),
    .mem_la_read (),
    .mem_la_write(),
    .mem_la_addr (),
    .mem_la_wdata(),
    .mem_la_wstrb(),
    .pcpi_valid  (),
    .pcpi_insn   (),
    .pcpi_rs1    (),
    .pcpi_rs2    (),
    .pcpi_wr     (1'b0),
    .pcpi_rd     (32'd0),
    .pcpi_wait   (1'b0),
    .pcpi_ready  (1'b0),
    .irq         (32'd0),
    .eoi         (),
    .trace_valid (),
    .trace_data  ()
  );

  // ---------------------------------------------------------------------------
  // memory bridge: one outstanding core access as a single-beat AXI4 burst
  // ---------------------------------------------------------------------------
  localparam [1:0] M_IDLE = 2'd0, M_READ = 2'd1, M_WRITE = 2'd2, M_RESP = 2'd3;
  reg  [1:0]  mstate;
  reg         halt;      // accept no new core request
  reg  [63:0] req_addr;
  reg  [31:0] req_wdata;
  reg  [3:0]  req_wstrb;
  reg         arvalid;
  reg         awvalid;
  reg         wvalid;

  assign m_axi_gmem_awvalid = awvalid;
  assign m_axi_gmem_awaddr  = req_addr;
  assign m_axi_gmem_awlen   = 8'd0;
  assign m_axi_gmem_awsize  = 3'b010;
  assign m_axi_gmem_awburst = 2'b01;
  assign m_axi_gmem_wvalid  = wvalid;
  assign m_axi_gmem_wdata   = req_wdata;
  assign m_axi_gmem_wstrb   = req_wstrb;
  assign m_axi_gmem_wlast   = 1'b1;
  assign m_axi_gmem_bready  = mstate == M_WRITE && !awvalid && !wvalid;
  assign m_axi_gmem_arvalid = arvalid;
  assign m_axi_gmem_araddr  = req_addr;
  assign m_axi_gmem_arlen   = 8'd0;
  assign m_axi_gmem_arsize  = 3'b010;
  assign m_axi_gmem_arburst = 2'b01;
  assign m_axi_gmem_rready  = mstate == M_READ && !arvalid;

  always @(posedge ap_clk) begin
    if (!ap_rst_n) begin
      mstate    <= M_IDLE;
      mem_ready <= 1'b0;
      arvalid   <= 1'b0;
      awvalid   <= 1'b0;
      wvalid    <= 1'b0;
    end else begin
      mem_ready <= 1'b0;
      case (mstate)
        M_IDLE: begin
          if (mem_valid && core_resetn && !halt) begin
            req_addr  <= mem + { 32'd0, mem_addr & ADDR_MASK & ~32'd3 };
            req_wdata <= mem_wdata;
            req_wstrb <= mem_wstrb;
            if (|mem_wstrb) begin
              awvalid <= 1'b1;
              wvalid  <= 1'b1;
              mstate  <= M_WRITE;
            end else begin
              arvalid <= 1'b1;
              mstate  <= M_READ;
            end
          end
        end
        M_READ: begin
          if (m_axi_gmem_arready)
            arvalid <= 1'b0;
          if (m_axi_gmem_rvalid && m_axi_gmem_rready) begin
            mem_rdata <= m_axi_gmem_rdata;
            mem_ready <= 1'b1;
            mstate    <= M_RESP;
          end
        end
        M_WRITE: begin
          if (m_axi_gmem_awready)
            awvalid <= 1'b0;
          if (m_axi_gmem_wready)
            wvalid <= 1'b0;
          if (m_axi_gmem_bvalid && m_axi_gmem_bready) begin
            mem_ready <= 1'b1;
            mstate    <= M_RESP;
          end
        end
        // the core samples mem_ready/mem_rdata and drops or replaces
        // mem_valid at the end of this cycle
        M_RESP: mstate <= M_IDLE;
        default: mstate <= M_IDLE;
      endcase
    end
  end

  // ---------------------------------------------------------------------------
  // run control
  // ---------------------------------------------------------------------------
  always @(posedge ap_clk) begin
    if (!ap_rst_n) begin
      state       <= S_IDLE;
      core_resetn <= 1'b0;
      halt        <= 1'b1;
      ap_done     <= 1'b0;
      cycles      <= 32'd0;
      status      <= 32'd0;
    end else begin
      ap_done <= 1'b0;
      case (state)
        S_IDLE: begin
          if (ap_start) begin
            cycles      <= 32'd0;
            status      <= 32'd0;
            halt        <= 1'b0;
            core_resetn <= 1'b1;
            state       <= S_RUN;
          end
        end
        S_RUN: begin
          if (trap) begin
            status <= STATUS_TRAP;
            halt   <= 1'b1;
            state  <= S_DRAIN;
          end else if (max_cycles != 32'd0 && cycles == max_cycles) begin
            status <= STATUS_TIMEOUT;
            halt   <= 1'b1;
            state  <= S_DRAIN;
          end else begin
            cycles <= cycles + 32'd1;
          end
        end
        S_DRAIN: begin
          if (mstate == M_IDLE) begin
            core_resetn <= 1'b0;
            ap_done     <= 1'b1;
            state       <= S_DONE;
          end
        end
        // ap_done/ap_ready pulse: the control block clears ap_start
        S_DONE: state <= S_IDLE;
        default: state <= S_IDLE;
      endcase
    end
  end

endmodule

`default_nettype wire

// EOF
