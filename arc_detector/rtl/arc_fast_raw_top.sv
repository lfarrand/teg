// Complete passive RAW fast-tier candidate path. No electrical permit output.
// Source indices are absolute 4 MSPS samples; block/candidate indices are
// exclusive source ends. Input must be contiguous, with >=50 clocks/sample.
module arc_fast_raw_top #(
    parameter integer MULTIRATE = 0,
    parameter PREFIX_FILE = "prefix.hex",
    parameter BANK0_FILE = "bank0.hex",
    parameter BANK1_FILE = "bank1.hex",
    parameter BANK2_FILE = "bank2.hex",
    parameter BANK3_FILE = "bank3.hex",
    parameter BANK4_FILE = "bank4.hex",
    parameter BANK5_FILE = "bank5.hex",
    parameter ENERGY_FILE = "energy.hex",
    parameter ACTIVITY_FILE = "activity.hex"
) (
    input wire clk,
    input wire rst,
    input wire in_valid,
    input wire signed [23:0] in_sample,
    input wire [63:0] in_index,
    output wire in_ready,
    output wire busy,
    output reg candidate_valid,
    output reg [63:0] candidate_index,
    output reg candidate_latched,
    output reg protocol_fault,
    output reg filtered_valid,
    output reg filtered_history_valid,
    output reg [63:0] filtered_index,
    output reg [191:0] filtered_samples,
    output reg block_valid,
    output reg [63:0] block_index,
    output reg [5:0] strong_mask,
    output reg qualifying,
    output reg [31:0] streak,
    output reg [383:0] block_energies,
    output reg [23:0] active_counts
);
    localparam integer BANK_TAPS = MULTIRATE ? 129 : 257;
    localparam integer BANK_LANES = MULTIRATE ? 1 : 3;
    localparam integer BANK_INPUT_BITS = MULTIRATE ? 26 : 24;
    localparam integer BANK_OUTPUT_BITS = MULTIRATE ? 27 : 25;
    localparam integer BLOCK_SAMPLES = MULTIRATE ? 128 : 256;
    localparam integer FRAGMENT_SAMPLES = MULTIRATE ? 16 : 32;
    localparam integer BLOCK_SHIFT = MULTIRATE ? 7 : 8;
    localparam integer FRAGMENT_SHIFT = MULTIRATE ? 4 : 5;
    reg [63:0] energy_threshold [0:5];
    reg [63:0] activity_threshold [0:5];
    initial begin
        $readmemh(ENERGY_FILE, energy_threshold);
        $readmemh(ACTIVITY_FILE, activity_threshold);
    end

    reg have_input;
    reg [63:0] expected_index, observed_end;
    reg pending_valid;
    wire [5:0] bank_ready, bank_busy, bank_fault, bank_valid, bank_warm;
    wire signed [31:0] bank_samples [0:5];
    wire [63:0] bank_indices [0:5];
    wire signed [63:0] bank_squares [0:5];
    wire prefix_ready, prefix_busy, prefix_fault, prefix_valid, prefix_warm;
    wire signed [31:0] prefix_sample;
    wire [63:0] prefix_index;
    wire stage_ready = MULTIRATE ? prefix_ready : (&bank_ready);
    wire any_fir_fault = (|bank_fault) || prefix_fault;
    assign in_ready = stage_ready && !protocol_fault && !any_fir_fault;
    wire index_ok = !in_index[63] && (in_index != 64'h7fffffffffffffff) &&
                    (!have_input || in_index == expected_index);
    wire input_fault_now = in_valid && (!in_ready || !index_ok);
    wire accept_input = in_valid && in_ready && index_ok;
    wire [63:0] observed_now = accept_input ? in_index + 1 : observed_end;
    assign busy = prefix_busy || (|bank_busy) || pending_valid;
    wire bank_input_valid = MULTIRATE ? (prefix_valid && (&bank_ready) && !protocol_fault) : accept_input;
    wire signed [BANK_INPUT_BITS-1:0] bank_input_sample = MULTIRATE ? prefix_sample[BANK_INPUT_BITS-1:0] : in_sample;
    wire [63:0] bank_input_index = MULTIRATE ? (prefix_index >> 1) : in_index;
    reg prefix_have_valid;
    reg [63:0] prefix_first_valid;

    generate
        if (MULTIRATE) begin: prefix_stage
            arc_serial_fir #(.TAPS(127), .LANES(2), .DATA_BITS(24), .OUTPUT_BITS(26),
                             .DECIMATE(2), .COEFF_FILE(PREFIX_FILE)) prefix (
                .clk(clk), .rst(rst), .in_valid(accept_input), .in_sample(in_sample), .in_index(in_index),
                .in_ready(prefix_ready), .busy(prefix_busy), .fault(prefix_fault), .out_valid(prefix_valid),
                .out_history_valid(prefix_warm), .out_sample(prefix_sample), .out_index(prefix_index));
        end else begin: no_prefix_stage
            assign prefix_ready = 1'b1;
            assign prefix_busy = 1'b0;
            assign prefix_fault = 1'b0;
            assign prefix_valid = 1'b0;
            assign prefix_warm = 1'b0;
            assign prefix_sample = 32'sd0;
            assign prefix_index = 64'd0;
        end
        for (genvar g = 0; g < 6; g = g + 1) begin: bands
            localparam FILE_NAME = g == 0 ? BANK0_FILE : g == 1 ? BANK1_FILE :
                                  g == 2 ? BANK2_FILE : g == 3 ? BANK3_FILE :
                                  g == 4 ? BANK4_FILE : BANK5_FILE;
            arc_serial_fir #(.TAPS(BANK_TAPS), .LANES(BANK_LANES), .DATA_BITS(BANK_INPUT_BITS),
                             .OUTPUT_BITS(BANK_OUTPUT_BITS), .COEFF_FILE(FILE_NAME)) filtered (
                .clk(clk), .rst(rst), .in_valid(bank_input_valid), .in_sample(bank_input_sample),
                .in_index(bank_input_index), .in_ready(bank_ready[g]), .busy(bank_busy[g]),
                .fault(bank_fault[g]), .out_valid(bank_valid[g]), .out_history_valid(bank_warm[g]),
                .out_sample(bank_samples[g]), .out_index(bank_indices[g]));
            assign bank_squares[g] = bank_samples[g] * bank_samples[g];
        end
    endgenerate

    wire bank_output = &bank_valid;
    wire partial_output = (|bank_valid) && !bank_output;
    wire bank_indices_match = (bank_indices[0] == bank_indices[1]) &&
                             (bank_indices[0] == bank_indices[2]) &&
                             (bank_indices[0] == bank_indices[3]) &&
                             (bank_indices[0] == bank_indices[4]) &&
                             (bank_indices[0] == bank_indices[5]);
    wire [63:0] source_point = MULTIRATE ? (bank_indices[0] << 1) : bank_indices[0];
    wire full_history = (&bank_warm) && (!MULTIRATE ||
                        (prefix_have_valid && bank_indices[0] >= prefix_first_valid + BANK_TAPS-1));
    wire stream_fault_now = input_fault_now || any_fir_fault || partial_output ||
                           (bank_output && !bank_indices_match) ||
                           (MULTIRATE && prefix_valid && !(&bank_ready));

    reg [63:0] energy_accumulator [0:5];
    reg [63:0] fragment_accumulator [0:5];
    reg [3:0] fragment_active [0:5];
    reg [8:0] samples_in_block;
    reg [5:0] samples_in_fragment;
    reg [63:0] pending_index;
    reg [5:0] pending_mask;
    reg pending_qualifying;
    reg [383:0] pending_energies;
    reg [23:0] pending_counts;
    reg [31:0] streak_counter;
    reg [64:0] sum_energy, sum_fragment;
    reg [79:0] rhs_energy, rhs_activity;
    reg [3:0] count_after;
    reg [5:0] mask_after;
    reg [383:0] energies_after;
    reg [23:0] counts_after;
    reg [3:0] strong_count;
    reg [31:0] streak_after;
    integer band;

    always @(posedge clk) begin
        if (rst) begin
            have_input <= 0;
            expected_index <= 0;
            observed_end <= 0;
            protocol_fault <= 0;
            candidate_valid <= 0;
            candidate_latched <= 0;
            candidate_index <= 0;
            prefix_have_valid <= 0;
            prefix_first_valid <= 0;
            filtered_valid <= 0;
            filtered_history_valid <= 0;
            filtered_index <= 0;
            filtered_samples <= 0;
            block_valid <= 0;
            block_index <= 0;
            strong_mask <= 0;
            qualifying <= 0;
            streak <= 0;
            block_energies <= 0;
            active_counts <= 0;
            samples_in_block <= 0;
            samples_in_fragment <= 0;
            pending_valid <= 0;
            pending_index <= 0;
            pending_mask <= 0;
            pending_qualifying <= 0;
            pending_energies <= 0;
            pending_counts <= 0;
            streak_counter <= 0;
            for (band = 0; band < 6; band = band + 1) begin
                energy_accumulator[band] <= 0;
                fragment_accumulator[band] <= 0;
                fragment_active[band] <= 0;
            end
        end else begin
            candidate_valid <= 0;
            filtered_valid <= 0;
            block_valid <= 0;
            if (stream_fault_now) protocol_fault <= 1;
            if (accept_input) begin
                have_input <= 1;
                expected_index <= in_index + 1;
                observed_end <= in_index + 1;
            end
            if (MULTIRATE && prefix_valid && prefix_warm && !prefix_have_valid) begin
                prefix_have_valid <= 1;
                prefix_first_valid <= prefix_index >> 1;
            end
            if (bank_output && !protocol_fault && !stream_fault_now) begin
                filtered_valid <= 1;
                filtered_history_valid <= full_history;
                filtered_index <= source_point;
                for (band = 0; band < 6; band = band + 1)
                    filtered_samples[32*band +: 32] <= bank_samples[band];
                if (full_history) begin
                    mask_after = 0;
                    energies_after = 0;
                    counts_after = 0;
                    strong_count = 0;
                    for (band = 0; band < 6; band = band + 1) begin
                        sum_energy = {1'b0, energy_accumulator[band]} + {1'b0, bank_squares[band]};
                        sum_fragment = {1'b0, fragment_accumulator[band]} + {1'b0, bank_squares[band]};
                        rhs_energy = {16'd0, energy_threshold[band]} << (3 + BLOCK_SHIFT);
                        rhs_activity = {16'd0, activity_threshold[band]} << (3 + FRAGMENT_SHIFT);
                        if (sum_energy[64:63] != 0 || sum_fragment[64:63] != 0) protocol_fault <= 1;
                        energy_accumulator[band] <= sum_energy[63:0];
                        fragment_accumulator[band] <= sum_fragment[63:0];
                        count_after = fragment_active[band];
                        if (samples_in_fragment == FRAGMENT_SAMPLES-1) begin
                            if ({16'd0, sum_fragment[63:0]} > rhs_activity) count_after = count_after + 1;
                            fragment_active[band] <= count_after;
                            fragment_accumulator[band] <= 0;
                        end
                        if (samples_in_block == BLOCK_SAMPLES-1) begin
                            mask_after[band] = ({16'd0, sum_energy[63:0]} > rhs_energy) && count_after >= 6;
                            strong_count = strong_count + mask_after[band];
                            energies_after[64*band +: 64] = sum_energy[63:0];
                            counts_after[4*band +: 4] = count_after;
                            energy_accumulator[band] <= 0;
                            fragment_active[band] <= 0;
                        end
                    end
                    if (samples_in_fragment == FRAGMENT_SAMPLES-1) samples_in_fragment <= 0;
                    else samples_in_fragment <= samples_in_fragment + 1;
                    if (samples_in_block == BLOCK_SAMPLES-1) begin
                        samples_in_block <= 0;
                        if (pending_valid) protocol_fault <= 1;
                        pending_valid <= 1;
                        pending_index <= source_point + (MULTIRATE ? 2 : 1);
                        pending_mask <= mask_after;
                        pending_qualifying <= (strong_count >= 4) && (|mask_after[1:0]) &&
                                              (|mask_after[3:2]) && (|mask_after[5:4]);
                        pending_energies <= energies_after;
                        pending_counts <= counts_after;
                    end else samples_in_block <= samples_in_block + 1;
                end
            end
            // A /2 output observed at source index N cannot finish its 64 us
            // block until source N+1 has also been accepted. Never backdate it.
            if (pending_valid && pending_index <= observed_now && !protocol_fault && !stream_fault_now) begin
                pending_valid <= 0;
                block_valid <= 1;
                block_index <= pending_index;
                strong_mask <= pending_mask;
                qualifying <= pending_qualifying;
                block_energies <= pending_energies;
                active_counts <= pending_counts;
                streak_after = pending_qualifying ? streak_counter + 1 : 0;
                if (pending_qualifying && (&streak_counter)) protocol_fault <= 1;
                else begin
                    streak_counter <= streak_after;
                    streak <= streak_after;
                    if (streak_after == 8) begin
                        candidate_valid <= 1;
                        candidate_latched <= 1;
                        candidate_index <= pending_index;
                    end
                end
            end
        end
    end
endmodule
