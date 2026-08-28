// Tests for the MainConfig <-> JSON mapping (config_serde.h)

#include <unity.h>
#include <string>
#include <config_compare.h>
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
  TEST_ASSERT_EQUAL_UINT16(250, cfg.Feedback.LoopHz);
  TEST_ASSERT_FALSE(cfg.FaultProtection.Enabled);
  TEST_ASSERT_EQUAL_UINT8(32, cfg.FaultProtection.Pin);
  TEST_ASSERT_TRUE(cfg.FaultProtection.ActiveHigh);
  TEST_ASSERT_FALSE(cfg.CurrentLimit.Enabled);
  TEST_ASSERT_EQUAL_UINT8(40, cfg.CurrentLimit.Pin);
  TEST_ASSERT_EQUAL_UINT16(2475, cfg.CurrentLimit.ThresholdMillivolts);
  TEST_ASSERT_FALSE(cfg.CurrentLimit.CycleByCycle);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.CurrentLimit.FilterCount);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.CurrentLimit.FilterPeriod);
  TEST_ASSERT_FALSE(cfg.Mtp.Enabled); // USB file access is opt-in
  TEST_ASSERT_FALSE(cfg.Mqtt.Enabled);
  TEST_ASSERT_EQUAL_STRING("", cfg.Mqtt.Host);
  TEST_ASSERT_EQUAL_UINT16(1883, cfg.Mqtt.Port);
  TEST_ASSERT_EQUAL_STRING("teg", cfg.Mqtt.BaseTopic);
  TEST_ASSERT_EQUAL_STRING("homeassistant", cfg.Mqtt.DiscoveryPrefix);
  TEST_ASSERT_TRUE(cfg.Mqtt.DiscoveryEnabled);
  TEST_ASSERT_EQUAL_UINT16(10, cfg.Mqtt.IntervalSeconds);
  TEST_ASSERT_FALSE(cfg.Mppt.Enabled);
  TEST_ASSERT_EQUAL_UINT16(3000, cfg.Mppt.IntervalMs);
  TEST_ASSERT_EQUAL_UINT16(20, cfg.Mppt.StepMilli);
  TEST_ASSERT_EQUAL_UINT16(5, cfg.Mppt.MinStepMilli);
  TEST_ASSERT_EQUAL_UINT16(50, cfg.Mppt.MinIndexMilli);
  TEST_ASSERT_EQUAL_UINT16(1000, cfg.Mppt.MaxIndexMilli);
  TEST_ASSERT_EQUAL_UINT16(10, cfg.Mppt.DeadbandMw);
  TEST_ASSERT_EQUAL_UINT32(1000, cfg.Mppt.RestartDeltaMw);
  TEST_ASSERT_FALSE(cfg.Pll.Enabled);
  TEST_ASSERT_EQUAL_INT16(0, cfg.Pll.PhaseOffsetCentiDeg);
  TEST_ASSERT_EQUAL_UINT16(45, cfg.Pll.MinHz);
  TEST_ASSERT_EQUAL_UINT16(55, cfg.Pll.MaxHz);
  TEST_ASSERT_EQUAL_UINT16(200, cfg.Pll.BandwidthDeciHz);
  TEST_ASSERT_EQUAL_UINT16(1650, cfg.Pll.ZeroMillivolts);
  TEST_ASSERT_EQUAL_UINT16(100, cfg.Pll.MinLevelMillivolts);
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
  TEST_ASSERT_EQUAL_UINT16(MinHalfBridgeDeadTimeNs, cfg.Pwm.Tm1.Sm13.DeadTime);
  TEST_ASSERT_EQUAL_UINT8(PairIndependent, cfg.Pwm.Tm1.Sm13.Pair);
  TEST_ASSERT_EQUAL_UINT32(1000, cfg.Pwm.Tm1.Sm13.PwmFrequency);
  // Zero, not 32768. A missing DutyCycle key used to fall back to 50%, which on a
  // submodule wired to a half-bridge commands both switches on together for half of
  // every carrier period. The safe fallback is "output off", and it now matches the
  // compiled struct default so the no-settings-file path and the missing-key path
  // cannot disagree.
  TEST_ASSERT_EQUAL_UINT16(0, cfg.Pwm.Tm1.Sm13.ChannelA.DutyCycle);
  TEST_ASSERT_EQUAL_UINT16(MinHalfBridgeDeadTimeNs, cfg.Pwm.Tm4.Sm42.DeadTime);
  TEST_ASSERT_EQUAL_UINT8(PairIndependent, cfg.Pwm.Tm4.Sm42.Pair);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.Pwm.Tm4.Sm42.ChannelB.DutyCycle);
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
  cfg.Mtp.Enabled = true;
  cfg.Mqtt.Enabled = true;
  copyConfigString(cfg.Mqtt.Host, sizeof(cfg.Mqtt.Host), "broker.lan");
  cfg.Mqtt.Port = 8883;
  copyConfigString(cfg.Mqtt.Username, sizeof(cfg.Mqtt.Username), "teguser");
  copyConfigString(cfg.Mqtt.Password, sizeof(cfg.Mqtt.Password), "tegpass");
  copyConfigString(cfg.Mqtt.BaseTopic, sizeof(cfg.Mqtt.BaseTopic), "power/teg");
  copyConfigString(cfg.Mqtt.DiscoveryPrefix, sizeof(cfg.Mqtt.DiscoveryPrefix), "ha");
  cfg.Mqtt.DiscoveryEnabled = false;
  cfg.Mqtt.IntervalSeconds = 30;
  cfg.Mppt.Enabled = true;
  cfg.Mppt.IntervalMs = 5000;
  cfg.Mppt.StepMilli = 30;
  cfg.Mppt.MinStepMilli = 3;
  cfg.Mppt.MinIndexMilli = 100;
  cfg.Mppt.MaxIndexMilli = 900;
  cfg.Mppt.DeadbandMw = 25;
  cfg.Mppt.RestartDeltaMw = 2000;
  cfg.Pll.Enabled = true;
  cfg.Pll.PhaseOffsetCentiDeg = -4500;
  cfg.Pll.MinHz = 55;
  cfg.Pll.MaxHz = 65;
  cfg.Pll.BandwidthDeciHz = 150;
  cfg.Pll.ZeroMillivolts = 1600;
  cfg.Pll.MinLevelMillivolts = 250;
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
  TEST_ASSERT_EQUAL_UINT16(MinHalfBridgeDeadTimeNs, cfg.Pwm.Tm2.Sm20.DeadTime); // default
  TEST_ASSERT_EQUAL_UINT32(1000, cfg.Pwm.Tm1.Sm13.PwmFrequency); // default
}

