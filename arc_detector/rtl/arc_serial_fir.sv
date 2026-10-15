// Passive research datapath. Signed Q1.23 coefficients, exact symmetric MACs.
// Each output uses issue/preadd, registered multiply, and accumulate stages.
module arc_serial_fir #(
    parameter integer TAPS = 257,
    parameter integer LANES = 3,
    parameter integer DATA_BITS = 24,
    parameter integer OUTPUT_BITS = 25,
    parameter integer DECIMATE = 1,
    parameter COEFF_FILE = "bank0.hex"
) (
    input wire clk,
    input wire rst,
    input wire in_valid,
    input wire signed [DATA_BITS-1:0] in_sample,
    input wire [63:0] in_index,
    output wire in_ready,
    output reg busy,
    output reg fault,
    output reg out_valid,
    output reg out_history_valid,
    output reg signed [31:0] out_sample,
    output reg [63:0] out_index
);
    localparam integer UNIQUE_TAPS = (TAPS + 1) / 2;
    localparam integer PRODUCT_BITS = DATA_BITS + 25;
    reg signed [23:0] coefficients [0:UNIQUE_TAPS-1];
    reg signed [DATA_BITS-1:0] history [0:TAPS-1];
    reg signed [DATA_BITS:0] preadd [0:LANES-1];
    reg signed [23:0] coefficient_pipe [0:LANES-1];
    reg signed [PRODUCT_BITS-1:0] products [0:LANES-1];
    reg [63:0] first_index;
    reg have_first;
    reg [63:0] captured_index;
    reg captured_history_valid;
    reg issuing;
    integer issue_index;
    reg issue_valid, issue_last;
    reg multiply_valid, multiply_last;
    reg finish_pending;
    reg signed [63:0] accumulator;
    reg signed [66:0] wide_sum;
    reg signed [63:0] rounded;
    integer i, lane, tap;

    initial $readmemh(COEFF_FILE, coefficients);
    assign in_ready = !busy && !fault;

    function automatic signed [63:0] round_q23(input signed [63:0] value);
        reg signed [63:0] quotient;
        reg [22:0] remainder;
        begin
            quotient = value >>> 23;
            remainder = value[22:0];
            if ((remainder > 23'h400000) ||
                ((remainder == 23'h400000) && quotient[0]))
                quotient = quotient + 64'sd1;
            round_q23 = quotient;
        end
    endfunction

    always @(posedge clk) begin
        if (rst) begin
            busy <= 0;
            fault <= 0;
            out_valid <= 0;
            out_history_valid <= 0;
            out_sample <= 0;
            out_index <= 0;
            have_first <= 0;
            first_index <= 0;
            captured_index <= 0;
            captured_history_valid <= 0;
            issuing <= 0;
            issue_index <= 0;
            issue_valid <= 0;
            issue_last <= 0;
            multiply_valid <= 0;
            multiply_last <= 0;
            finish_pending <= 0;
            accumulator <= 0;
            for (i = 0; i < TAPS; i = i + 1) history[i] <= 0;
            for (lane = 0; lane < LANES; lane = lane + 1) begin
                preadd[lane] <= 0;
                coefficient_pipe[lane] <= 0;
                products[lane] <= 0;
            end
        end else begin
            out_valid <= 0;
            if (in_valid) begin
                if (!in_ready) fault <= 1;
                else begin
                    history[0] <= in_sample;
                    for (i = 1; i < TAPS; i = i + 1) history[i] <= history[i-1];
                    if (!have_first) begin
                        have_first <= 1;
                        first_index <= in_index;
                    end
                    if ((DECIMATE == 1) || !in_index[0]) begin
                        captured_index <= in_index;
                        captured_history_valid <= have_first && (in_index - first_index >= TAPS-1);
                        busy <= 1;
                        issuing <= 1;
                        issue_index <= 0;
                        issue_valid <= 0;
                        issue_last <= 0;
                        multiply_valid <= 0;
                        multiply_last <= 0;
                        finish_pending <= 0;
                        accumulator <= 0;
                    end
                end
            end
            if (busy && !fault) begin
                issue_valid <= 0;
                multiply_valid <= issue_valid;
                multiply_last <= issue_last;
                if (issuing) begin
                    issue_valid <= 1;
                    issue_last <= (issue_index + LANES >= UNIQUE_TAPS);
                    if (issue_index + LANES >= UNIQUE_TAPS) issuing <= 0;
                    else issue_index <= issue_index + LANES;
                    for (lane = 0; lane < LANES; lane = lane + 1) begin
                        tap = issue_index + lane;
                        if (tap < UNIQUE_TAPS) begin
                            if (tap == TAPS-1-tap)
                                preadd[lane] <= $signed({history[tap][DATA_BITS-1], history[tap]});
                            else
                                preadd[lane] <= $signed({history[tap][DATA_BITS-1], history[tap]}) +
                                                $signed({history[TAPS-1-tap][DATA_BITS-1], history[TAPS-1-tap]});
                            coefficient_pipe[lane] <= coefficients[tap];
                        end else begin
                            preadd[lane] <= 0;
                            coefficient_pipe[lane] <= 0;
                        end
                    end
                end
                if (issue_valid)
                    for (lane = 0; lane < LANES; lane = lane + 1)
                        products[lane] <= preadd[lane] * coefficient_pipe[lane];
                if (multiply_valid) begin
                    wide_sum = {{3{accumulator[63]}}, accumulator};
                    for (lane = 0; lane < LANES; lane = lane + 1)
                        wide_sum = wide_sum + {{(67-PRODUCT_BITS){products[lane][PRODUCT_BITS-1]}}, products[lane]};
                    if ((wide_sum > 67'sh07fffffffffffffff) ||
                        (wide_sum < -67'sh08000000000000000)) fault <= 1;
                    else accumulator <= wide_sum[63:0];
                    if (multiply_last) finish_pending <= 1;
                end
                if (finish_pending) begin
                    rounded = round_q23(accumulator);
                    if ((rounded < -(64'sd1 <<< (OUTPUT_BITS-1))) ||
                        (rounded > ((64'sd1 <<< (OUTPUT_BITS-1)) - 1))) fault <= 1;
                    else begin
                        out_sample <= rounded[31:0];
                        out_index <= captured_index;
                        out_history_valid <= captured_history_valid;
                        out_valid <= 1;
                    end
                    finish_pending <= 0;
                    busy <= 0;
                end
            end
        end
    end
endmodule
