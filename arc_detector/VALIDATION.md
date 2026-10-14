# Host reference validation

Validated on 14 October 2026 with Python 3.12, NumPy 2.2.6 and SciPy 1.15.3.
The independent review's reproduced timing, reentry and reporting defects were
fixed before this validation. Commands from the repository root:

```text
python -m unittest discover -s arc_detector/tests -p 'test_*.py' -v
python -m unittest discover -s scripts/tests -p 'test_*.py' -v
python scripts/verify_ci_policy.py
python -m arc_detector demo --output-dir .pio/arc-demo-final-20261014
git diff --check
```

Results: 94 arc reference tests and 52 existing release-tooling tests passed;
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
| Explicit mode binding and latched mode loss | `test_qualified_profile_requires_explicit_exact_operating_mode`, `test_mode_loss_revokes_existing_permit_and_requires_explicit_reset`, `test_adc_wrapper_checks_mode_before_first_complete_decimation_interval`, `test_prototype_cannot_treat_unknown_mode_as_matching_envelopes` |
| No partial or overwritten calibration pair | `test_calibration_pair_collision_preserves_existing_file_without_partial_profile`, `test_calibration_pair_second_publish_failure_rolls_back_both_artifacts`, `test_calibration_pair_raced_collision_retains_new_operator_file`, `test_calibration_pair_success_has_complete_profile_and_provenance` |
| Explicit unavailable, spectral-only and full tier telemetry | `test_tier_telemetry_distinguishes_unavailable_robust_only_and_full` |
| Pre-capture tails cannot inflate residual fit, thresholds or floors | `test_calibration_excludes_unmodelled_prefix_before_residual_filters_and_floor`, `test_residual_calibration_requires_eight_frames_after_predictor_warmup`, `test_fit_settling_samples_must_leave_valid_objective` |
| Invalid ADC words preserve earlier decisions and actual fault time | `test_late_invalid_word_preserves_prefix_time_and_consumed_continuity`, `test_late_invalid_word_cannot_preempt_an_earlier_arc` |
| Rounded coefficients remain representable in signed Q1.23 | `test_q1_23_positive_rounding_cannot_overflow_the_signed_integer`, `test_q1_23_lower_boundary_remains_representable` |
| Only currently applicable paths with completed frames can grant permit | `test_fresh_residual_path_cannot_inherit_raw_frame_readiness`, `test_fresh_raw_path_cannot_inherit_residual_frame_readiness`, `test_raw_readiness_survives_only_cancellation_changes` |
| Sub-decimation applicability/reference changes are retained | `test_one_adc_sample_coverage_loss_latches_before_partial_interval_is_completed`, `test_adc_reference_reset_persists_across_incomplete_decimation_interval` |
| Empty/partial calls retain qualification and tier telemetry | `test_initial_empty_calls_emit_pending_unqualified_decision_once`, `test_partial_adc_calls_report_current_available_tiers` |

Automated PR-review findings (mode applicability, calibration-pair publication,
tier reporting, pre-capture residual contamination, path readiness, sub-interval
coverage loss, local ADC word failures, empty-input reporting and coefficient
rounding overflow) were reproduced, fixed and independently rechecked.
Prefix exclusion was also
compared against the original-stream causal FIR at multiple absolute sample
phases; calibration retains the decimator's physical phase.

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
