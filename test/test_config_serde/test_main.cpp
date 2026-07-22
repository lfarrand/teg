// Tests for the MainConfig <-> JSON mapping (config_serde.h)

#include <unity.h>
#include <string>
#include <config_serde.h>

void setUp() {}
void tearDown() {}

static std::string toJsonString(const MainConfig &cfg) {
  JsonDocument doc;
  configToJson(cfg, doc);
  std::string out;
  serializeJson(doc, out);
  return out;
}

void test_defaults_from_empty_document() {
  JsonDocument doc; // empty: every field falls back to its default
  MainConfig cfg;
  configFromJson(doc, cfg);

  TEST_ASSERT_FALSE(cfg.AsymmetricInduction.IsEnabled);
  TEST_ASSERT_EQUAL_INT32(250, cfg.AsymmetricInduction.PreShiftNanos);
  TEST_ASSERT_EQUAL_INT32(500, cfg.AsymmetricInduction.PostShiftNanos);
  TEST_ASSERT_FALSE(cfg.Pwm.PrintRegs);
  TEST_ASSERT_FALSE(cfg.Pwm.SyncPwm);
  TEST_ASSERT_FALSE(cfg.Pwm.Tm2.UseSpwm);
  TEST_ASSERT_EQUAL_UINT32(20000, cfg.Pwm.Tm2.SpwmCarrierFrequency);
  TEST_ASSERT_EQUAL_UINT32(50, cfg.Pwm.Tm2.SpwmModulationFrequency);
  TEST_ASSERT_EQUAL_UINT8(1, cfg.Pwm.Tm2.ModulationScheme);
  TEST_ASSERT_EQUAL_UINT16(1000, cfg.Pwm.Tm2.ModulationIndexMilli);
  TEST_ASSERT_EQUAL_UINT8(2, cfg.Pwm.Tm2.ModulationCells);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.Pwm.Tm2.CarrierDisposition);
  TEST_ASSERT_FALSE(cfg.Pwm.Tm2.DeadTimeCompensation);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.Pwm.Tm2.SoftStartMs);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.Pwm.Tm2.ReferenceWaveform);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.Pwm.Tm2.DpwmVariant);
  TEST_ASSERT_EQUAL_INT8(0, cfg.Pwm.Tm2.DpwmClampAngleDeg);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.Pwm.Tm2.CarrierDitherMode);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.Pwm.Tm2.CarrierDitherPercent);
  TEST_ASSERT_FALSE(cfg.Pwm.Tm2.NearestLevelModulation);
  TEST_ASSERT_FALSE(cfg.Feedback.Enabled);
  TEST_ASSERT_EQUAL_UINT8(41, cfg.Feedback.AnalogPin);
  TEST_ASSERT_EQUAL_UINT32(0, cfg.Feedback.SetpointMillivolts);
  TEST_ASSERT_EQUAL_UINT32(3300, cfg.Feedback.FullScaleMillivolts);
  TEST_ASSERT_EQUAL_UINT16(200, cfg.Feedback.KpMilli);
  TEST_ASSERT_EQUAL_UINT16(2000, cfg.Feedback.KiMilli);
  TEST_ASSERT_EQUAL_UINT16(1000, cfg.Feedback.LoopHz);
  TEST_ASSERT_FALSE(cfg.FaultProtection.Enabled);
  TEST_ASSERT_EQUAL_UINT8(32, cfg.FaultProtection.Pin);
  TEST_ASSERT_TRUE(cfg.FaultProtection.ActiveHigh);
  TEST_ASSERT_FALSE(cfg.CurrentLimit.Enabled);
  TEST_ASSERT_EQUAL_UINT8(40, cfg.CurrentLimit.Pin);
  TEST_ASSERT_EQUAL_UINT16(2475, cfg.CurrentLimit.ThresholdMillivolts);
  TEST_ASSERT_FALSE(cfg.CurrentLimit.CycleByCycle);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.CurrentLimit.FilterCount);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.CurrentLimit.FilterPeriod);
  TEST_ASSERT_EQUAL_STRING("ub-1.lan", cfg.Influx.Host);
  TEST_ASSERT_EQUAL_UINT16(8086, cfg.Influx.Port);
  TEST_ASSERT_EQUAL_STRING("power_generator", cfg.Influx.Bucket);
  TEST_ASSERT_EQUAL_STRING("", cfg.Influx.Token); // no token in source or defaults
  TEST_ASSERT_EQUAL_UINT16(10, cfg.Influx.IntervalSeconds);
  TEST_ASSERT_FALSE(cfg.Meter.Enabled);
  TEST_ASSERT_EQUAL_UINT8(40, cfg.Meter.CurrentPin);
  TEST_ASSERT_EQUAL_UINT16(1650, cfg.Meter.VoltageZeroMillivolts);
  TEST_ASSERT_EQUAL_UINT16(1650, cfg.Meter.CurrentZeroMillivolts);
  TEST_ASSERT_EQUAL_UINT16(10000, cfg.Meter.CurrentMilliampPerVolt);
  TEST_ASSERT_EQUAL_UINT16(1000, cfg.Meter.VoltageRatioMilli);
  TEST_ASSERT_EQUAL_STRING("", cfg.Security.WritePin);
  TEST_ASSERT_EQUAL_UINT16(50, cfg.Pwm.Tm1.Sm13.DeadTime);
  TEST_ASSERT_EQUAL_UINT32(1000, cfg.Pwm.Tm1.Sm13.PwmFrequency);
  TEST_ASSERT_EQUAL_UINT16(32768, cfg.Pwm.Tm1.Sm13.ChannelA.DutyCycle);
  TEST_ASSERT_EQUAL_UINT16(50, cfg.Pwm.Tm4.Sm42.DeadTime);
  TEST_ASSERT_EQUAL_UINT16(32768, cfg.Pwm.Tm4.Sm42.ChannelB.DutyCycle);
}

