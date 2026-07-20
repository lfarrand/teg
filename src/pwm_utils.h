#ifndef PWM_UTILS_H
#define PWM_UTILS_H

#include <Arduino.h>
#include <arm_math.h>
#include <eFlexPwm.h>
#include <TeensyTimerTool.h>

#define LOG_BUF_SIZE 256
static constexpr uint16_t FIFTY_PERCENT_DUTY = 32768;
constexpr uint16_t MinDutyCycle = 0;
constexpr uint16_t MidDutyCycle = 32768;
constexpr uint16_t MaxDutyCycle = 65535;
constexpr uint16_t MAX_COUNTER_VALUE = 0xFFFF;

extern volatile uint32_t vPhase;
extern volatile uint32_t vIsrCycles;

extern const uint8_t PrescalerValues[];
extern const char *prescaleStr[];

constexpr int TriggerPin = 13;

extern TeensyTimerTool::OneShotTimer startupTimer;
extern TeensyTimerTool::OneShotTimer chargeToggleTimer;
extern TeensyTimerTool::OneShotTimer dischargeToggleTimer;
extern TeensyTimerTool::PeriodicTimer pwmSyncTimer;

using namespace eFlex;

extern SubModule Sm13;
extern SubModule Sm20;
extern SubModule Sm21;
extern SubModule Sm22;
extern SubModule Sm23;
extern SubModule Sm31;
extern SubModule Sm40;
extern SubModule Sm41;
extern SubModule Sm42;

extern Timer &Tm1;
extern Timer &Tm2;
extern Timer &Tm3;
extern Timer &Tm4;

void configureTimers();

void startupTimerCallback();

void chargeToggleTimerCallback();

void dischargeToggleTimerCallback();

void configurePwm();

struct MainConfig;
void applyPwmConfig(const MainConfig &previous);

void buildSpwmLut();

// True when the Tm2 modulation ISR should run (SPWM enabled and scheme is not fixed-duty)
bool spwmActive();

// Live amplitude control (clamped to the maximum modulation index)
void setModulationIndexTargetQ15(uint32_t targetQ15);

// Closed-loop amplitude regulation; call from loop()
void runFeedbackLoop();

// Fast software fault trip on a GPIO pin; call after configurePwm() and on config change
void configureFaultProtection();
extern volatile bool vFaultTripped;

void configureModule1();

void configureModule2();

void configureModule3();

void configureModule4();

void attachInterruptVectors();

void attachModule2PwmInterruptVectors();

void enablePwmInterrupts();

void disablePwmInterrupts();

void enableModule2PwmInterrupts();

void disableModule2PwmInterrupts();

FASTRUN void IsrOverflowSm20();

void enableXbar();

uint8_t calculateBestPrescaler(uint32_t pwmFrequency);

void printStats();

void logPwmModuleStats(const char *moduleId, SubModule &pwmModule, bool hasChanB = true);

bool xbarConnect(uint8_t input, uint8_t output);

#endif