void test_redact_secrets_blanks_only_the_secrets() {
  MainConfig cfg;
  copyConfigString(cfg.Influx.Token, sizeof(cfg.Influx.Token), "super-secret==");
  copyConfigString(cfg.Security.WritePin, sizeof(cfg.Security.WritePin), "1234");
  copyConfigString(cfg.Mqtt.Password, sizeof(cfg.Mqtt.Password), "mqtt-pass");
  copyConfigString(cfg.Mqtt.Username, sizeof(cfg.Mqtt.Username), "mqtt-user");
  JsonDocument doc;
  configToJson(cfg, doc);
  redactSecrets(doc);

  TEST_ASSERT_EQUAL_STRING("", doc["Config"]["Influx"]["Token"] | "x");
  TEST_ASSERT_EQUAL_STRING("", doc["Config"]["Security"]["WritePin"] | "x");
  TEST_ASSERT_EQUAL_STRING("", doc["Config"]["Mqtt"]["Password"] | "x");
  TEST_ASSERT_EQUAL_STRING("ub-1.lan", doc["Config"]["Influx"]["Host"] | ""); // untouched
  TEST_ASSERT_EQUAL_STRING("mqtt-user", doc["Config"]["Mqtt"]["Username"] | ""); // not a secret
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

  // Same contract for the MQTT password
  MainConfig prevMqtt;
  copyConfigString(prevMqtt.Mqtt.Password, sizeof(prevMqtt.Mqtt.Password), "stored-pass");
  MainConfig inMqtt;
  preserveSecrets(inMqtt, prevMqtt);
  TEST_ASSERT_EQUAL_STRING("stored-pass", inMqtt.Mqtt.Password);
}

void test_restore_secrets_is_unconditional() {
  // preserveSecrets keeps stored values only when the incoming field is
  // blank - right for the UI round-trip, wrong for a file the operator did
  // not author. When the endpoint identity is unchanged, restoreSecrets
  // must overwrite even a populated credential so a file can never SET a
  // secret. A changed endpoint must not receive the device secret.
  MainConfig previous;
  copyConfigString(previous.Mqtt.Host, sizeof(previous.Mqtt.Host), "broker.lan");
  previous.Mqtt.Port = 1883;
  copyConfigString(previous.Mqtt.Username, sizeof(previous.Mqtt.Username), "teguser");
  copyConfigString(previous.Mqtt.Password, sizeof(previous.Mqtt.Password), "device-pass");
  copyConfigString(previous.Influx.Host, sizeof(previous.Influx.Host), "influx.lan");
  previous.Influx.Port = 8086;
  copyConfigString(previous.Influx.Org, sizeof(previous.Influx.Org), "device-org");
  copyConfigString(previous.Influx.Bucket, sizeof(previous.Influx.Bucket), "device-bucket");
  copyConfigString(previous.Influx.Token, sizeof(previous.Influx.Token), "device-token");
  copyConfigString(previous.Security.WritePin, sizeof(previous.Security.WritePin), "1234");

  MainConfig incoming;
  copyConfigString(incoming.Mqtt.Host, sizeof(incoming.Mqtt.Host), "broker.lan");
  incoming.Mqtt.Port = 1883;
  copyConfigString(incoming.Mqtt.Username, sizeof(incoming.Mqtt.Username), "teguser");
  copyConfigString(incoming.Mqtt.Password, sizeof(incoming.Mqtt.Password), "attacker-pass");
  copyConfigString(incoming.Influx.Host, sizeof(incoming.Influx.Host), "influx.lan");
  incoming.Influx.Port = 8086;
  copyConfigString(incoming.Influx.Org, sizeof(incoming.Influx.Org), "device-org");
  copyConfigString(incoming.Influx.Bucket, sizeof(incoming.Influx.Bucket), "device-bucket");
  copyConfigString(incoming.Influx.Token, sizeof(incoming.Influx.Token), "attacker-token");
  copyConfigString(incoming.Security.WritePin, sizeof(incoming.Security.WritePin), "9999");

  restoreSecrets(incoming, previous);
  TEST_ASSERT_EQUAL_STRING("device-token", incoming.Influx.Token);
  TEST_ASSERT_EQUAL_STRING("1234", incoming.Security.WritePin);
  TEST_ASSERT_EQUAL_STRING("device-pass", incoming.Mqtt.Password);
}

void test_restore_secrets_clears_mqtt_on_host_change() {
  MainConfig previous;
  previous.Mqtt.Enabled = true;
  copyConfigString(previous.Mqtt.Host, sizeof(previous.Mqtt.Host), "broker.lan");
  previous.Mqtt.Port = 1883;
  copyConfigString(previous.Mqtt.Username, sizeof(previous.Mqtt.Username), "teguser");
  copyConfigString(previous.Mqtt.Password, sizeof(previous.Mqtt.Password), "device-pass");
  copyConfigString(previous.Security.WritePin, sizeof(previous.Security.WritePin), "1234");

  MainConfig incoming;
  incoming.Mqtt.Enabled = true;
  copyConfigString(incoming.Mqtt.Host, sizeof(incoming.Mqtt.Host), "other.lan");
  incoming.Mqtt.Port = 1883;
  copyConfigString(incoming.Mqtt.Username, sizeof(incoming.Mqtt.Username), "teguser");
  copyConfigString(incoming.Mqtt.Password, sizeof(incoming.Mqtt.Password), "attacker-pass");
  copyConfigString(incoming.Security.WritePin, sizeof(incoming.Security.WritePin), "9999");

  restoreSecrets(incoming, previous);
  TEST_ASSERT_EQUAL_STRING("", incoming.Mqtt.Password);
  TEST_ASSERT_FALSE(incoming.Mqtt.Enabled);
  TEST_ASSERT_EQUAL_STRING("1234", incoming.Security.WritePin);
}

void test_config_api_reject_reason_current_limit_threshold() {
  MainConfig cfg;
  cfg.CurrentLimit.ThresholdMillivolts = 50;
  TEST_ASSERT_NOT_NULL(configApiRejectReason(cfg));
  TEST_ASSERT_EQUAL_UINT16(50, cfg.CurrentLimit.ThresholdMillivolts);

  cfg.CurrentLimit.ThresholdMillivolts = 4000;
  TEST_ASSERT_NOT_NULL(configApiRejectReason(cfg));
  TEST_ASSERT_EQUAL_UINT16(4000, cfg.CurrentLimit.ThresholdMillivolts);

  cfg.CurrentLimit.ThresholdMillivolts = 1650;
  TEST_ASSERT_NULL(configApiRejectReason(cfg));
  TEST_ASSERT_EQUAL_UINT16(1650, cfg.CurrentLimit.ThresholdMillivolts);
}

