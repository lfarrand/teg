#ifndef CONFIG_JSON_H
#define CONFIG_JSON_H

#include <Arduino.h>

struct ChannelConfig {
  uint32_t OnPeriodMicroseconds{};
  uint16_t DutyCycle{};
  uint8_t PhaseShift{};
  bool Enabled = true;
};

struct SubmoduleConfig {
  uint16_t DeadTime{};
  uint32_t PwmFrequency{};
  ChannelConfig ChannelA;
  ChannelConfig ChannelB;
};

struct Module1Config {
  SubmoduleConfig Sm13;
};

struct Module2Config {
  bool UseSpwm = false;
  uint32_t SpwmCarrierFrequency = 20000;
  uint32_t SpwmModulationFrequency = 50;
  SubmoduleConfig Sm20;
  SubmoduleConfig Sm21;
  SubmoduleConfig Sm22;
  SubmoduleConfig Sm23;
};

struct Module3Config {
  SubmoduleConfig Sm31;
};

struct Module4Config {
  SubmoduleConfig Sm40;
  SubmoduleConfig Sm41;
  SubmoduleConfig Sm42;
};

struct AsymmetricInductionConfig {
  bool IsEnabled = true;
  int32_t PreShiftNanos = 250;
  int32_t PostShiftNanos = 500;
};

struct PwmConfig {
  Module1Config Tm1;
  Module2Config Tm2;
  Module3Config Tm3;
  Module4Config Tm4;
  bool PrintRegs = false;
  bool SyncPwm = false;
};

struct MainConfig {
  PwmConfig Pwm;
  AsymmetricInductionConfig AsymmetricInduction;
};

extern MainConfig config;

void loadConfiguration(const char *filename);

void saveConfiguration(const char *filename);

void printFile(const char *filename);

void loadConfigurationFromFlash(const char* filename);

void saveConfigurationToFlash(const char* filename, const MainConfig& config);

#endif