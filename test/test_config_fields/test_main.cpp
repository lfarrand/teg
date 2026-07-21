// Tests for the web-form field -> MainConfig mapping (config_fields.h)

#include <unity.h>
#include <config_fields.h>

static MainConfig cfg;

void setUp() { cfg = MainConfig{}; }
void tearDown() {}

// Every recognised field name of the PWM settings form. Applying all of them
// exercises every branch of the mapper.
static const char *const numericFields[] = {
  "period-13a", "period-13b",
  "pwm-frequency-13", "pwm-frequency-20", "pwm-frequency-21", "pwm-frequency-22",
  "pwm-frequency-23", "pwm-frequency-31", "pwm-frequency-40", "pwm-frequency-41",
  "pwm-frequency-42",
  "dead-time-13", "dead-time-20", "dead-time-21", "dead-time-22", "dead-time-23",
  "dead-time-31", "dead-time-40", "dead-time-41", "dead-time-42",
  "duty-cycle-13a", "duty-cycle-13b", "duty-cycle-20a", "duty-cycle-20b",
  "duty-cycle-21a", "duty-cycle-22a", "duty-cycle-22b", "duty-cycle-23a",
  "duty-cycle-23b", "duty-cycle-31a", "duty-cycle-31b", "duty-cycle-40a",
  "duty-cycle-41a", "duty-cycle-42a", "duty-cycle-42b",
  "phase-shift-21a", "phase-shift-22a", "phase-shift-22b", "phase-shift-23a",
  "phase-shift-23b", "phase-shift-41a", "phase-shift-42a", "phase-shift-42b",
  "spwm-carrier-signal-frequency", "spwm-modulation-frequency",
  "modulation-scheme", "modulation-index", "modulation-cells", "carrier-disposition",
  "soft-start-ms", "feedback-setpoint-mv", "feedback-kp", "feedback-ki", "fault-pin",
  "reference-waveform", "dpwm-variant", "dpwm-clamp-angle", "carrier-dither-mode",
  "carrier-dither-percent",
  "asymmetric-induction-preshiftnanos", "asymmetric-induction-postshiftnanos",
};

static const char *const booleanFields[] = {
  "print-regs", "sync-pwm", "use-spwm", "enable-asymmetric-induction",
  "deadtime-compensation", "feedback-enabled", "fault-enabled", "fault-active-high",
  "nearest-level",
};

void test_all_numeric_fields_recognised() {
  for (const char *name : numericFields) {
    TEST_ASSERT_TRUE_MESSAGE(applyConfigFormField(cfg, name, "123"), name);
  }
}

void test_all_boolean_fields_recognised_yes_and_no() {
  for (const char *name : booleanFields) {
    TEST_ASSERT_TRUE_MESSAGE(applyConfigFormField(cfg, name, "Yes"), name);
    TEST_ASSERT_TRUE_MESSAGE(applyConfigFormField(cfg, name, "No"), name);
  }
}

void test_numeric_fields_reach_the_right_slots() {
  applyConfigFormField(cfg, "pwm-frequency-20", "20000");
  applyConfigFormField(cfg, "dead-time-42", "75");
  applyConfigFormField(cfg, "duty-cycle-13b", "40000");
  applyConfigFormField(cfg, "phase-shift-23b", "7");
  applyConfigFormField(cfg, "period-13a", "1500");
  applyConfigFormField(cfg, "spwm-carrier-signal-frequency", "24000");
  applyConfigFormField(cfg, "spwm-modulation-frequency", "60");
  applyConfigFormField(cfg, "asymmetric-induction-preshiftnanos", "-250");
  applyConfigFormField(cfg, "modulation-scheme", "4");
  applyConfigFormField(cfg, "modulation-index", "1155");
  applyConfigFormField(cfg, "modulation-cells", "4");
  applyConfigFormField(cfg, "carrier-disposition", "2");

  applyConfigFormField(cfg, "soft-start-ms", "750");
  applyConfigFormField(cfg, "feedback-setpoint-mv", "2400");
  applyConfigFormField(cfg, "feedback-kp", "333");
  applyConfigFormField(cfg, "fault-pin", "31");
  applyConfigFormField(cfg, "feedback-enabled", "Yes");
  applyConfigFormField(cfg, "fault-active-high", "No");
  applyConfigFormField(cfg, "reference-waveform", "1");
  applyConfigFormField(cfg, "dpwm-variant", "3");
  applyConfigFormField(cfg, "dpwm-clamp-angle", "-30");
  applyConfigFormField(cfg, "carrier-dither-mode", "2");
  applyConfigFormField(cfg, "carrier-dither-percent", "20");

  TEST_ASSERT_EQUAL_UINT8(4, cfg.Pwm.Tm2.ModulationScheme);
  TEST_ASSERT_EQUAL_UINT16(1155, cfg.Pwm.Tm2.ModulationIndexMilli);
  TEST_ASSERT_EQUAL_UINT8(4, cfg.Pwm.Tm2.ModulationCells);
  TEST_ASSERT_EQUAL_UINT8(2, cfg.Pwm.Tm2.CarrierDisposition);
  TEST_ASSERT_EQUAL_UINT16(750, cfg.Pwm.Tm2.SoftStartMs);
  TEST_ASSERT_EQUAL_UINT32(2400, cfg.Feedback.SetpointMillivolts);
  TEST_ASSERT_EQUAL_UINT16(333, cfg.Feedback.KpMilli);
  TEST_ASSERT_EQUAL_UINT8(31, cfg.FaultProtection.Pin);
  TEST_ASSERT_TRUE(cfg.Feedback.Enabled);
  TEST_ASSERT_FALSE(cfg.FaultProtection.ActiveHigh);
  TEST_ASSERT_EQUAL_UINT8(1, cfg.Pwm.Tm2.ReferenceWaveform);
  TEST_ASSERT_EQUAL_UINT8(3, cfg.Pwm.Tm2.DpwmVariant);
  TEST_ASSERT_EQUAL_INT8(-30, cfg.Pwm.Tm2.DpwmClampAngleDeg);
  TEST_ASSERT_EQUAL_UINT8(2, cfg.Pwm.Tm2.CarrierDitherMode);
  TEST_ASSERT_EQUAL_UINT8(20, cfg.Pwm.Tm2.CarrierDitherPercent);
  TEST_ASSERT_EQUAL_UINT32(20000, cfg.Pwm.Tm2.Sm20.PwmFrequency);
  TEST_ASSERT_EQUAL_UINT16(75, cfg.Pwm.Tm4.Sm42.DeadTime);
  TEST_ASSERT_EQUAL_UINT16(40000, cfg.Pwm.Tm1.Sm13.ChannelB.DutyCycle);
  TEST_ASSERT_EQUAL_UINT8(7, cfg.Pwm.Tm2.Sm23.ChannelB.PhaseShift);
  TEST_ASSERT_EQUAL_UINT32(1500, cfg.Pwm.Tm1.Sm13.ChannelA.OnPeriodMicroseconds);
  TEST_ASSERT_EQUAL_UINT32(24000, cfg.Pwm.Tm2.SpwmCarrierFrequency);
  TEST_ASSERT_EQUAL_UINT32(60, cfg.Pwm.Tm2.SpwmModulationFrequency);
  TEST_ASSERT_EQUAL_INT32(-250, cfg.AsymmetricInduction.PreShiftNanos);
}