void test_doc_completeness_gate() {
  // Absent sections default to compiled values, which would disarm fault
  // protection, the current limit and thermal derating
  MainConfig cfg;
  JsonDocument full;
  configToJson(cfg, full);
  TEST_ASSERT_TRUE(configDocComplete(full));

  JsonDocument empty;
  TEST_ASSERT_FALSE(configDocComplete(empty));

  JsonDocument noConfig;
  noConfig["something"] = 1;
  TEST_ASSERT_FALSE(configDocComplete(noConfig));

  const char *sections[] = {"Pwm", "FaultProtection", "CurrentLimit", "Thermal"};
  for (unsigned i = 0; i < sizeof(sections) / sizeof(sections[0]); i++) {
    JsonDocument partial;
    configToJson(cfg, partial);
    partial["Config"].as<JsonObject>().remove(sections[i]);
    TEST_ASSERT_FALSE(configDocComplete(partial));
  }
}

void test_doc_completeness_checks_nested_keys_types_and_version() {
  MainConfig cfg;
  JsonDocument full;
  configToJson(cfg, full);
  TEST_ASSERT_TRUE(configDocComplete(full));
  TEST_ASSERT_EQUAL_UINT32(1, full["SchemaVersion"].as<uint32_t>());

  JsonDocument noNestedKey;
  configToJson(cfg, noNestedKey);
  noNestedKey["Config"]["Pwm"]["Tm2"]["Sm20"]["ChannelB"].as<JsonObject>().remove("DutyCycle");
  TEST_ASSERT_FALSE(configDocComplete(noNestedKey));

  JsonDocument wrongObjectType;
  configToJson(cfg, wrongObjectType);
  wrongObjectType["Config"]["CurrentLimit"] = 7;
  TEST_ASSERT_FALSE(configDocComplete(wrongObjectType));

  JsonDocument wrongScalarType;
  configToJson(cfg, wrongScalarType);
  wrongScalarType["Config"]["FaultProtection"]["Enabled"] = "yes";
  TEST_ASSERT_FALSE(configDocComplete(wrongScalarType));

  JsonDocument future;
  configToJson(cfg, future);
  future["SchemaVersion"] = 2;
  TEST_ASSERT_FALSE(configDocComplete(future));

  // A complete export from the immediately-pre-schema firmware remains
  // importable; the next successful save adds SchemaVersion.
  JsonDocument legacy;
  configToJson(cfg, legacy);
  legacy.remove("SchemaVersion");
  TEST_ASSERT_TRUE(configDocComplete(legacy));
}

void test_shorter_config_string_clears_the_old_tail() {
  char pin[12];
  memset(pin, 0x5A, sizeof(pin));
  copyConfigString(pin, sizeof(pin), "LONGERPIN");
  copyConfigString(pin, sizeof(pin), "NEW");
  TEST_ASSERT_EQUAL_STRING("NEW", pin);
  for (size_t i = 4; i < sizeof(pin); i++) {
    TEST_ASSERT_EQUAL_HEX8(0, static_cast<uint8_t>(pin[i]));
  }
}

void test_pair_mode_roundtrips_per_submodule() {
  // Each submodule carries its own mode, so a save/load cycle must not smear one
  // submodule's setting onto another.
  MainConfig cfg;
  cfg.Pwm.Tm2.Sm20.Pair = PairHalfBridge;
  cfg.Pwm.Tm2.Sm22.Pair = PairDifferential;
  cfg.Pwm.Tm3.Sm31.Pair = PairHalfBridge;
  cfg.Pwm.Tm2.Sm20.DeadTime = 300;

  JsonDocument doc;
  configToJson(cfg, doc);
  MainConfig restored;
  configFromJson(doc, restored);

  TEST_ASSERT_EQUAL_UINT8(PairHalfBridge, restored.Pwm.Tm2.Sm20.Pair);
  TEST_ASSERT_EQUAL_UINT8(PairDifferential, restored.Pwm.Tm2.Sm22.Pair);
  TEST_ASSERT_EQUAL_UINT8(PairHalfBridge, restored.Pwm.Tm3.Sm31.Pair);
  TEST_ASSERT_EQUAL_UINT8(PairIndependent, restored.Pwm.Tm2.Sm23.Pair);
  TEST_ASSERT_EQUAL_UINT8(PairIndependent, restored.Pwm.Tm1.Sm13.Pair);
  TEST_ASSERT_EQUAL_UINT16(300, restored.Pwm.Tm2.Sm20.DeadTime);
}

void test_absent_pair_key_defaults_to_independent() {
  // A settings file written before this field existed, or one hand-edited, must
  // land on the mode that is always valid rather than inherit whatever was in the
  // struct - the config is reused across loads.
  JsonDocument doc;
  doc["Config"]["Pwm"]["Tm2"]["Sm20"]["DeadTime"] = 400;
  MainConfig cfg;
  cfg.Pwm.Tm2.Sm20.Pair = PairHalfBridge; // stale value from a previous load
  configFromJson(doc, cfg);
  TEST_ASSERT_EQUAL_UINT8(PairIndependent, cfg.Pwm.Tm2.Sm20.Pair);
  TEST_ASSERT_EQUAL_UINT16(400, cfg.Pwm.Tm2.Sm20.DeadTime);
}

void test_validate_refuses_pairing_submodules_without_a_b_pin() {
  // Sm21/Sm40/Sm41 have no channel-B pin; pairing one would run DTCNT1 at its
  // 0x07FF reset value as the falling-edge dead time.
  MainConfig cfg;
  cfg.Pwm.Tm2.Sm21.Pair = PairHalfBridge;
  cfg.Pwm.Tm4.Sm40.Pair = PairDifferential;
  cfg.Pwm.Tm4.Sm41.Pair = PairHalfBridge;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT8(PairIndependent, cfg.Pwm.Tm2.Sm21.Pair);
  TEST_ASSERT_EQUAL_UINT8(PairIndependent, cfg.Pwm.Tm4.Sm40.Pair);
  TEST_ASSERT_EQUAL_UINT8(PairIndependent, cfg.Pwm.Tm4.Sm41.Pair);

  // Sm42 has a B pin but drives it from independent start/stop values.
  MainConfig sm42;
  sm42.Pwm.Tm4.Sm42.Pair = PairHalfBridge;
  TEST_ASSERT_TRUE(validateConfig(sm42));
  TEST_ASSERT_EQUAL_UINT8(PairIndependent, sm42.Pwm.Tm4.Sm42.Pair);

  // The submodules that CAN be a leg are left alone.
  MainConfig legs;
  legs.Pwm.Tm2.Sm20.Pair = PairHalfBridge;
  legs.Pwm.Tm2.Sm22.Pair = PairDifferential;
  validateConfig(legs);
  TEST_ASSERT_EQUAL_UINT8(PairHalfBridge, legs.Pwm.Tm2.Sm20.Pair);
  TEST_ASSERT_EQUAL_UINT8(PairDifferential, legs.Pwm.Tm2.Sm22.Pair);
}

