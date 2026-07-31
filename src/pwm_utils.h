#ifndef PWM_UTILS_H
#define PWM_UTILS_H

#include <Arduino.h>
#include <eFlexPwm.h>

#define LOG_BUF_SIZE 256
constexpr uint16_t MinDutyCycle = 0;
constexpr uint16_t MidDutyCycle = 32768;
constexpr uint16_t MaxDutyCycle = 65535;
constexpr uint16_t MAX_COUNTER_VALUE = 0xFFFF;

extern volatile uint32_t vPhase;
extern volatile uint32_t vIsrCycles;
// Carrier cycles the modulation ISR was too late to serve. Non-zero means something
// blocked or preempted it; resets when the carrier is reconfigured.
extern volatile uint32_t vMissedIsrCycles;

extern const char *prescaleStr[];

constexpr int TriggerPin = 13;


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

void configurePwm();

// Mask every PWM output, un-inverting any polarity-inverted cell first. MASK forces
// outputs to logic 0 BEFORE polarity is applied (RM 55.8.45.4), so masking a LowTrue
// cell drives its pins HIGH - the gate commanded ON by the shutdown. Use this instead
// of Tm*.disable() on any path that exists to make the outputs safe.
void maskAllOutputsSafely();

struct MainConfig;
void applyPwmConfig(const MainConfig &previous);

void buildSpwmLut();

void buildDitherState();

// True when the Tm2 modulation ISR should run (SPWM enabled and scheme is not fixed-duty)
bool spwmActive();

// Live amplitude control (clamped to the maximum modulation index scaled by
// the thermal derate factor)
void setModulationIndexTargetQ15(uint32_t targetQ15);
void setThermalDerateMilli(uint16_t derateMilli);

// Live telemetry for the web UI status endpoint
uint32_t modulationIndexNowMilli();
uint32_t modulationIndexTargetMilli();
uint64_t modulationActualMilliHz();

// PLL steering: live DDS frequency override without a reconfigure (aligned
// 32-bit writes are atomic; the ISR picks the new increment up next cycle)
void modulationSetPhaseIncrement(uint32_t increment);
uint32_t modulationPhaseIncrement();
uint32_t modulationPhaseNow();  // DDS accumulator snapshot
bool modulationDdsDriven();     // false in stepped playback (sequence/sample-step/stream)

// Closed-loop amplitude regulation; call from loop()
void runFeedbackLoop();

// Fast software fault trip on a GPIO pin; call after configurePwm() and on config change
void configureFaultProtection();
void enterOtaSafeState(); // mask outputs + modulation IRQ; only reboot leaves
// Clear a latched trip and restore outputs/interrupts/capture
void clearFaultTrip();
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