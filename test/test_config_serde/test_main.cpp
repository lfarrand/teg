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
  TEST_ASSERT_EQUAL_UINT16(50, cfg.Pwm.Tm1.Sm13.DeadTime);
  TEST_ASSERT_EQUAL_UINT32(1000, cfg.Pwm.Tm1.Sm13.PwmFrequency);
  TEST_ASSERT_EQUAL_UINT16(32768, cfg.Pwm.Tm1.Sm13.ChannelA.DutyCycle);
  TEST_ASSERT_TRUE(cfg.Pwm.Tm1.Sm13.ChannelA.Enabled);
  TEST_ASSERT_EQUAL_UINT32(1000, cfg.Pwm.Tm1.Sm13.ChannelA.OnPeriodMicroseconds);
  TEST_ASSERT_EQUAL_UINT16(50, cfg.Pwm.Tm4.Sm42.DeadTime);
  TEST_ASSERT_EQUAL_UINT16(32768, cfg.Pwm.Tm4.Sm42.ChannelB.DutyCycle);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.Pwm.Tm3.Sm31.ChannelA.PhaseShift);
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
  cfg.Pwm.Tm1.Sm13.DeadTime = 11;
  cfg.Pwm.Tm1.Sm13.PwmFrequency = 1111;
  cfg.Pwm.Tm1.Sm13.ChannelA.OnPeriodMicroseconds = 101;
  cfg.Pwm.Tm1.Sm13.ChannelA.DutyCycle = 1001;
  cfg.Pwm.Tm1.Sm13.ChannelA.PhaseShift = 1;
  cfg.Pwm.Tm1.Sm13.ChannelA.Enabled = false;
  cfg.Pwm.Tm1.Sm13.ChannelB.OnPeriodMicroseconds = 102;
  cfg.Pwm.Tm1.Sm13.ChannelB.DutyCycle = 1002;
  cfg.Pwm.Tm1.Sm13.ChannelB.PhaseShift = 2;
  cfg.Pwm.Tm1.Sm13.ChannelB.Enabled = false;
  cfg.Pwm.Tm2.Sm20.DeadTime = 20;
  cfg.Pwm.Tm2.Sm20.PwmFrequency = 2020;
  cfg.Pwm.Tm2.Sm20.ChannelA.DutyCycle = 2001;
  cfg.Pwm.Tm2.Sm20.ChannelB.PhaseShift = 3;
  cfg.Pwm.Tm2.Sm21.DeadTime = 21;
  cfg.Pwm.Tm2.Sm21.PwmFrequency = 2121;
  cfg.Pwm.Tm2.Sm21.ChannelA.DutyCycle = 2101;
  cfg.Pwm.Tm2.Sm22.DeadTime = 22;
  cfg.Pwm.Tm2.Sm22.ChannelA.PhaseShift = 4;
  cfg.Pwm.Tm2.Sm22.ChannelB.DutyCycle = 2202;
  cfg.Pwm.Tm2.Sm23.PwmFrequency = 2323;
  cfg.Pwm.Tm2.Sm23.ChannelB.DutyCycle = 2302;
  cfg.Pwm.Tm3.Sm31.DeadTime = 31;
  cfg.Pwm.Tm3.Sm31.PwmFrequency = 3131;
  cfg.Pwm.Tm3.Sm31.ChannelA.DutyCycle = 3101;
  cfg.Pwm.Tm3.Sm31.ChannelB.PhaseShift = 5;
  cfg.Pwm.Tm4.Sm40.DeadTime = 40;
  cfg.Pwm.Tm4.Sm40.PwmFrequency = 4040;
  cfg.Pwm.Tm4.Sm40.ChannelA.DutyCycle = 4001;
  cfg.Pwm.Tm4.Sm41.PwmFrequency = 4141;
  cfg.Pwm.Tm4.Sm41.ChannelA.PhaseShift = 6;
  cfg.Pwm.Tm4.Sm42.DeadTime = 42;
  cfg.Pwm.Tm4.Sm42.PwmFrequency = 4242;
  cfg.Pwm.Tm4.Sm42.ChannelA.DutyCycle = 4201;
  cfg.Pwm.Tm4.Sm42.ChannelB.DutyCycle = 4202;
  cfg.Pwm.Tm4.Sm42.ChannelB.PhaseShift = 7;

  // to JSON -> from JSON -> to JSON again: both serializations must be identical
  JsonDocument doc;
  configToJson(cfg, doc);
  MainConfig restored;
  configFromJson(doc, restored);

  TEST_ASSERT_EQUAL_STRING(toJsonString(cfg).c_str(), toJsonString(restored).c_str());

  // And a few direct spot checks
  TEST_ASSERT_EQUAL_INT32(-111, restored.AsymmetricInduction.PreShiftNanos);
  TEST_ASSERT_EQUAL_UINT32(4242, restored.Pwm.Tm4.Sm42.PwmFrequency);
  TEST_ASSERT_FALSE(restored.Pwm.Tm1.Sm13.ChannelB.Enabled);
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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_from_empty_document);
  RUN_TEST(test_roundtrip_preserves_every_field);
  RUN_TEST(test_partial_document_keeps_defaults_elsewhere);
  RUN_TEST(test_validate_clamps_out_of_range_frequency);
  return UNITY_END();
}