void test_validate_clamps_dead_time_to_a_representable_value() {
  // Above ~13.6us DTCNT wraps, programming a SHORTER gap than requested.
  MainConfig cfg;
  cfg.Pwm.Tm2.Sm20.DeadTime = 65535;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT16(pairMaxDeadTimeNs(), cfg.Pwm.Tm2.Sm20.DeadTime);
  TEST_ASSERT_TRUE(pairDeadTimeCycles(cfg.Pwm.Tm2.Sm20.DeadTime) <= MaxDeadTimeCycles);

  // A representable value is untouched.
  cfg.Pwm.Tm2.Sm20.DeadTime = 500;
  validateConfig(cfg);
  TEST_ASSERT_EQUAL_UINT16(500, cfg.Pwm.Tm2.Sm20.DeadTime);
}

void test_validate_is_idempotent_over_pair_modes() {
  // Running validation twice must not move anything again, or a config could drift
  // every time it is saved.
  for (uint8_t mode = 0; mode < PairModeCount + 2; mode++) {
    MainConfig cfg;
    cfg.Pwm.Tm2.Sm20.Pair = mode;
    cfg.Pwm.Tm2.Sm21.Pair = mode;
    cfg.Pwm.Tm4.Sm42.Pair = mode;
    validateConfig(cfg);
    const uint8_t a = cfg.Pwm.Tm2.Sm20.Pair, b = cfg.Pwm.Tm2.Sm21.Pair, c = cfg.Pwm.Tm4.Sm42.Pair;
    TEST_ASSERT_FALSE(validateConfig(cfg)); // nothing left to correct
    TEST_ASSERT_EQUAL_UINT8(a, cfg.Pwm.Tm2.Sm20.Pair);
    TEST_ASSERT_EQUAL_UINT8(b, cfg.Pwm.Tm2.Sm21.Pair);
    TEST_ASSERT_EQUAL_UINT8(c, cfg.Pwm.Tm4.Sm42.Pair);
  }
}

void test_pair_change_is_visible_to_the_reconfigure_gates() {
  // applyPwmConfig() only reconfigures a timer when its semantic comparison
  // differs. A previous attempt put the pair setting at Pwm. level, outside all
  // four gates, so changing it reconfigured nothing while the UI reported
  // success. Exercise the production comparator so moving it out again fails
  // here rather than silently going inert on hardware.
  MainConfig before, after;
  after.Pwm.Tm2.Sm20.Pair = PairHalfBridge;
  TEST_ASSERT_FALSE(configValuesEqual(before.Pwm, after.Pwm));

  MainConfig b1, a1;
  a1.Pwm.Tm1.Sm13.Pair = PairDifferential;
  TEST_ASSERT_FALSE(configValuesEqual(b1.Pwm, a1.Pwm));

  MainConfig b3, a3;
  a3.Pwm.Tm3.Sm31.Pair = PairHalfBridge;
  TEST_ASSERT_FALSE(configValuesEqual(b3.Pwm, a3.Pwm));

  MainConfig b4, a4;
  a4.Pwm.Tm4.Sm42.Pair = PairDifferential;
  TEST_ASSERT_FALSE(configValuesEqual(b4.Pwm, a4.Pwm));

  // Independently constructed defaults must compare equal even when their
  // indeterminate padding bytes do not.
  MainConfig same1, same2;
  TEST_ASSERT_TRUE(configValuesEqual(same1.Pwm, same2.Pwm));
}

void test_reconfigure_gates_compare_values_not_object_bytes() {
  JsonDocument firstDoc;
  JsonDocument secondDoc;
  MainConfig first;
  MainConfig second;
  configFromJson(firstDoc, first);
  configFromJson(secondDoc, second);

  TEST_ASSERT_TRUE(configValuesEqual(first.Pwm, second.Pwm));
  TEST_ASSERT_TRUE(configValuesEqual(first.AsymmetricInduction, second.AsymmetricInduction));
  TEST_ASSERT_TRUE(configValuesEqual(first.CurrentLimit, second.CurrentLimit));
  TEST_ASSERT_TRUE(configValuesEqual(first.FaultProtection, second.FaultProtection));
  TEST_ASSERT_TRUE(configValuesEqual(first.Mppt, second.Mppt));
  TEST_ASSERT_TRUE(configValuesEqual(first.Feedback, second.Feedback));
  TEST_ASSERT_TRUE(configValuesEqual(first.Mqtt, second.Mqtt));
  TEST_ASSERT_TRUE(configValuesEqual(first.PowerMon, second.PowerMon));
  TEST_ASSERT_TRUE(configValuesEqual(first.Pll, second.Pll));

  // Bytes after a string terminator are not part of the configuration value.
  first.Mqtt.Host[sizeof(first.Mqtt.Host) - 1] = 'x';
  TEST_ASSERT_TRUE(configValuesEqual(first.Mqtt, second.Mqtt));

  MainConfig changed = second;
  changed.AsymmetricInduction.PreShiftNanos++;
  TEST_ASSERT_FALSE(configValuesEqual(first.AsymmetricInduction, changed.AsymmetricInduction));
  changed = second;
  changed.CurrentLimit.FilterPeriod++;
  TEST_ASSERT_FALSE(configValuesEqual(first.CurrentLimit, changed.CurrentLimit));
  changed = second;
  changed.FaultProtection.ActiveHigh = !changed.FaultProtection.ActiveHigh;
  TEST_ASSERT_FALSE(configValuesEqual(first.FaultProtection, changed.FaultProtection));
  changed = second;
  changed.Mppt.RestartDeltaMw++;
  TEST_ASSERT_FALSE(configValuesEqual(first.Mppt, changed.Mppt));
  changed = second;
  changed.Feedback.LoopHz++;
  TEST_ASSERT_FALSE(configValuesEqual(first.Feedback, changed.Feedback));
  changed = second;
  copyConfigString(changed.Mqtt.Host, sizeof(changed.Mqtt.Host), "broker.lan");
  TEST_ASSERT_FALSE(configValuesEqual(first.Mqtt, changed.Mqtt));
  changed = second;
  changed.PowerMon.ImonRimonOhm++;
  TEST_ASSERT_FALSE(configValuesEqual(first.PowerMon, changed.PowerMon));
  changed = second;
  changed.Pll.BandwidthDeciHz++;
  TEST_ASSERT_FALSE(configValuesEqual(first.Pll, changed.Pll));
}

