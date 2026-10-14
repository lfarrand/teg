# Host reference validation

Validated on 14 October 2026 with Python 3.12, NumPy 2.2.6 and SciPy 1.15.3.
The independent review's reproduced timing, reentry and reporting defects were
fixed before this validation. Commands from the repository root:

```text
python -m unittest discover -s arc_detector/tests -p 'test_*.py' -v
python -m unittest discover -s scripts/tests -p 'test_*.py' -v
python scripts/verify_ci_policy.py
python -m arc_detector demo --output-dir .pio/arc-demo-20261014
git diff --check
```

Results: 71 arc reference tests and 52 existing release-tooling tests passed;
workflow policy, synthetic CLI demo and whitespace checks passed. This is not a
firmware test census. No firmware/PlatformIO inputs or TEG power-control paths
were modified, so these results make no ISR, OUTEN or power-stage claim.

## Requirement evidence

All names below are executable unittest cases under `arc_detector/tests`.

| Requirement | Evidence |
|---|---|
| Exact ADC sign/randomisation decoding | `test_fixed_plain_vectors_preserve_sign_and_user_bits`, `test_fixed_prbs_vectors_use_appended_reference_not_conversion_lsb`, `test_fixed_lsb_vectors_leave_conversion_lsb_unchanged` |
| Dual-gain handover and unavailable clipping | `test_gain_handover_preserves_input_units_and_flags_double_clipping`, `test_middle_clip_reports_physical_time_independent_of_host_chunk` |
| ADC-to-decimator-to-detector composition | `test_adc_decode_to_real_decimator_and_detector_composition`, `test_twenty_msps_wrapper_chunk_invariant_even_inside_decimation_intervals` |
| Anti-alias response and complete settling | `test_quantised_filters_meet_measured_passband_and_antialias_targets`, `test_twenty_msps_cascaded_settling_excludes_incomplete_fir_history` |
| Frozen, phase-aligned recorded noise | `test_joint_fit_matches_predictor_including_fractional_delay`, `test_live_or_unverified_recording_cannot_train`, `test_calibration_envelopes_are_frozen_and_never_hardware_qualified` |
| Raw detector cannot be cancelled away | `test_raw_path_cannot_be_vetoed_by_cancellation_matching_signal`, `test_correlated_and_startup_arcs_survive_cancellation` |
| Energy and broadband occupancy together | `test_broadband_features_require_both_energy_and_occupancy`, `test_exclusions_never_qualify_less_than_sixty_percent_band` |
| Three strong frames and weak elapsed-time decay | `test_strong_requires_three_bands_and_three_consecutive_complete_frames`, `test_weak_accumulates_physical_time_with_one_quarter_decay` |
| Persistent ambiguity and clear recovery | `test_suspicion_timeout_before_at_and_after_exact_sample_deadline`, `test_suspect_only_clears_after_zero_and_two_clear_raw_frames` |
| Separately enabled fast tier and impulse rejection | `test_fast_requires_four_subbands_all_octaves_and_eight_blocks`, `test_one_impulse_cannot_pass_sustained_fast_guard`, `test_fast_adc_wrapper_settling_parity_with_tiny_initial_chunks` |
| Latched health, explicit reset and continued sensing | `test_health_failure_partitions_latch_and_need_inhibited_diagnostic_reset`, `test_detection_and_sample_evidence_continue_after_trip` |
| Unavailable-profile recovery discards stale evidence | `test_raw_profile_reentry_discards_old_qualified_streak_and_filter_history` |
| Causal timestamps, including late clipped samples | `test_no_spectral_frame_or_decision_can_use_unobserved_future_samples`, `test_mid_chunk_clipping_cannot_preempt_an_earlier_arc` |
| Bounded recordings and strict profiles | `test_npz_rejects_oversized_claimed_array_before_numpy_allocation`, `test_csv_streams_exact_schema_and_rejects_truncation`, `test_unknown_schema_and_unexpected_fields_rejected` |
| Labels do not leak into detection | `test_labels_cannot_change_detector_decisions_and_chunks_do_not_change_time` |
| Health shutdown, arc decision and extinction are separate | `test_health_shutdown_is_protection_without_an_arc_classification`, `test_event_metrics_do_not_equate_terminal_zero_and_extinction` |
| Qualification and prototype permit defaults | `test_unqualified_mode_rejects_permit_and_prototype_never_issues_permit`, `test_default_cli_never_claims_unqualified_protection_and_prototype_is_explicit` |

## Numerical observations and limits

The deterministic example detects its later and startup synthetic injections
with experimental sample-clock latency below 10 ms. Both reports say
`synthetic=true`, `hardware_qualification=false`, `permit=false`. These are
constructed signals, not independently recorded electrical arcs. They establish
software composition and state behavior, not detection sensitivity, nuisance
rate, field latency or safety certification.

The quantised FIR coefficient responses meet the tested alias/passband targets.
The reference uses floating-point accumulators and Q1.23 coefficients; RTL
word lengths, accumulator overflow, complete analogue impulse tails and timing
closure still require separate proof. No Lattice synthesis/place/route or
ADS9219 hardware capture was performed.

Real series/parallel recordings, held-out installations and nuisance exposure
remain necessary. Source/sensor coverage and actual shutdown must be verified
independently; terminal current-zero is not parallel-arc extinction.