void test_roundtrip_preserves_every_field() {
  // Distinctive values spread across the whole structure
  MainConfig cfg;
  cfg.AsymmetricInduction.IsEnabled = true;
  cfg.AsymmetricInduction.PreShiftNanos = -111;
  cfg.AsymmetricInduction.PostShiftNanos = 222;
  cfg.Pwm.PrintRegs = true;
  cfg.Pwm.SyncPwm = true;
  cfg.Pwm.Tm2.UseSpwm = true;
  cfg.Pwm.Tm2.SpwmCarrierFrequency = 24000;
  cfg.Pwm.Tm2.SpwmModulationFrequency = 60;
  cfg.Pwm.Tm2.ModulationScheme = 3;
  cfg.Pwm.Tm2.ModulationIndexMilli = 1155;
  cfg.Pwm.Tm2.ModulationCells = 4;
  cfg.Pwm.Tm2.CarrierDisposition = 2;
  cfg.Pwm.Tm2.DeadTimeCompensation = true;
  cfg.Pwm.Tm2.SoftStartMs = 750;
  cfg.Pwm.Tm2.ReferenceWaveform = 2;
  cfg.Pwm.Tm2.DpwmVariant = 2;
  cfg.Pwm.Tm2.DpwmClampAngleDeg = -30;
  cfg.Pwm.Tm2.CarrierDitherMode = 1;
  cfg.Pwm.Tm2.CarrierDitherPercent = 15;
  cfg.Pwm.Tm2.NearestLevelModulation = true;
  cfg.Feedback.Enabled = true;
  cfg.Feedback.AnalogPin = 40;
  cfg.Feedback.SetpointMillivolts = 2400;
  cfg.Feedback.FullScaleMillivolts = 3000;
  cfg.Feedback.KpMilli = 333;
  cfg.Feedback.KiMilli = 4444;
  cfg.Feedback.LoopHz = 500;
  cfg.FaultProtection.Enabled = true;
  cfg.FaultProtection.Pin = 31;
  cfg.FaultProtection.ActiveHigh = false;
  cfg.CurrentLimit.Enabled = true;
  cfg.CurrentLimit.Pin = 23;
  cfg.CurrentLimit.ThresholdMillivolts = 1800;
  cfg.CurrentLimit.CycleByCycle = true;
  cfg.CurrentLimit.FilterCount = 7;
  cfg.CurrentLimit.FilterPeriod = 99;
  copyConfigString(cfg.Influx.Host, sizeof(cfg.Influx.Host), "influx.example.lan");
  cfg.Influx.Port = 9999;
  copyConfigString(cfg.Influx.Org, sizeof(cfg.Influx.Org), "myorg");
  copyConfigString(cfg.Influx.Bucket, sizeof(cfg.Influx.Bucket), "test_bucket");
  copyConfigString(cfg.Influx.Token, sizeof(cfg.Influx.Token), "secret-token-value==");
  cfg.Influx.IntervalSeconds = 30;
  cfg.Meter.Enabled = true;
  cfg.Meter.CurrentPin = 39;
  cfg.Meter.VoltageZeroMillivolts = 1600;
  cfg.Meter.CurrentZeroMillivolts = 1700;
  cfg.Meter.CurrentMilliampPerVolt = 5000;
  cfg.Meter.VoltageRatioMilli = 15000;
  cfg.Pwm.Tm1.Sm13.DeadTime = 11;
  cfg.Pwm.Tm1.Sm13.PwmFrequency = 1111;
  cfg.Pwm.Tm1.Sm13.ChannelA.DutyCycle = 1001;
  cfg.Pwm.Tm1.Sm13.ChannelB.DutyCycle = 1002;
  cfg.Pwm.Tm2.Sm20.DeadTime = 20;
  cfg.Pwm.Tm2.Sm20.PwmFrequency = 2020;
  cfg.Pwm.Tm2.Sm20.ChannelA.DutyCycle = 2001;
  cfg.Pwm.Tm2.Sm21.DeadTime = 21;
  cfg.Pwm.Tm2.Sm21.PwmFrequency = 2121;
  cfg.Pwm.Tm2.Sm21.ChannelA.DutyCycle = 2101;
  cfg.Pwm.Tm2.Sm22.DeadTime = 22;
  cfg.Pwm.Tm2.Sm22.ChannelB.DutyCycle = 2202;
  cfg.Pwm.Tm2.Sm23.PwmFrequency = 2323;
  cfg.Pwm.Tm2.Sm23.ChannelB.DutyCycle = 2302;
  cfg.Pwm.Tm3.Sm31.DeadTime = 31;
  cfg.Pwm.Tm3.Sm31.PwmFrequency = 3131;
  cfg.Pwm.Tm3.Sm31.ChannelA.DutyCycle = 3101;
  cfg.Pwm.Tm4.Sm40.DeadTime = 40;
  cfg.Pwm.Tm4.Sm40.PwmFrequency = 4040;
  cfg.Pwm.Tm4.Sm40.ChannelA.DutyCycle = 4001;
  cfg.Pwm.Tm4.Sm41.PwmFrequency = 4141;
  cfg.Pwm.Tm4.Sm42.DeadTime = 42;
  cfg.Pwm.Tm4.Sm42.PwmFrequency = 4242;
  cfg.Pwm.Tm4.Sm42.ChannelA.DutyCycle = 4201;
  cfg.Pwm.Tm4.Sm42.ChannelB.DutyCycle = 4202;

  // to JSON -> from JSON -> to JSON again: both serializations must be identical
  JsonDocument doc;
  configToJson(cfg, doc);
  MainConfig restored;
  configFromJson(doc, restored);

  TEST_ASSERT_EQUAL_STRING(toJsonString(cfg).c_str(), toJsonString(restored).c_str());

  // And a few direct spot checks
  TEST_ASSERT_EQUAL_STRING("secret-token-value==", restored.Influx.Token);
  TEST_ASSERT_EQUAL_STRING("influx.example.lan", restored.Influx.Host);
  TEST_ASSERT_EQUAL_UINT16(9999, restored.Influx.Port);
  TEST_ASSERT_EQUAL_INT32(-111, restored.AsymmetricInduction.PreShiftNanos);
  TEST_ASSERT_EQUAL_UINT32(4242, restored.Pwm.Tm4.Sm42.PwmFrequency);
  TEST_ASSERT_TRUE(restored.Pwm.Tm2.UseSpwm);
}