void test_boolean_fields_set_and_clear() {
  applyConfigFormField(cfg, "use-spwm", "Yes");
  applyConfigFormField(cfg, "sync-pwm", "Yes");
  applyConfigFormField(cfg, "print-regs", "Yes");
  applyConfigFormField(cfg, "enable-asymmetric-induction", "Yes");
  TEST_ASSERT_TRUE(cfg.Pwm.Tm2.UseSpwm);
  TEST_ASSERT_TRUE(cfg.Pwm.SyncPwm);
  TEST_ASSERT_TRUE(cfg.Pwm.PrintRegs);
  TEST_ASSERT_TRUE(cfg.AsymmetricInduction.IsEnabled);

  applyConfigFormField(cfg, "use-spwm", "No");
  applyConfigFormField(cfg, "sync-pwm", "anything-not-Yes");
  TEST_ASSERT_FALSE(cfg.Pwm.Tm2.UseSpwm);
  TEST_ASSERT_FALSE(cfg.Pwm.SyncPwm);
}

void test_unknown_field_rejected_and_config_untouched() {
  MainConfig before = cfg;
  TEST_ASSERT_FALSE(applyConfigFormField(cfg, "definitely-not-a-field", "42"));
  TEST_ASSERT_FALSE(applyConfigFormField(cfg, "", "42"));
  TEST_ASSERT_EQUAL_UINT32(before.Pwm.Tm2.Sm20.PwmFrequency, cfg.Pwm.Tm2.Sm20.PwmFrequency);
}

void test_timer_form_fields() {
  // Checkbox semantics: handler resets Enabled before parsing
  cfg.Pwm.Tm1.Sm13.ChannelA.Enabled = false;
  cfg.Pwm.Tm1.Sm13.ChannelB.Enabled = false;

  TEST_ASSERT_TRUE(applyTimerFormField(cfg, "period-13a", "2500"));
  TEST_ASSERT_TRUE(applyTimerFormField(cfg, "period-13b", "3500"));
  TEST_ASSERT_TRUE(applyTimerFormField(cfg, "toggle-13a", "on"));
  TEST_ASSERT_TRUE(applyTimerFormField(cfg, "toggle-13b", "off"));

  TEST_ASSERT_EQUAL_UINT32(2500, cfg.Pwm.Tm1.Sm13.ChannelA.OnPeriodMicroseconds);
  TEST_ASSERT_EQUAL_UINT32(3500, cfg.Pwm.Tm1.Sm13.ChannelB.OnPeriodMicroseconds);
  TEST_ASSERT_TRUE(cfg.Pwm.Tm1.Sm13.ChannelA.Enabled);
  TEST_ASSERT_FALSE(cfg.Pwm.Tm1.Sm13.ChannelB.Enabled); // "off" never enables

  TEST_ASSERT_FALSE(applyTimerFormField(cfg, "pwm-frequency-20", "1000")); // wrong form
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_all_numeric_fields_recognised);
  RUN_TEST(test_all_boolean_fields_recognised_yes_and_no);
  RUN_TEST(test_numeric_fields_reach_the_right_slots);
  RUN_TEST(test_boolean_fields_set_and_clear);
  RUN_TEST(test_unknown_field_rejected_and_config_untouched);
  RUN_TEST(test_timer_form_fields);
  return UNITY_END();
}
