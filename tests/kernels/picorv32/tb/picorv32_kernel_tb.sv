//
// _picorv32_kernel_tb_sv_
//
// Copyright (C) 2026 Tactical Computing Laboratories, LLC
// All Rights Reserved
// contact@tactcomplabs.com
//
// See LICENSE.txt in the top level directory for licensing details
//
// xsim testbench for picorv32_kernel: drives s_axi_control as XRT does,
// serves m_axi_gmem from a memory model with random ready/valid delays,
// checks the AXI handshake rules and runs the test firmware:
//
//   1. a run that the watchdog stops (status = timeout, cycles = max_cycles)
//   2. RUNS runs to completion (status = trap) with different inputs; the
//      results are checked against the reference in picorv32_test.h
//
//   xsim ... -testplusarg firmware=<firmware.hex>
// Prints "result=PASS" or "result=FAIL".
//

`timescale 1 ns / 1 ps

module picorv32_kernel_tb;

  // picorv32_test.h
  localparam int unsigned PRV_PARAMS  = 'h8000;
  localparam int unsigned PRV_RESULT  = 'h8100;
  localparam int unsigned PRV_IN      = 'h9000;
  localparam int unsigned PRV_OUT     = 'hA000;
  localparam int unsigned PRV_MAGIC   = 'h7069636f;
  localparam int unsigned PRV_FIB     = 144;  // fib(12)
  localparam int unsigned MEM_WORDS   = 'h10000 / 4;

  localparam longint unsigned BASE    = 64'h0000_0040_1234_0000;  // `mem` buffer address
  localparam int unsigned N           = 48;
  localparam int unsigned RUNS        = 2;
  localparam int unsigned TIMEOUT     = 300;

  // control registers (kernel.xml)
  localparam int unsigned REG_CTRL    = 'h00;
  localparam int unsigned REG_GIE     = 'h04;
  localparam int unsigned REG_IER     = 'h08;
  localparam int unsigned REG_ISR     = 'h0c;
  localparam int unsigned REG_MEM     = 'h10;
  localparam int unsigned REG_MAX     = 'h18;
  localparam int unsigned REG_CYCLES  = 'h20;
  localparam int unsigned REG_STATUS  = 'h28;

  logic ap_clk = 0;
  logic ap_rst_n = 0;
  wire  interrupt;
  always #1.667 ap_clk = ~ap_clk;  // 300 MHz

  // m_axi_gmem
  wire        awvalid, wvalid, bready, arvalid, rready, wlast;
  logic       awready = 0, wready = 0, bvalid = 0, arready = 0, rvalid = 0;
  wire [63:0] awaddr, araddr;
  wire [7:0]  awlen, arlen;
  wire [2:0]  awsize, arsize;
  wire [1:0]  awburst, arburst;
  wire [31:0] wdata;
  wire [3:0]  wstrb;
  logic [31:0] rdata;

  // s_axi_control
  logic        c_awvalid = 0, c_wvalid = 0, c_bready = 0, c_arvalid = 0, c_rready = 0;
  logic [11:0] c_awaddr, c_araddr;
  logic [31:0] c_wdata;
  wire         c_awready, c_wready, c_bvalid, c_arready, c_rvalid;
  wire  [31:0] c_rdata;
  wire  [1:0]  c_bresp, c_rresp;

  picorv32_kernel dut (
    .ap_clk                (ap_clk),
    .ap_rst_n              (ap_rst_n),
    .interrupt             (interrupt),
    .m_axi_gmem_awvalid    (awvalid),
    .m_axi_gmem_awready    (awready),
    .m_axi_gmem_awaddr     (awaddr),
    .m_axi_gmem_awlen      (awlen),
    .m_axi_gmem_awsize     (awsize),
    .m_axi_gmem_awburst    (awburst),
    .m_axi_gmem_wvalid     (wvalid),
    .m_axi_gmem_wready     (wready),
    .m_axi_gmem_wdata      (wdata),
    .m_axi_gmem_wstrb      (wstrb),
    .m_axi_gmem_wlast      (wlast),
    .m_axi_gmem_bvalid     (bvalid),
    .m_axi_gmem_bready     (bready),
    .m_axi_gmem_bresp      (2'b00),
    .m_axi_gmem_arvalid    (arvalid),
    .m_axi_gmem_arready    (arready),
    .m_axi_gmem_araddr     (araddr),
    .m_axi_gmem_arlen      (arlen),
    .m_axi_gmem_arsize     (arsize),
    .m_axi_gmem_arburst    (arburst),
    .m_axi_gmem_rvalid     (rvalid),
    .m_axi_gmem_rready     (rready),
    .m_axi_gmem_rdata      (rdata),
    .m_axi_gmem_rlast      (1'b1),
    .m_axi_gmem_rresp      (2'b00),
    .s_axi_control_awvalid (c_awvalid),
    .s_axi_control_awready (c_awready),
    .s_axi_control_awaddr  (c_awaddr),
    .s_axi_control_wvalid  (c_wvalid),
    .s_axi_control_wready  (c_wready),
    .s_axi_control_wdata   (c_wdata),
    .s_axi_control_wstrb   (4'hf),
    .s_axi_control_bvalid  (c_bvalid),
    .s_axi_control_bready  (c_bready),
    .s_axi_control_bresp   (c_bresp),
    .s_axi_control_arvalid (c_arvalid),
    .s_axi_control_arready (c_arready),
    .s_axi_control_araddr  (c_araddr),
    .s_axi_control_rvalid  (c_rvalid),
    .s_axi_control_rready  (c_rready),
    .s_axi_control_rdata   (c_rdata),
    .s_axi_control_rresp   (c_rresp)
  );

  int errors = 0;

  task automatic fail( string what );
    $display( "FAIL: %s", what );
    errors++;
  endtask

  // ---------------------------------------------------------------------------
  // memory model: one transaction at a time, random handshake delays
  // ---------------------------------------------------------------------------
  logic [31:0] mem [MEM_WORDS];
  int unsigned accesses = 0;

  function automatic int unsigned word_index( input logic [63:0] addr );
    if( addr < BASE || addr >= BASE + MEM_WORDS * 4 || addr[1:0] != 0 ) begin
      $display( "FAIL: m_axi access outside the buffer: 0x%016h", addr );
      errors++;
      return 0;
    end
    return int'( ( addr - BASE ) >> 2 );
  endfunction

  task automatic delay();
    repeat( $urandom_range( 0, 3 ) ) @( posedge ap_clk );
  endtask

  initial begin : memory
    logic [63:0] a;
    logic [31:0] d;
    logic [3:0]  s;
    int unsigned w;
    forever begin
      @( posedge ap_clk );
      if( arvalid ) begin
        delay();
        arready <= 1;
        a = araddr;
        @( posedge ap_clk );
        arready <= 0;
        if( arlen != 0 || arsize != 3'b010 || arburst != 2'b01 )
          fail( "read burst is not a single 32-bit INCR beat" );
        delay();
        rdata  <= mem[word_index( a )];
        rvalid <= 1;
        do @( posedge ap_clk ); while( !rready );
        rvalid <= 0;
        accesses++;
      end else if( awvalid || wvalid ) begin
        // accept AW and W in either order
        delay();
        fork
          begin
            wait( awvalid );
            awready <= 1;
            @( posedge ap_clk );
            a = awaddr;
            awready <= 0;
          end
          begin
            wait( wvalid );
            delay();
            wready <= 1;
            @( posedge ap_clk );
            d = wdata;
            s = wstrb;
            if( !wlast )
              fail( "write beat without wlast" );
            wready <= 0;
          end
        join
        if( awlen != 0 || awsize != 3'b010 || awburst != 2'b01 )
          fail( "write burst is not a single 32-bit INCR beat" );
        w = word_index( a );
        for( int b = 0; b < 4; b++ )
          if( s[b] )
            mem[w][8*b +: 8] = d[8*b +: 8];
        delay();
        bvalid <= 1;
        do @( posedge ap_clk ); while( !bready );
        bvalid <= 0;
        accesses++;
      end
    end
  end

  // AXI rule: once valid is asserted it stays asserted (with stable
  // address/data) until the handshake
  logic        p_arvalid = 0, p_awvalid = 0, p_wvalid = 0;
  logic [63:0] p_araddr, p_awaddr;
  logic [31:0] p_wdata;
  always @( posedge ap_clk ) begin
    if( ap_rst_n ) begin
      if( p_arvalid && ( !arvalid || araddr != p_araddr ) )
        fail( "arvalid/araddr changed before arready" );
      if( p_awvalid && ( !awvalid || awaddr != p_awaddr ) )
        fail( "awvalid/awaddr changed before awready" );
      if( p_wvalid && ( !wvalid || wdata != p_wdata ) )
        fail( "wvalid/wdata changed before wready" );
    end
    p_arvalid <= arvalid && !arready;
    p_awvalid <= awvalid && !awready;
    p_wvalid  <= wvalid && !wready;
    p_araddr  <= araddr;
    p_awaddr  <= awaddr;
    p_wdata   <= wdata;
  end

  // ---------------------------------------------------------------------------
  // s_axi_control master
  // ---------------------------------------------------------------------------
  task automatic reg_write( input int unsigned addr, input logic [31:0] data );
    @( posedge ap_clk );
    c_awaddr  <= addr[11:0];
    c_awvalid <= 1;
    c_wdata   <= data;
    c_wvalid  <= 1;
    c_bready  <= 1;
    do @( posedge ap_clk ); while( !c_awready );
    c_awvalid <= 0;
    while( !c_wready ) @( posedge ap_clk );
    @( posedge ap_clk );
    c_wvalid <= 0;
    while( !c_bvalid ) @( posedge ap_clk );
    @( posedge ap_clk );
    c_bready <= 0;
  endtask

  task automatic reg_read( input int unsigned addr, output logic [31:0] data );
    @( posedge ap_clk );
    c_araddr  <= addr[11:0];
    c_arvalid <= 1;
    do @( posedge ap_clk ); while( !c_arready );
    c_arvalid <= 0;
    c_rready  <= 1;
    while( !c_rvalid ) @( posedge ap_clk );
    data = c_rdata;
    @( posedge ap_clk );
    c_rready <= 0;
  endtask

  // ---------------------------------------------------------------------------
  // reference (picorv32_test.h)
  // ---------------------------------------------------------------------------
  function automatic logic [31:0] prv_input( input logic [31:0] i, input logic [31:0] r );
    return i * 32'h01000193 + r * 32'h9e37 + 32'h811c9dc5;
  endfunction

  function automatic logic [31:0] prv_element( input logic [31:0] x, input logic [31:0] i );
    logic [31:0] v;
    logic [63:0] p;
    v = x * ( x + i );
    v = v + x / ( ( i & 7 ) + 1 );
    v = v - x % 13;
    p = { 32'd0, x } * 64'h9e3779b9;
    v = v ^ p[63:32];
    v = v + 32'( $signed( x ) / -32'sd7 );
    return v ^ ( i << 3 );
  endfunction

  function automatic logic [31:0] prv_checksum( input logic [31:0] c, input logic [31:0] v );
    return { c[26:0], c[31:27] } ^ v;
  endfunction

  // ---------------------------------------------------------------------------
  // test
  // ---------------------------------------------------------------------------
  logic [31:0] firmware [MEM_WORDS];

  task automatic load_image( input int unsigned run );
    for( int i = 0; i < MEM_WORDS; i++ )
      mem[i] = ( i < PRV_PARAMS / 4 ) ? firmware[i] : 32'hdeadbeef;
    mem[PRV_PARAMS / 4] = N;
    for( int i = 0; i < 6; i++ )
      mem[PRV_RESULT / 4 + i] = 0;
    for( int i = 0; i < N; i++ )
      mem[PRV_IN / 4 + i] = prv_input( i, run );
  endtask

  task automatic run_kernel( input logic [31:0] max_cycles, output logic [31:0] status, output logic [31:0] cycles );
    logic [31:0] ctrl;
    int unsigned polls = 0;
    reg_write( REG_MEM, BASE[31:0] );
    reg_write( REG_MEM + 4, BASE[63:32] );
    reg_write( REG_MAX, max_cycles );
    reg_read( REG_CTRL, ctrl );
    if( ctrl[2] !== 1'b1 )
      fail( "kernel idle before ap_start" );
    reg_write( REG_CTRL, 32'h1 );
    do begin
      repeat( 50 ) @( posedge ap_clk );
      reg_read( REG_CTRL, ctrl );
      if( ++polls > 200000 ) begin
        fail( "kernel never reported ap_done" );
        $finish;
      end
    end while( !ctrl[1] );
    if( !ctrl[2] || ctrl[0] )
      fail( "ap_idle set and ap_start clear after ap_done" );
    reg_read( REG_CTRL, ctrl );
    if( ctrl[1] )
      fail( "ap_done clears on read" );
    reg_read( REG_STATUS, status );
    reg_read( REG_CYCLES, cycles );
  endtask

  initial begin : test
    string       fw;
    logic [31:0] status, cycles, v, c, isr;
    if( !$value$plusargs( "firmware=%s", fw ) ) begin
      $display( "result=FAIL: pass -testplusarg firmware=<firmware.hex>" );
      $finish;
    end
    for( int i = 0; i < MEM_WORDS; i++ )
      firmware[i] = 0;
    $readmemh( fw, firmware );

    repeat( 10 ) @( posedge ap_clk );
    ap_rst_n <= 1;
    repeat( 5 ) @( posedge ap_clk );

    // interrupt on ap_done
    reg_write( REG_GIE, 1 );
    reg_write( REG_IER, 1 );

    // 1. watchdog
    load_image( 99 );
    run_kernel( TIMEOUT, status, cycles );
    $display( "timeout run: status=%0d cycles=%0d", status, cycles );
    if( status != 2 )
      fail( "watchdog run reports status timeout (2)" );
    if( cycles != TIMEOUT )
      fail( "watchdog run reports cycles == max_cycles" );
    if( mem[PRV_RESULT / 4] == PRV_MAGIC )
      fail( "watchdog stopped the firmware before it finished" );
    if( !interrupt )
      fail( "interrupt raised on ap_done" );
    reg_read( REG_ISR, isr );
    reg_write( REG_ISR, isr );  // toggle-on-write clears
    @( posedge ap_clk );
    if( interrupt )
      fail( "interrupt cleared through ISR" );

    // 2. complete runs
    for( int r = 0; r < RUNS; r++ ) begin
      load_image( r );
      run_kernel( 0, status, cycles );
      $display( "run %0d: status=%0d cycles=%0d firmware cycles=%0d instret=%0d accesses=%0d", r, status, cycles,
                mem[PRV_RESULT / 4 + 4], mem[PRV_RESULT / 4 + 5], accesses );
      if( status != 1 )
        fail( "run reports status trap (1)" );
      if( mem[PRV_RESULT / 4] != PRV_MAGIC )
        fail( "result magic" );
      if( mem[PRV_RESULT / 4 + 1] != N )
        fail( "result n" );
      if( mem[PRV_RESULT / 4 + 3] != PRV_FIB )
        fail( "result fib" );
      if( mem[PRV_RESULT / 4 + 4] == 0 || mem[PRV_RESULT / 4 + 4] >= cycles )
        fail( "firmware rdcycle within the kernel cycle count" );
      c = 0;
      for( int i = 0; i < N; i++ ) begin
        v = prv_element( prv_input( i, r ), i );
        c = prv_checksum( c, v );
        if( mem[PRV_OUT / 4 + i] != v ) begin
          $display( "out[%0d] = 0x%08h, expected 0x%08h", i, mem[PRV_OUT / 4 + i], v );
          fail( "out[] values" );
          break;
        end
      end
      if( mem[PRV_RESULT / 4 + 2] != c )
        fail( "result checksum" );
    end

    if( errors == 0 )
      $display( "result=PASS: picorv32_kernel rtl" );
    else
      $display( "result=FAIL: %0d errors", errors );
    $finish;
  end

endmodule

// EOF