void test_partial_document_keeps_defaults_elsewhere() {
  JsonDocument doc;
  deserializeJson(doc, R"({"Config":{"Pwm":{"Tm2":{"Sm20":{"PwmFrequency":12345}}}}})");
  MainConfig cfg;
  configFromJson(doc, cfg);

  TEST_ASSERT_EQUAL_UINT32(12345, cfg.Pwm.Tm2.Sm20.PwmFrequency);
  TEST_ASSERT_EQUAL_UINT16(50, cfg.Pwm.Tm2.Sm20.DeadTime);       // default
  TEST_ASSERT_EQUAL_UINT32(1000, cfg.Pwm.Tm1.Sm13.PwmFrequency); // default
}

void test_redact_secrets_blanks_only_the_secrets() {
  MainConfig cfg;
  copyConfigString(cfg.Influx.Token, sizeof(cfg.Influx.Token), "super-secret==");
  copyConfigString(cfg.Security.WritePin, sizeof(cfg.Security.WritePin), "1234");
  JsonDocument doc;
  configToJson(cfg, doc);
  redactSecrets(doc);

  TEST_ASSERT_EQUAL_STRING("", doc["Config"]["Influx"]["Token"] | "x");
  TEST_ASSERT_EQUAL_STRING("", doc["Config"]["Security"]["WritePin"] | "x");
  TEST_ASSERT_EQUAL_STRING("ub-1.lan", doc["Config"]["Influx"]["Host"] | ""); // untouched
}