void test_validate_preserves_pair_intent_under_an_inverting_scheme() {
  // validateConfig must NOT rewrite Pair because of the modulation scheme. The scheme
  // changes; the wiring does not. Rewriting it would permanently discard the fact
  // that the operator built a half-bridge there, and the config would no longer hold
  // the information needed to refuse to drive that leg later.
  //
  // The gate itself lives in configureModule2() and is tested as a pure function in
  // test_pwm_pair (pairModeSanitisedForScheme).
  MainConfig cfg;
  cfg.Pwm.Tm2.UseSpwm = true;
  cfg.Pwm.Tm2.ModulationScheme = ModSchemeSpwmBipolar; // needs inversion
  cfg.Pwm.Tm2.ModulationCells = 2;
  cfg.Pwm.Tm2.Sm20.Pair = PairHalfBridge;
  cfg.Pwm.Tm2.Sm22.Pair = PairHalfBridge;
  validateConfig(cfg);
  TEST_ASSERT_EQUAL_UINT8(PairHalfBridge, cfg.Pwm.Tm2.Sm20.Pair);
  TEST_ASSERT_EQUAL_UINT8(PairHalfBridge, cfg.Pwm.Tm2.Sm22.Pair);

  // Same under a non-inverting scheme, for the obvious reason.
  MainConfig ok;
  ok.Pwm.Tm2.UseSpwm = true;
  ok.Pwm.Tm2.ModulationScheme = ModSchemeSpwmUnipolar;
  ok.Pwm.Tm2.ModulationCells = 2;
  ok.Pwm.Tm2.Sm20.Pair = PairHalfBridge;
  ok.Pwm.Tm2.Sm22.Pair = PairDifferential;
  validateConfig(ok);
  TEST_ASSERT_EQUAL_UINT8(PairHalfBridge, ok.Pwm.Tm2.Sm20.Pair);
  TEST_ASSERT_EQUAL_UINT8(PairDifferential, ok.Pwm.Tm2.Sm22.Pair);
}

void test_validate_still_enforces_the_permanent_hardware_facts() {
  // The facts that never change are still corrected and persisted: no channel-B pin,
  // and Sm42's independent start/stop timing.
  MainConfig cfg;
  cfg.Pwm.Tm2.UseSpwm = true;
  cfg.Pwm.Tm2.ModulationScheme = ModSchemeSpwmUnipolar;
  cfg.Pwm.Tm2.Sm21.Pair = PairHalfBridge;
  cfg.Pwm.Tm4.Sm40.Pair = PairDifferential;
  cfg.Pwm.Tm4.Sm42.Pair = PairHalfBridge;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT8(PairIndependent, cfg.Pwm.Tm2.Sm21.Pair);
  TEST_ASSERT_EQUAL_UINT8(PairIndependent, cfg.Pwm.Tm4.Sm40.Pair);
  TEST_ASSERT_EQUAL_UINT8(PairIndependent, cfg.Pwm.Tm4.Sm42.Pair);
}

void test_validate_leaves_non_cell_submodules_ungated_by_scheme() {
  // Sm13 and Sm31 are standalone PWM, not modulation cells, so an inverting scheme
  // on Tm2 must not disturb their pair mode.
  MainConfig cfg;
  cfg.Pwm.Tm2.UseSpwm = true;
  cfg.Pwm.Tm2.ModulationScheme = ModSchemeSpwmBipolar;
  cfg.Pwm.Tm2.ModulationCells = 2;
  cfg.Pwm.Tm1.Sm13.Pair = PairHalfBridge;
  cfg.Pwm.Tm3.Sm31.Pair = PairDifferential;
  validateConfig(cfg);
  TEST_ASSERT_EQUAL_UINT8(PairHalfBridge, cfg.Pwm.Tm1.Sm13.Pair);
  TEST_ASSERT_EQUAL_UINT8(PairDifferential, cfg.Pwm.Tm3.Sm31.Pair);
}

void test_validate_gate_is_inactive_when_spwm_is_off() {
  // With UseSpwm off the cell plans are never applied and polarity stays HighTrue,
  // so a pair is safe even though the configured scheme would invert.
  MainConfig cfg;
  cfg.Pwm.Tm2.UseSpwm = false;
  cfg.Pwm.Tm2.ModulationScheme = ModSchemeSpwmBipolar;
  cfg.Pwm.Tm2.ModulationCells = 2;
  cfg.Pwm.Tm2.Sm20.Pair = PairHalfBridge;
  validateConfig(cfg);
  TEST_ASSERT_EQUAL_UINT8(PairHalfBridge, cfg.Pwm.Tm2.Sm20.Pair);
}

void test_compiled_defaults_are_safe_with_no_settings_file() {
  // This is the exact state a board reaches with no SD card: loadConfiguration()
  // returns early and nothing overwrites the struct defaults. Every one of these was
  // wrong at some point, and each wrong value reached the power stage.
  MainConfig cfg;

  // Special modulation and every optional software/hardware actuator are off.
  TEST_ASSERT_FALSE(cfg.AsymmetricInduction.IsEnabled);
  TEST_ASSERT_FALSE(cfg.Pwm.Tm2.UseSpwm);
  TEST_ASSERT_FALSE(cfg.FaultProtection.Enabled);
  TEST_ASSERT_FALSE(cfg.CurrentLimit.Enabled);

  // Zero frequency divided by zero in computeAsymmetricTimings, produced garbage edge
  // timings, and the outputs were enabled anyway.
  TEST_ASSERT_TRUE(cfg.Pwm.Tm2.Sm20.PwmFrequency >= 1);
  TEST_ASSERT_TRUE(cfg.Pwm.Tm4.Sm42.PwmFrequency >= 1);
  TEST_ASSERT_TRUE(cfg.Pwm.Tm1.Sm13.PwmFrequency >= 1);

  // Outputs off, not half on.
  TEST_ASSERT_EQUAL_UINT16(0, cfg.Pwm.Tm2.Sm20.ChannelA.DutyCycle);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.Pwm.Tm2.Sm20.ChannelB.DutyCycle);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.Pwm.Tm1.Sm13.ChannelA.DutyCycle);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.Pwm.Tm1.Sm13.ChannelB.DutyCycle);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.Pwm.Tm4.Sm42.ChannelB.DutyCycle);

  // Every pair independent until an operator says otherwise.
  TEST_ASSERT_EQUAL_UINT8(PairIndependent, cfg.Pwm.Tm2.Sm20.Pair);

  // And a dead time that is at least the floor, in case a pair is enabled later.
  TEST_ASSERT_TRUE(cfg.Pwm.Tm2.Sm20.DeadTime >= MinHalfBridgeDeadTimeNs);
}

