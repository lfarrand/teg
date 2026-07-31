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
  // not author. restoreSecrets must overwrite even a populated credential,
  // so an imported file can never change the write PIN or pair the device's
  // real password with someone else's broker.
  MainConfig previous;
  copyConfigString(previous.Influx.Token, sizeof(previous.Influx.Token), "device-token");
  copyConfigString(previous.Security.WritePin, sizeof(previous.Security.WritePin), "1234");
  copyConfigString(previous.Mqtt.Password, sizeof(previous.Mqtt.Password), "device-pass");

  MainConfig incoming;
  copyConfigString(incoming.Influx.Token, sizeof(incoming.Influx.Token), "attacker-token");
  copyConfigString(incoming.Security.WritePin, sizeof(incoming.Security.WritePin), "9999");
  copyConfigString(incoming.Mqtt.Password, sizeof(incoming.Mqtt.Password), "attacker-pass");

  restoreSecrets(incoming, previous);
  TEST_ASSERT_EQUAL_STRING("device-token", incoming.Influx.Token);
  TEST_ASSERT_EQUAL_STRING("1234", incoming.Security.WritePin);
  TEST_ASSERT_EQUAL_STRING("device-pass", incoming.Mqtt.Password);
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
  // applyPwmConfig() only reconfigures a timer when
  // memcmp(&previous.Pwm.TmN, &config.Pwm.TmN, sizeof(...)) differs. A previous
  // attempt put the pair setting at Pwm. level, outside all four gates, so
  // changing it reconfigured nothing while the UI reported success. This test
  // replicates the comparison so that moving the field out again fails here
  // rather than silently going inert on hardware.
  MainConfig before, after;
  after.Pwm.Tm2.Sm20.Pair = PairHalfBridge;
  TEST_ASSERT_TRUE(memcmp(&before.Pwm.Tm2, &after.Pwm.Tm2, sizeof(after.Pwm.Tm2)) != 0);

  MainConfig b1, a1;
  a1.Pwm.Tm1.Sm13.Pair = PairDifferential;
  TEST_ASSERT_TRUE(memcmp(&b1.Pwm.Tm1, &a1.Pwm.Tm1, sizeof(a1.Pwm.Tm1)) != 0);

  MainConfig b3, a3;
  a3.Pwm.Tm3.Sm31.Pair = PairHalfBridge;
  TEST_ASSERT_TRUE(memcmp(&b3.Pwm.Tm3, &a3.Pwm.Tm3, sizeof(a3.Pwm.Tm3)) != 0);

  MainConfig b4, a4;
  a4.Pwm.Tm4.Sm42.Pair = PairDifferential;
  TEST_ASSERT_TRUE(memcmp(&b4.Pwm.Tm4, &a4.Pwm.Tm4, sizeof(a4.Pwm.Tm4)) != 0);

  // And an identical pair of configs must compare equal, so the gate does not fire
  // on every apply for no reason.
  MainConfig same1, same2;
  TEST_ASSERT_EQUAL_INT(0, memcmp(&same1.Pwm.Tm2, &same2.Pwm.Tm2, sizeof(same2.Pwm.Tm2)));
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

  // Zero frequency divided by zero in computeAsymmetricTimings, produced garbage edge
  // timings, and the outputs were enabled anyway.
  TEST_ASSERT_TRUE(cfg.Pwm.Tm2.Sm20.PwmFrequency >= 1);
  TEST_ASSERT_TRUE(cfg.Pwm.Tm4.Sm42.PwmFrequency >= 1);
  TEST_ASSERT_TRUE(cfg.Pwm.Tm1.Sm13.PwmFrequency >= 1);

  // Outputs off, not half on.
  TEST_ASSERT_EQUAL_UINT16(0, cfg.Pwm.Tm2.Sm20.ChannelA.DutyCycle);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.Pwm.Tm2.Sm20.ChannelB.DutyCycle);
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
  cfg.Pwm.Tm2.Sm20.PwmFrequency = 0;       // divide-by-zero downstream
  cfg.Pwm.Tm4.Sm42.PwmFrequency = 5000000; // no usable duty resolution
  TEST_ASSERT_TRUE(validateConfig(cfg));
  TEST_ASSERT_EQUAL_UINT32(1000, cfg.Pwm.Tm2.Sm20.PwmFrequency);
  TEST_ASSERT_EQUAL_UINT32(1000, cfg.Pwm.Tm4.Sm42.PwmFrequency);

  cfg.Pwm.Tm2.Sm20.PwmFrequency = 20000; // in range, left alone
  validateConfig(cfg);
  TEST_ASSERT_EQUAL_UINT32(20000, cfg.Pwm.Tm2.Sm20.PwmFrequency);
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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_from_empty_document);
  RUN_TEST(test_roundtrip_preserves_every_field);
  RUN_TEST(test_partial_document_keeps_defaults_elsewhere);
  RUN_TEST(test_redact_secrets_blanks_only_the_secrets);
  RUN_TEST(test_preserve_secrets_keeps_stored_values_on_empty_post);
  RUN_TEST(test_restore_secrets_is_unconditional);
  RUN_TEST(test_doc_completeness_gate);
  RUN_TEST(test_pair_mode_roundtrips_per_submodule);
  RUN_TEST(test_absent_pair_key_defaults_to_independent);
  RUN_TEST(test_validate_refuses_pairing_submodules_without_a_b_pin);
  RUN_TEST(test_validate_clamps_dead_time_to_a_representable_value);
  RUN_TEST(test_validate_is_idempotent_over_pair_modes);
  RUN_TEST(test_pair_change_is_visible_to_the_reconfigure_gates);
  RUN_TEST(test_validate_preserves_pair_intent_under_an_inverting_scheme);
  RUN_TEST(test_validate_still_enforces_the_permanent_hardware_facts);
  RUN_TEST(test_validate_leaves_non_cell_submodules_ungated_by_scheme);
  RUN_TEST(test_validate_gate_is_inactive_when_spwm_is_off);
  RUN_TEST(test_compiled_defaults_are_safe_with_no_settings_file);
  RUN_TEST(test_json_fallbacks_match_the_compiled_defaults);
  RUN_TEST(test_validate_clamps_pwm_frequency_on_every_submodule);
  RUN_TEST(test_validate_clamps_out_of_range_frequency);
  RUN_TEST(test_validate_current_limit);
  RUN_TEST(test_validate_pll);
  RUN_TEST(test_validate_mppt);
  return UNITY_END();
}
