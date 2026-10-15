// Pin-count research wrapper retaining the complete candidate computation.
// Internal indices begin at zero. This wrapper cannot detect external ADC gaps.
module arc_fast_synth_top #(
    parameter integer MULTIRATE = 0
) (
    input wire clk,
    input wire rst,
    input wire in_valid,
    input wire signed [23:0] in_sample,
    output wire in_ready,
    output wire candidate_valid,
    output wire candidate_latched,
    output wire [63:0] candidate_index,
    output wire protocol_fault
);
    reg [63:0] source_index;
    always @(posedge clk) begin
        if (rst) source_index <= 0;
        else if (in_valid && in_ready) source_index <= source_index + 1;
    end
    arc_fast_raw_top #(.MULTIRATE(MULTIRATE)) core (
        .clk(clk), .rst(rst), .in_valid(in_valid), .in_sample(in_sample), .in_index(source_index),
        .in_ready(in_ready), .busy(), .candidate_valid(candidate_valid), .candidate_index(candidate_index),
        .candidate_latched(candidate_latched), .protocol_fault(protocol_fault),
        .filtered_valid(), .filtered_history_valid(), .filtered_index(), .filtered_samples(),
        .block_valid(), .block_index(), .strong_mask(), .qualifying(), .streak(),
        .block_energies(), .active_counts());
endmodule