void test_json_fallbacks_match_the_compiled_defaults() {
  // The two paths a board can arrive by - no settings file at all, and a settings file
  // missing keys - must land on the SAME values. They disagreed for DeadTime,
  // PwmFrequency and DutyCycle, and in every case the JSON side was the unsafe one.
  MainConfig compiled;
  MainConfig fromEmpty;
  JsonDocument empty;
  configFromJson(empty, fromEmpty);

  TEST_ASSERT_EQUAL_UINT32(compiled.Pwm.Tm2.Sm20.PwmFrequency, fromEmpty.Pwm.Tm2.Sm20.PwmFrequency);
  TEST_ASSERT_EQUAL_UINT16(compiled.Pwm.Tm2.Sm20.DeadTime, fromEmpty.Pwm.Tm2.Sm20.DeadTime);
  TEST_ASSERT_EQUAL_UINT16(compiled.Pwm.Tm2.Sm20.ChannelA.DutyCycle,
                           fromEmpty.Pwm.Tm2.Sm20.ChannelA.DutyCycle);
  TEST_ASSERT_EQUAL_UINT16(compiled.Pwm.Tm2.Sm20.ChannelB.DutyCycle,
                           fromEmpty.Pwm.Tm2.Sm20.ChannelB.DutyCycle);
  TEST_ASSERT_EQUAL_UINT8(compiled.Pwm.Tm2.Sm20.Pair, fromEmpty.Pwm.Tm2.Sm20.Pair);
}

void test_validate_clamps_pwm_frequency_on_every_submodule() {
  MainConfig cfg;
  cfg.Pwm.Tm2.Sm20.PwmFrequency = 0;       // stale mirror, canonical carrier wins
  cfg.Pwm.Tm4.Sm42.PwmFrequency = 5000000; // no usable duty resolution
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT32(DefaultModulationCarrierHz, cfg.Pwm.Tm2.Sm20.PwmFrequency);
  TEST_ASSERT_EQUAL_UINT32(DefaultPwmFrequencyHz, cfg.Pwm.Tm4.Sm42.PwmFrequency);

  cfg.Pwm.Tm2.SpwmCarrierFrequency = 24000;
  cfg.Pwm.Tm2.Sm20.PwmFrequency = 20000;
  cfg.Pwm.Tm2.Sm21.PwmFrequency = 1000;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT32(24000, cfg.Pwm.Tm2.Sm20.PwmFrequency);
  TEST_ASSERT_EQUAL_UINT32(24000, cfg.Pwm.Tm2.Sm21.PwmFrequency);
  TEST_ASSERT_EQUAL_UINT32(24000, cfg.Pwm.Tm2.Sm22.PwmFrequency);
  TEST_ASSERT_EQUAL_UINT32(24000, cfg.Pwm.Tm2.Sm23.PwmFrequency);
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

void test_validate_modulation_domain_and_removed_sync_option() {
  MainConfig cfg;
  cfg.Pwm.SyncPwm = true;
  cfg.Pwm.Tm2.UseSpwm = true;
  cfg.Pwm.Tm2.SpwmCarrierFrequency = 20000;
  cfg.Pwm.Tm2.SpwmModulationFrequency = 10001; // above Nyquist
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_FALSE(cfg.Pwm.SyncPwm);
  TEST_ASSERT_FALSE(cfg.Pwm.Tm2.UseSpwm);

  cfg = MainConfig{};
  cfg.Pwm.Tm2.UseSpwm = true;
  cfg.Pwm.Tm2.SpwmCarrierFrequency = MaxModulationCarrierHz + 1;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT32(DefaultModulationCarrierHz,
                           cfg.Pwm.Tm2.SpwmCarrierFrequency);

  cfg = MainConfig{};
  cfg.Pwm.Tm2.SpwmCarrierFrequency = MinRepresentablePwmFrequencyHz - 1;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT32(DefaultModulationCarrierHz,
                           cfg.Pwm.Tm2.SpwmCarrierFrequency);

  cfg = MainConfig{};
  cfg.Pwm.Tm2.DeadTimeCompensation = true;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_FALSE(cfg.Pwm.Tm2.DeadTimeCompensation);

  cfg = MainConfig{};
  cfg.Thermal.Enabled = true;
  cfg.Pwm.Tm2.UseSpwm = true;
  cfg.Pwm.Tm2.SpwmCarrierFrequency = 20000;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT32(10000, cfg.Pwm.Tm2.SpwmCarrierFrequency);
  TEST_ASSERT_EQUAL_UINT32(10000, cfg.Pwm.Tm2.Sm20.PwmFrequency);
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
  cfg.CurrentLimit.FilterPeriod = 255;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT8(0, cfg.CurrentLimit.FilterCount);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.CurrentLimit.FilterPeriod);

  cfg.CurrentLimit.Pin = 18; // valid alternative route (CMP1 channel 0)
  cfg.CurrentLimit.ThresholdMillivolts = 1650;
  TEST_ASSERT_FALSE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT8(18, cfg.CurrentLimit.Pin);
}

void test_validate_pll() {
  MainConfig cfg;
  cfg.Pll.MinHz = 60; // >= MaxHz
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT16(45, cfg.Pll.MinHz);
  TEST_ASSERT_EQUAL_UINT16(55, cfg.Pll.MaxHz);

  cfg.Pll.PhaseOffsetCentiDeg = 20000;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_INT16(0, cfg.Pll.PhaseOffsetCentiDeg);

  cfg.Pll.BandwidthDeciHz = 5;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT16(200, cfg.Pll.BandwidthDeciHz);

  // Exclusions: PLL wins over dither (dither turned off)...
  cfg.Pll.Enabled = true;
  cfg.Pwm.Tm2.CarrierDitherMode = 1;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT8(0, cfg.Pwm.Tm2.CarrierDitherMode);
  TEST_ASSERT_TRUE(cfg.Pll.Enabled);

  // ...but loses to the feedback amplitude loop (older feature keeps working)
  cfg.Feedback.Enabled = true;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_FALSE(cfg.Pll.Enabled);
  cfg.Feedback.Enabled = false;

  // ...and to stepped playback modes, where the DDS phase drives nothing
  cfg.Pll.Enabled = true;
  cfg.Pwm.Tm2.ReferenceWaveform = 4; // RefWaveSequence
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_FALSE(cfg.Pll.Enabled);

  cfg.Pwm.Tm2.ReferenceWaveform = 3; // RefWaveCustom
  cfg.Pwm.Tm2.WaveformSampleStep = true;
  cfg.Pll.Enabled = true;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_FALSE(cfg.Pll.Enabled);

  // Custom waveform via the DDS (no sample-step) is allowed
  cfg.Pwm.Tm2.WaveformSampleStep = false;
  cfg.Pll.Enabled = true;
  TEST_ASSERT_FALSE(validateConfig(cfg));
  TEST_ASSERT_TRUE(cfg.Pll.Enabled);

  // Nominal outside the lock window: unlock/coast would rail-snap the output
  cfg.Pwm.Tm2.SpwmModulationFrequency = 60;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_FALSE(cfg.Pll.Enabled);
  cfg.Pwm.Tm2.SpwmModulationFrequency = 50;

  // Carrier below 18x nominal: SOGI discretization outside its envelope
  cfg.Pll.Enabled = true;
  cfg.Pwm.Tm2.SpwmCarrierFrequency = 800;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_FALSE(cfg.Pll.Enabled);
  cfg.Pwm.Tm2.SpwmCarrierFrequency = 20000;

  // Even a numerically acceptable 18:1 ratio is not a qualified sampling
  // rate below the tested 1kHz PLL floor.
  cfg.Pll.Enabled = true;
  cfg.Pwm.Tm2.SpwmModulationFrequency = 1;
  cfg.Pll.MinHz = 1;
  cfg.Pll.MaxHz = 2;
  cfg.Pwm.Tm2.SpwmCarrierFrequency = 18;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_FALSE(cfg.Pll.Enabled);

  // The amplitude floor cannot be zeroed (noise would sweep the clamps)
  cfg.Pll.Enabled = true;
  cfg.Pll.MinLevelMillivolts = 0;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT16(100, cfg.Pll.MinLevelMillivolts);
}