void test_preserve_secrets_keeps_stored_values_on_empty_post() {
  MainConfig previous;
  copyConfigString(previous.Influx.Token, sizeof(previous.Influx.Token), "stored-token");
  copyConfigString(previous.Security.WritePin, sizeof(previous.Security.WritePin), "9876");

  // A redacted round-trip (empty secrets) must not wipe the stored values
  MainConfig incoming;
  preserveSecrets(incoming, previous);
  TEST_ASSERT_EQUAL_STRING("stored-token", incoming.Influx.Token);
  TEST_ASSERT_EQUAL_STRING("9876", incoming.Security.WritePin);

  // But an explicit new value wins
  MainConfig updated;
  copyConfigString(updated.Influx.Token, sizeof(updated.Influx.Token), "new-token");
  copyConfigString(updated.Security.WritePin, sizeof(updated.Security.WritePin), "0000");
  preserveSecrets(updated, previous);
  TEST_ASSERT_EQUAL_STRING("new-token", updated.Influx.Token);
  TEST_ASSERT_EQUAL_STRING("0000", updated.Security.WritePin);
}

void test_validate_clamps_out_of_range_frequency() {
  MainConfig cfg;
  cfg.Pwm.Tm1.Sm13.PwmFrequency = 0;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT32(1000, cfg.Pwm.Tm1.Sm13.PwmFrequency);

  cfg.Pwm.Tm1.Sm13.PwmFrequency = 2000000;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT32(1000, cfg.Pwm.Tm1.Sm13.PwmFrequency);

  cfg.Pwm.Tm1.Sm13.PwmFrequency = 500000;
  TEST_ASSERT_FALSE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT32(500000, cfg.Pwm.Tm1.Sm13.PwmFrequency);
}

void test_validate_current_limit() {
  MainConfig cfg;
  cfg.CurrentLimit.Pin = 13; // not comparator-capable
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT8(40, cfg.CurrentLimit.Pin);

  cfg.CurrentLimit.ThresholdMillivolts = 50; // below the DAC's useful floor
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT16(2475, cfg.CurrentLimit.ThresholdMillivolts);

  cfg.CurrentLimit.ThresholdMillivolts = 4000;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT16(2475, cfg.CurrentLimit.ThresholdMillivolts);

  cfg.CurrentLimit.FilterCount = 9;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT8(0, cfg.CurrentLimit.FilterCount);

  cfg.CurrentLimit.Pin = 18; // valid alternative route (CMP1 channel 0)
  cfg.CurrentLimit.ThresholdMillivolts = 1650;
  TEST_ASSERT_FALSE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT8(18, cfg.CurrentLimit.Pin);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_from_empty_document);
  RUN_TEST(test_roundtrip_preserves_every_field);
  RUN_TEST(test_partial_document_keeps_defaults_elsewhere);
  RUN_TEST(test_redact_secrets_blanks_only_the_secrets);
  RUN_TEST(test_preserve_secrets_keeps_stored_values_on_empty_post);
  RUN_TEST(test_validate_clamps_out_of_range_frequency);
  RUN_TEST(test_validate_current_limit);
  return UNITY_END();
}
