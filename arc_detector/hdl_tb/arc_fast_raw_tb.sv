`timescale 1ns/1ps
module arc_fast_raw_tb;
    parameter MULTIRATE = 0;
    parameter SAMPLES = 8192;
    parameter INJECT_BUSY = 0;
    parameter INJECT_GAP = 0;
    parameter RESET_AT = -1;
    parameter RESET_BUSY = 0;
    reg clk = 0;
    always #2.5 clk = ~clk;
    reg rst = 1;
    reg in_valid = 0;
    reg signed [23:0] in_sample = 0;
    reg [63:0] in_index = 0;
    wire in_ready, busy, candidate_valid, candidate_latched, protocol_fault;
    wire [63:0] candidate_index, filtered_index, block_index;
    wire filtered_valid, filtered_history_valid, block_valid, qualifying;
    wire [191:0] filtered_samples;
    wire [383:0] block_energies;
    wire [23:0] active_counts;
    wire [5:0] strong_mask;
    wire [31:0] streak;
    reg [87:0] inputs [0:SAMPLES-1];
    integer k, b, cycle = 0;
    arc_fast_raw_top #(.MULTIRATE(MULTIRATE)) dut (
        .clk(clk), .rst(rst), .in_valid(in_valid), .in_sample(in_sample), .in_index(in_index),
        .in_ready(in_ready), .busy(busy), .candidate_valid(candidate_valid),
        .candidate_index(candidate_index), .candidate_latched(candidate_latched),
        .protocol_fault(protocol_fault), .filtered_valid(filtered_valid),
        .filtered_history_valid(filtered_history_valid), .filtered_index(filtered_index),
        .filtered_samples(filtered_samples), .block_valid(block_valid), .block_index(block_index),
        .strong_mask(strong_mask), .qualifying(qualifying), .streak(streak),
        .block_energies(block_energies), .active_counts(active_counts)
    );
    always @(posedge clk) begin
        cycle = cycle + 1;
        #1;
        if (!rst && filtered_valid && filtered_history_valid) begin
            $write("F %0d %0d", cycle, filtered_index);
            for (b=0; b<6; b=b+1) $write(" %0d", $signed(filtered_samples[b*32+:32]));
            $write("\n");
        end
        if (!rst && block_valid) begin
            $write("B %0d %0d %0d %0d %0d", cycle, block_index, strong_mask, qualifying, streak);
            for (b=0; b<6; b=b+1) $write(" %0d", block_energies[b*64+:64]);
            for (b=0; b<6; b=b+1) $write(" %0d", active_counts[b*4+:4]);
            $write("\n");
        end
        if (!rst && candidate_valid) $display("C %0d %0d", cycle, candidate_index);
    end
    initial begin
        $readmemh("input.hex", inputs);
        repeat (5) @(negedge clk);
        rst = 0;
        for (k=0; k<SAMPLES; k=k+1) begin
            if (k == RESET_AT) begin
                in_valid = 0;
                // The latch-reset case drains previous observations. A separate
                // RESET_BUSY case cancels an in-flight warm-up MAC explicitly.
                if (!RESET_BUSY) repeat (300) @(negedge clk);
                rst = 1;
                repeat (5) @(negedge clk);
                rst = 0;
                $display("R %0d %0d", cycle, k);
            end
            if (!in_ready && !INJECT_BUSY && !INJECT_GAP) $fatal(1, "not ready at required source cadence: %0d", k);
            in_valid = 1;
            in_sample = inputs[k][23:0];
            in_index = inputs[k][87:24];
            if (INJECT_GAP && k == 7) in_index = in_index + 1;
            @(negedge clk);
            in_valid = 0;
            if (!((INJECT_BUSY || RESET_BUSY) && k == 0)) repeat (49) @(negedge clk);
        end
        repeat (300) @(negedge clk);
        $display("DONE %0d %0d %0d", cycle, protocol_fault, candidate_latched);
        $finish;
    end
    initial begin
        #(SAMPLES * 300 + 100000);
        $fatal(1, "testbench watchdog expired");
    end
endmodule