void test_validate_mppt() {
  MainConfig cfg;
  cfg.Mppt.IntervalMs = 500; // too fast for a settled meter window
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT16(2100, cfg.Mppt.IntervalMs);

  // The floor tracks the soft-start ramp: each perturbation must finish
  // ramping before the measurement window opens
  cfg.Mppt.IntervalMs = 3000;
  cfg.Pwm.Tm2.SoftStartMs = 5000;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT16(7100, cfg.Mppt.IntervalMs);
  cfg.Pwm.Tm2.SoftStartMs = 0;

  // Degenerate noise thresholds: tiny restart delta fires on every step,
  // deadband above restart is unreachable dead code
  cfg.Mppt.RestartDeltaMw = 5;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT16(10, cfg.Mppt.DeadbandMw);
  TEST_ASSERT_EQUAL_UINT32(1000, cfg.Mppt.RestartDeltaMw);
  cfg.Mppt.DeadbandMw = 2000;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT16(10, cfg.Mppt.DeadbandMw);

  cfg.Mppt.MinIndexMilli = 900;
  cfg.Mppt.MaxIndexMilli = 800; // inverted window
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT16(50, cfg.Mppt.MinIndexMilli);
  TEST_ASSERT_EQUAL_UINT16(1000, cfg.Mppt.MaxIndexMilli);

  cfg.Mppt.MinStepMilli = 50; // above the max step
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT16(20, cfg.Mppt.StepMilli);
  TEST_ASSERT_EQUAL_UINT16(5, cfg.Mppt.MinStepMilli);

  // Same actuator as the feedback loop: the older feature wins
  cfg.Mppt.Enabled = true;
  cfg.Feedback.Enabled = true;
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_FALSE(cfg.Mppt.Enabled);
  cfg.Feedback.Enabled = false;

  cfg.Mppt.Enabled = true;
  TEST_ASSERT_FALSE(validateConfig(cfg));
  TEST_ASSERT_TRUE(cfg.Mppt.Enabled); // MPPT alone is fine (PLL too)
}

void test_power_mon_serde_and_validate() {
  // JSON fallbacks land on the compiled defaults (both zero-file paths agree)
  MainConfig compiled;
  MainConfig fromEmpty;
  JsonDocument empty;
  configFromJson(empty, fromEmpty);
  TEST_ASSERT_EQUAL(compiled.PowerMon.Enabled, fromEmpty.PowerMon.Enabled);
  TEST_ASSERT_EQUAL_UINT8(compiled.PowerMon.Address, fromEmpty.PowerMon.Address);
  TEST_ASSERT_EQUAL_UINT32(compiled.PowerMon.ShuntMicroOhm, fromEmpty.PowerMon.ShuntMicroOhm);
  TEST_ASSERT_EQUAL_UINT16(compiled.PowerMon.CurrentLsbMicroAmp,
                           fromEmpty.PowerMon.CurrentLsbMicroAmp);
  TEST_ASSERT_EQUAL_UINT16(compiled.PowerMon.AlertMilliAmp, fromEmpty.PowerMon.AlertMilliAmp);
  TEST_ASSERT_EQUAL_UINT16(compiled.PowerMon.IntervalMs, fromEmpty.PowerMon.IntervalMs);
  TEST_ASSERT_EQUAL_UINT8(compiled.PowerMon.PgEfusePin, fromEmpty.PowerMon.PgEfusePin);
  TEST_ASSERT_EQUAL_UINT8(compiled.PowerMon.PgBuckPin, fromEmpty.PowerMon.PgBuckPin);
  TEST_ASSERT_EQUAL_UINT8(compiled.PowerMon.AlertPin, fromEmpty.PowerMon.AlertPin);
  TEST_ASSERT_EQUAL_UINT8(compiled.PowerMon.ImonPin, fromEmpty.PowerMon.ImonPin);
  TEST_ASSERT_EQUAL_UINT16(compiled.PowerMon.ImonRimonOhm, fromEmpty.PowerMon.ImonRimonOhm);

  // Round trip with distinctive values
  MainConfig cfg;
  cfg.PowerMon.Enabled = true;
  cfg.PowerMon.Address = 0x44;
  cfg.PowerMon.ShuntMicroOhm = 5000;
  cfg.PowerMon.CurrentLsbMicroAmp = 100;
  cfg.PowerMon.AlertMilliAmp = 2000;
  cfg.PowerMon.IntervalMs = 250;
  cfg.PowerMon.PgEfusePin = 26;
  cfg.PowerMon.PgBuckPin = 27;
  cfg.PowerMon.AlertPin = 255;
  cfg.PowerMon.ImonPin = 38;
  cfg.PowerMon.ImonRimonOhm = 5360;
  JsonDocument doc;
  configToJson(cfg, doc);
  MainConfig back;
  configFromJson(doc, back);
  TEST_ASSERT_TRUE(back.PowerMon.Enabled);
  TEST_ASSERT_EQUAL_UINT8(0x44, back.PowerMon.Address);
  TEST_ASSERT_EQUAL_UINT32(5000, back.PowerMon.ShuntMicroOhm);
  TEST_ASSERT_EQUAL_UINT16(100, back.PowerMon.CurrentLsbMicroAmp);
  TEST_ASSERT_EQUAL_UINT16(2000, back.PowerMon.AlertMilliAmp);
  TEST_ASSERT_EQUAL_UINT16(250, back.PowerMon.IntervalMs);
  TEST_ASSERT_EQUAL_UINT8(26, back.PowerMon.PgEfusePin);
  TEST_ASSERT_EQUAL_UINT8(27, back.PowerMon.PgBuckPin);
  TEST_ASSERT_EQUAL_UINT8(255, back.PowerMon.AlertPin);
  TEST_ASSERT_EQUAL_UINT8(38, back.PowerMon.ImonPin);
  TEST_ASSERT_EQUAL_UINT16(5360, back.PowerMon.ImonRimonOhm);

  // Validation: a non-INA226 address, an unrepresentable CAL pair, and a
  // too-fast interval are all corrected to the driver-board defaults
  back.PowerMon.Address = 0x23;
  TEST_ASSERT_TRUE(validateConfig(back));
  TEST_ASSERT_EQUAL_UINT8(0x40, back.PowerMon.Address);

  back.PowerMon.ShuntMicroOhm = 1; // CAL would overflow 16 bits
  back.PowerMon.CurrentLsbMicroAmp = 1;
  TEST_ASSERT_TRUE(validateConfig(back));
  TEST_ASSERT_EQUAL_UINT32(10000, back.PowerMon.ShuntMicroOhm);
  TEST_ASSERT_EQUAL_UINT16(50, back.PowerMon.CurrentLsbMicroAmp);

  back.PowerMon.IntervalMs = 10;
  TEST_ASSERT_TRUE(validateConfig(back));
  TEST_ASSERT_EQUAL_UINT16(250, back.PowerMon.IntervalMs);

  // A config validateConfig has already corrected passes unchanged
  TEST_ASSERT_FALSE(validateConfig(back));
}

void test_dead_schema_keys_omitted() {
  MainConfig cfg;
  JsonDocument doc;
  configToJson(cfg, doc);

  // Const views so operator[] cannot create the keys we are proving absent
  const JsonObjectConst pwm = doc["Config"]["Pwm"];
  const JsonObjectConst currentLimit = doc["Config"]["CurrentLimit"];
  const JsonObjectConst tm2 = pwm["Tm2"];

  TEST_ASSERT_TRUE(pwm["SyncPwm"].isNull());
  TEST_ASSERT_TRUE(currentLimit["FilterCount"].isNull());
  TEST_ASSERT_TRUE(currentLimit["FilterPeriod"].isNull());
  TEST_ASSERT_TRUE(tm2["Sm20"]["PwmFrequency"].isNull());
  TEST_ASSERT_TRUE(tm2["Sm21"]["PwmFrequency"].isNull());
  TEST_ASSERT_TRUE(tm2["Sm22"]["PwmFrequency"].isNull());
  TEST_ASSERT_TRUE(tm2["Sm23"]["PwmFrequency"].isNull());
  TEST_ASSERT_TRUE(tm2["DeadTimeCompensation"].isNull());
  TEST_ASSERT_FALSE(tm2["SpwmCarrierFrequency"].isNull());
}

void test_legacy_dead_keys_still_accepted() {
  JsonDocument doc;
  deserializeJson(doc, R"({
    "Config":{
      "Pwm":{"SyncPwm":true,"Tm2":{"DeadTimeCompensation":true,"Sm20":{"PwmFrequency":12345}}},
      "CurrentLimit":{"FilterCount":3,"FilterPeriod":10}
    }
  })");
  MainConfig cfg;
  configFromJson(doc, cfg);

  TEST_ASSERT_TRUE(cfg.Pwm.SyncPwm);
  TEST_ASSERT_TRUE(cfg.Pwm.Tm2.DeadTimeCompensation);
  TEST_ASSERT_EQUAL_UINT8(3, cfg.CurrentLimit.FilterCount);
  TEST_ASSERT_EQUAL_UINT8(10, cfg.CurrentLimit.FilterPeriod);
  TEST_ASSERT_EQUAL_UINT32(12345, cfg.Pwm.Tm2.Sm20.PwmFrequency);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_from_empty_document);
  RUN_TEST(test_roundtrip_preserves_every_field);
  RUN_TEST(test_partial_document_keeps_defaults_elsewhere);
  RUN_TEST(test_redact_secrets_blanks_only_the_secrets);
  RUN_TEST(test_preserve_secrets_keeps_stored_values_on_empty_post);
  RUN_TEST(test_restore_secrets_is_unconditional);
  RUN_TEST(test_restore_secrets_clears_mqtt_on_host_change);
  RUN_TEST(test_config_api_reject_reason_current_limit_threshold);
  RUN_TEST(test_doc_completeness_gate);
  RUN_TEST(test_doc_completeness_checks_nested_keys_types_and_version);
  RUN_TEST(test_shorter_config_string_clears_the_old_tail);
  RUN_TEST(test_pair_mode_roundtrips_per_submodule);
  RUN_TEST(test_absent_pair_key_defaults_to_independent);
  RUN_TEST(test_validate_refuses_pairing_submodules_without_a_b_pin);
  RUN_TEST(test_validate_clamps_dead_time_to_a_representable_value);
  RUN_TEST(test_validate_is_idempotent_over_pair_modes);
  RUN_TEST(test_pair_change_is_visible_to_the_reconfigure_gates);
  RUN_TEST(test_reconfigure_gates_compare_values_not_object_bytes);
  RUN_TEST(test_validate_preserves_pair_intent_under_an_inverting_scheme);
  RUN_TEST(test_validate_still_enforces_the_permanent_hardware_facts);
  RUN_TEST(test_validate_leaves_non_cell_submodules_ungated_by_scheme);
  RUN_TEST(test_validate_gate_is_inactive_when_spwm_is_off);
  RUN_TEST(test_compiled_defaults_are_safe_with_no_settings_file);
  RUN_TEST(test_json_fallbacks_match_the_compiled_defaults);
  RUN_TEST(test_validate_clamps_pwm_frequency_on_every_submodule);
  RUN_TEST(test_validate_clamps_out_of_range_frequency);
  RUN_TEST(test_validate_modulation_domain_and_removed_sync_option);
  RUN_TEST(test_validate_current_limit);
  RUN_TEST(test_validate_pll);
  RUN_TEST(test_validate_mppt);
  RUN_TEST(test_power_mon_serde_and_validate);
  RUN_TEST(test_dead_schema_keys_omitted);
  RUN_TEST(test_legacy_dead_keys_still_accepted);
  return UNITY_END();
}
