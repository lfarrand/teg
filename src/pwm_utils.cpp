#include "pwm_utils.h"
#include "utils.h"
#include <QNEthernet.h>

#include "config_json.h"

extern qindesign::network::EthernetServer server;

volatile uint32_t vSample = 0;
volatile float32_t vSpwmUpdateSpeed = 0.0f;

const uint8_t PrescalerValues[] = {1, 2, 4, 8, 16, 32, 64, 128};

const char *prescaleStr[] = {
  "fclk/1", "fclk/2", "fclk/4", "fclk/8", "fclk/16", "fclk/32", "fclk/64", "fclk/128"
};

TeensyTimerTool::OneShotTimer startupTimer(TeensyTimerTool::TCK64);
TeensyTimerTool::OneShotTimer chargeToggleTimer(TeensyTimerTool::GPT1);
TeensyTimerTool::OneShotTimer dischargeToggleTimer(TeensyTimerTool::GPT2);
TeensyTimerTool::PeriodicTimer pwmSyncTimer(TeensyTimerTool::PIT);

SubModule Sm13(8, 7);
SubModule Sm20(4, 33);
SubModule Sm21(5);
SubModule Sm22(6, 9);
SubModule Sm23(36, 37);
SubModule Sm31(29, 28);
SubModule Sm40(22);
SubModule Sm41(23);
SubModule Sm42(2, 3);

eFlex::Timer &Tm1 = Sm13.timer();
eFlex::Timer &Tm2 = Sm20.timer();
eFlex::Timer &Tm3 = Sm31.timer();
eFlex::Timer &Tm4 = Sm40.timer();

extern MainConfig config;

void configureTimers() {
}

void startupTimerCallback() {
}

void chargeToggleTimerCallback() {
}

void dischargeToggleTimerCallback() {
}

void configurePwm() {
  if (config.Pwm.Tm2.UseSpwm) {
    vSpwmUpdateSpeed = (2.0f * PI * static_cast<float32_t>(config.Pwm.Tm2.SpwmModulationFrequency)) / static_cast<float32_t>(config.Pwm.Tm2.SpwmCarrierFrequency);
  }

  configureModule1();
  configureModule2();
  configureModule3();
  configureModule4();
}

void configureModule1() {
  char strBuf[LOG_BUF_SIZE];
  uint32_t ret;

  Sm13.setupDeadtime(config.Pwm.Tm1.Sm13.DeadTime, ChanA);
  Sm13.setupDeadtime(config.Pwm.Tm1.Sm13.DeadTime, ChanB);

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM13 ChanA & ChanB deadtime to %u ns", config.Pwm.Tm1.Sm13.DeadTime);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  if (Sm13.setPwmFrequency(config.Pwm.Tm1.Sm13.PwmFrequency, false, true)) {
    ret = snprintf(strBuf, sizeof(strBuf), "Set SM13 PWM freq. to %luHz", config.Pwm.Tm1.Sm13.PwmFrequency);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  } else {
    ret = snprintf(strBuf, sizeof(strBuf), "Failed to set SM13 PWM freq. to %luHz", config.Pwm.Tm1.Sm13.PwmFrequency);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  }

  Sm13.setupDutyCycle(ChanA, config.Pwm.Tm1.Sm13.ChannelA.DutyCycle);
  Sm13.setupDutyCycle(ChanB, config.Pwm.Tm1.Sm13.ChannelB.DutyCycle);

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM13 ChanA duty cycle to %u", config.Pwm.Tm1.Sm13.ChannelA.DutyCycle);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM13 ChanB duty cycle to %u", config.Pwm.Tm1.Sm13.ChannelB.DutyCycle);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  Tm1.setPwmLdok(true);
  Tm1.enable();

  if (!Sm13.begin(true, true, false)) {
    writeLog(F("Failed to start SM13"));
    exit(EXIT_FAILURE);
  }

  if (config.Pwm.PrintRegs) {
    writeLog(F("Printing SM13 register values"));
    Sm13.printRegs();
  }

  writeLog(F("Started TM1"));
}

void configureModule2() {
  char strBuf[LOG_BUF_SIZE];
  uint32_t ret;

  Sm20.setupDeadtime(config.Pwm.Tm2.Sm20.DeadTime, ChanA);
  Sm20.setupDeadtime(config.Pwm.Tm2.Sm20.DeadTime, ChanB);

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM20 ChanA & ChanB deadtime to %u ns", config.Pwm.Tm2.Sm20.DeadTime);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  if (Sm20.setPwmFrequency(config.Pwm.Tm2.Sm20.PwmFrequency, false, true)) {
    ret = snprintf(strBuf, sizeof(strBuf), "Set SM20 PWM freq. to %luHz", config.Pwm.Tm2.Sm20.PwmFrequency);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  } else {
    ret = snprintf(strBuf, sizeof(strBuf), "Failed to set SM20 PWM freq. to %luHz", config.Pwm.Tm2.Sm20.PwmFrequency);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  }

  Sm20.setupDutyCycle(ChanA, config.Pwm.Tm2.Sm20.ChannelA.DutyCycle);
  Sm20.setupDutyCycle(ChanB, config.Pwm.Tm2.Sm20.ChannelB.DutyCycle);

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM20 ChanA duty cycle to %u", config.Pwm.Tm2.Sm20.ChannelA.DutyCycle);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM20 ChanB duty cycle to %u", config.Pwm.Tm2.Sm20.ChannelB.DutyCycle);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  Sm21.setupDeadtime(config.Pwm.Tm2.Sm21.DeadTime, ChanA);

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM21 ChanA deadtime to %u ns", config.Pwm.Tm2.Sm21.DeadTime);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  if (Sm21.setPwmFrequency(config.Pwm.Tm2.Sm21.PwmFrequency, false, true)) {
    ret = snprintf(strBuf, sizeof(strBuf), "Set SM21 PWM freq. to %luHz", config.Pwm.Tm2.Sm21.PwmFrequency);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  } else {
    ret = snprintf(strBuf, sizeof(strBuf), "Failed to set SM21 PWM freq. to %luHz", config.Pwm.Tm2.Sm21.PwmFrequency);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  }

  Sm21.setupDutyCycle(ChanA, config.Pwm.Tm2.Sm21.ChannelA.DutyCycle);

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM21 ChanA duty cycle to %u", config.Pwm.Tm2.Sm21.ChannelA.DutyCycle);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  Sm22.setupDeadtime(config.Pwm.Tm2.Sm22.DeadTime, ChanA);
  Sm22.setupDeadtime(config.Pwm.Tm2.Sm22.DeadTime, ChanB);

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM22 ChanA & ChanB deadtime to %u ns", config.Pwm.Tm2.Sm22.DeadTime);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  if (Sm22.setPwmFrequency(config.Pwm.Tm2.Sm22.PwmFrequency, false, true)) {
    ret = snprintf(strBuf, sizeof(strBuf), "Set SM22 PWM freq. to %luHz", config.Pwm.Tm2.Sm22.PwmFrequency);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  } else {
    ret = snprintf(strBuf, sizeof(strBuf), "Failed to set SM22 PWM freq. to %luHz", config.Pwm.Tm2.Sm22.PwmFrequency);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  }

  Sm22.setupDutyCycle(ChanA, config.Pwm.Tm2.Sm22.ChannelA.DutyCycle);
  Sm22.setupDutyCycle(ChanB, config.Pwm.Tm2.Sm22.ChannelB.DutyCycle);

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM22 ChanA duty cycle to %u", config.Pwm.Tm2.Sm22.ChannelA.DutyCycle);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM22 ChanB duty cycle to %u", config.Pwm.Tm2.Sm22.ChannelB.DutyCycle);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  Sm23.setupDeadtime(config.Pwm.Tm2.Sm23.DeadTime, ChanA);
  Sm23.setupDeadtime(config.Pwm.Tm2.Sm23.DeadTime, ChanB);

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM23 ChanA & ChanB deadtime to %u ns", config.Pwm.Tm2.Sm23.DeadTime);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  if (Sm23.setPwmFrequency(config.Pwm.Tm2.Sm23.PwmFrequency, false, true)) {
    ret = snprintf(strBuf, sizeof(strBuf), "Set SM23 PWM freq. to %luHz", config.Pwm.Tm2.Sm23.PwmFrequency);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  } else {
    ret = snprintf(strBuf, sizeof(strBuf), "Failed to set SM23 PWM freq. to %luHz", config.Pwm.Tm2.Sm23.PwmFrequency);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  }

  Sm23.setupDutyCycle(ChanA, config.Pwm.Tm2.Sm23.ChannelA.DutyCycle);
  Sm23.setupDutyCycle(ChanB, config.Pwm.Tm2.Sm23.ChannelB.DutyCycle);

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM23 ChanA duty cycle to %u", config.Pwm.Tm2.Sm23.ChannelA.DutyCycle);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM23 ChanB duty cycle to %u", config.Pwm.Tm2.Sm23.ChannelB.DutyCycle);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  Tm2.setPwmLdok(true);
  Tm2.enable();

  if (!Sm20.begin(true, true, false)) {
    writeLog(F("Failed to start SM20"));
    exit(EXIT_FAILURE);
  }

  if (!Sm21.begin()) {
    writeLog(F("Failed to start SM21"));
    exit(EXIT_FAILURE);
  }

  if (!Sm22.begin(true, true, false)) {
    writeLog(F("Failed to start SM22"));
    exit(EXIT_FAILURE);
  }

  if (!Sm23.begin(true, true, false)) {
    writeLog(F("Failed to start SM23"));
    exit(EXIT_FAILURE);
  }

  if (config.Pwm.PrintRegs) {
    writeLog(F("Printing SM20 register values"));
    Sm20.printRegs();
    writeLog(F("Printing SM21 register values"));
    Sm21.printRegs();
    writeLog(F("Printing SM22 register values"));
    Sm22.printRegs();
    writeLog(F("Printing SM23 register values"));
    Sm23.printRegs();
  }

  writeLog(F("Started TM2"));
}

void configureModule3() {
  char strBuf[LOG_BUF_SIZE];
  uint32_t ret;

  Sm31.setupDeadtime(config.Pwm.Tm3.Sm31.DeadTime, ChanA);
  Sm31.setupDeadtime(config.Pwm.Tm3.Sm31.DeadTime, ChanB);

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM31 ChanA & ChanB deadtime to %u ns", config.Pwm.Tm3.Sm31.DeadTime);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  if (Sm31.setPwmFrequency(config.Pwm.Tm3.Sm31.PwmFrequency, false, true)) {
    ret = snprintf(strBuf, sizeof(strBuf), "Set SM31 PWM freq. to %luHz", config.Pwm.Tm3.Sm31.PwmFrequency);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  } else {
    ret = snprintf(strBuf, sizeof(strBuf), "Failed to set SM31 PWM freq. to %luHz", config.Pwm.Tm3.Sm31.PwmFrequency);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  }

  Sm31.setupDutyCycle(ChanA, config.Pwm.Tm3.Sm31.ChannelA.DutyCycle);
  Sm31.setupDutyCycle(ChanB, config.Pwm.Tm3.Sm31.ChannelB.DutyCycle);

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM31 ChanA duty cycle to %u", config.Pwm.Tm3.Sm31.ChannelA.DutyCycle);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM31 ChanB duty cycle to %u", config.Pwm.Tm3.Sm31.ChannelB.DutyCycle);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  Tm3.setPwmLdok(true);
  Tm3.enable();

  if (!Sm31.begin(true, true, false)) {
    writeLog(F("Failed to start SM31"));
    exit(EXIT_FAILURE);
  }

  if (config.Pwm.PrintRegs) {
    writeLog(F("Printing SM31 register values"));
    Sm31.printRegs();
  }

  writeLog(F("Started TM3"));
}

void configureModule4() {
  char strBuf[LOG_BUF_SIZE];
  uint32_t ret;

  Sm40.setupDeadtime(config.Pwm.Tm4.Sm40.DeadTime, ChanA);

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM40 ChanA deadtime to %u ns", config.Pwm.Tm4.Sm40.DeadTime);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  if (Sm40.setPwmFrequency(config.Pwm.Tm4.Sm40.PwmFrequency, false, true)) {
    ret = snprintf(strBuf, sizeof(strBuf), "Set SM40 PWM freq. to %luHz", config.Pwm.Tm4.Sm40.PwmFrequency);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  } else {
    ret = snprintf(strBuf, sizeof(strBuf), "Failed to set SM40 PWM freq. to %luHz", config.Pwm.Tm4.Sm40.PwmFrequency);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  }

  Sm40.setupDutyCycle(ChanA, config.Pwm.Tm4.Sm40.ChannelA.DutyCycle);

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM40 ChanA duty cycle to %u", config.Pwm.Tm4.Sm40.ChannelA.DutyCycle);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  Sm41.setupDeadtime(config.Pwm.Tm4.Sm41.DeadTime, ChanA);

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM41 ChanA deadtime to %u ns", config.Pwm.Tm4.Sm41.DeadTime);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  if (Sm41.setPwmFrequency(config.Pwm.Tm4.Sm41.PwmFrequency, false, true)) {
    ret = snprintf(strBuf, sizeof(strBuf), "Set SM41 PWM freq. to %luHz", config.Pwm.Tm4.Sm41.PwmFrequency);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  } else {
    ret = snprintf(strBuf, sizeof(strBuf), "Failed to set SM41 PWM freq. to %luHz", config.Pwm.Tm4.Sm41.PwmFrequency);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  }

  Sm41.setupDutyCycle(ChanA, config.Pwm.Tm4.Sm41.ChannelA.DutyCycle);

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM41 ChanA duty cycle to %u", config.Pwm.Tm4.Sm41.ChannelA.DutyCycle);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  Sm42.setupDeadtime(config.Pwm.Tm4.Sm42.DeadTime, ChanA);
  Sm42.setupDeadtime(config.Pwm.Tm4.Sm42.DeadTime, ChanB);

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM42 ChanA & ChanB deadtime to %u ns", config.Pwm.Tm4.Sm42.DeadTime);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  if (Sm42.setPwmFrequency(config.Pwm.Tm4.Sm42.PwmFrequency, false, true)) {
    ret = snprintf(strBuf, sizeof(strBuf), "Set SM42 PWM freq. to %luHz", config.Pwm.Tm4.Sm42.PwmFrequency);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  } else {
    ret = snprintf(strBuf, sizeof(strBuf), "Failed to set SM42 PWM freq. to %luHz", config.Pwm.Tm4.Sm42.PwmFrequency);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  }

  Sm42.setupDutyCycle(ChanA, config.Pwm.Tm4.Sm42.ChannelA.DutyCycle);

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM42 ChanA duty cycle to %u", config.Pwm.Tm4.Sm42.ChannelA.DutyCycle);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  Sm42.setupDutyCycle(ChanB, config.Pwm.Tm4.Sm42.ChannelB.DutyCycle);

  ret = snprintf(strBuf, sizeof(strBuf), "Setting SM42 ChanB duty cycle to %u", config.Pwm.Tm4.Sm42.ChannelB.DutyCycle);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  if (config.Pwm.Tm4.Sm41.ChannelA.PhaseShift != 0) {
    Sm41.setupDutyCycle(ChanA, FIFTY_PERCENT_DUTY);

    ret = snprintf(strBuf, sizeof(strBuf), "SM41 ChanA phase shift was %u, setting duty cycle to %u", config.Pwm.Tm4.Sm41.ChannelA.PhaseShift, FIFTY_PERCENT_DUTY);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  } else {
    Sm41.setupDutyCycle(ChanA, config.Pwm.Tm4.Sm41.ChannelA.DutyCycle);

    ret = snprintf(strBuf, sizeof(strBuf), "Setting SM41 ChanA duty cycle to %u", config.Pwm.Tm4.Sm41.ChannelA.DutyCycle);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }
  }

  if (!config.AsymmetricInduction.IsEnabled) {
    if (config.Pwm.Tm4.Sm42.ChannelA.PhaseShift != 0) {
      Sm42.setupDutyCycle(ChanA, FIFTY_PERCENT_DUTY);

      ret = snprintf(strBuf, sizeof(strBuf), "SM42 ChanA phase shift was %u, setting duty cycle to %u", config.Pwm.Tm4.Sm42.ChannelA.PhaseShift, FIFTY_PERCENT_DUTY);
      if (ret >= sizeof(strBuf)) {
        writeLog("Log buffer overflow!");
      } else {
        writeLog(strBuf);
      }
    } else {
      Sm42.setupDutyCycle(ChanA, config.Pwm.Tm4.Sm42.ChannelA.DutyCycle);

      ret = snprintf(strBuf, sizeof(strBuf), "Setting SM42 ChanA duty cycle to %u", config.Pwm.Tm4.Sm42.ChannelA.DutyCycle);
      if (ret >= sizeof(strBuf)) {
        writeLog("Log buffer overflow!");
      } else {
        writeLog(strBuf);
      }
    }

    if (config.Pwm.Tm4.Sm42.ChannelB.PhaseShift != 0) {
      Sm42.setupDutyCycle(ChanB, FIFTY_PERCENT_DUTY);

      ret = snprintf(strBuf, sizeof(strBuf), "SM42 ChanB phase shift was %u, setting duty cycle to %u", config.Pwm.Tm4.Sm42.ChannelB.PhaseShift, FIFTY_PERCENT_DUTY);
      if (ret >= sizeof(strBuf)) {
        writeLog("Log buffer overflow!");
      } else {
        writeLog(strBuf);
      }
    } else {
      Sm42.setupDutyCycle(ChanB, config.Pwm.Tm4.Sm42.ChannelB.DutyCycle);

      ret = snprintf(strBuf, sizeof(strBuf), "Setting SM42 ChanB duty cycle to %u", config.Pwm.Tm4.Sm42.ChannelB.DutyCycle);
      if (ret >= sizeof(strBuf)) {
        writeLog("Log buffer overflow!");
      } else {
        writeLog(strBuf);
      }
    }
  }

  if (config.AsymmetricInduction.IsEnabled) {
    const uint8_t prescalerIndex = calculateBestPrescaler(config.Pwm.Tm4.Sm42.PwmFrequency);
    const float32_t dutyCycleA = static_cast<float32_t>(config.Pwm.Tm4.Sm42.ChannelA.DutyCycle) / 65536;
    const int32_t preShiftNanos = config.AsymmetricInduction.PreShiftNanos;
    const int32_t postShiftNanos = config.AsymmetricInduction.PostShiftNanos;
    constexpr int32_t nanosecondsPerSecond = 1000000000;
    constexpr auto nanosecondsPerSecondFloat = static_cast<float32_t>(nanosecondsPerSecond);
    const float32_t clockTicksPerNanosecond = F_BUS_ACTUAL / nanosecondsPerSecondFloat;
    const uint32_t periodTicks = F_BUS_ACTUAL / config.Pwm.Tm4.Sm42.PwmFrequency;
    const int16_t pulseTicksA = periodTicks * dutyCycleA;
    const int16_t preShiftTicks = clockTicksPerNanosecond * preShiftNanos;
    const int16_t postShiftTicks = clockTicksPerNanosecond * postShiftNanos;
    constexpr int16_t periodStart = 0;
    const int16_t periodEnd = periodTicks - 1;
    constexpr int16_t startChanA = periodStart;
    const int16_t stopChanA = startChanA + pulseTicksA;
    const int16_t startChanB = stopChanA - preShiftTicks;
    const int16_t stopChanB = periodEnd - postShiftTicks;

    ret = snprintf(strBuf, sizeof(strBuf), "PWM 4.2 %ldHz %s periodTicks:%lu pulseTicksA:%d preShiftNanos:%ld preShiftTicks:%d postShiftNanos:%ld postShiftTicks:%d periodStart:%d periodEnd:%d startChanA:%d stopChanA:%d startChanB:%d stopChanB:%d", config.Pwm.Tm4.Sm42.PwmFrequency, prescaleStr[prescalerIndex], periodTicks, pulseTicksA, preShiftNanos, preShiftTicks, postShiftNanos, postShiftTicks, periodStart, periodEnd, startChanA, stopChanA, startChanB, stopChanB);
    if (ret >= sizeof(strBuf)) {
      writeLog("Log buffer overflow!");
    } else {
      writeLog(strBuf);
    }

    Sm42.disableOutput(ChanA);
    Sm42.disableOutput(ChanB);

    Sm42.setPrescaler(static_cast<pwm_clock_prescale_t>(prescalerIndex));
    Sm42.setInitValue(periodStart);
    Sm42.setVal0Value(periodStart);
    Sm42.setVal1Value(periodEnd);

    Sm42.setVal2Value(startChanA);
    Sm42.setVal3Value(stopChanA);

    Sm42.setVal4Value(startChanB);
    Sm42.setVal5Value(stopChanB);
  }

  writeLog(F("-----#3 Printing SM42 register values"));
  Sm42.printRegs();

  uint32_t pwmFrequency = Sm40.pwmFrequency();
  uint32_t pwmMode = Sm40.pwmMode();
  uint16_t deadtimeSettingChanA = Sm40.deadtimeSetting(ChanA);
  uint16_t dutyCycleSettingChanA = Sm40.dutyCycleSetting(ChanA);
  const char *prescale = prescaleStr[Sm40.prescaler()];
  ret = snprintf(strBuf, sizeof(strBuf), "Configured PWM 4.0 %luHz %s md:%lu dtA:%hu dcA:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  pwmFrequency = Sm41.pwmFrequency();
  pwmMode = Sm41.pwmMode();
  deadtimeSettingChanA = Sm41.deadtimeSetting(ChanA);
  dutyCycleSettingChanA = Sm41.dutyCycleSetting(ChanA);
  prescale = prescaleStr[Sm41.prescaler()];
  ret = snprintf(strBuf, sizeof(strBuf), "Configured PWM 4.1 %luHz %s md:%lu dtA:%hu dcA:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  pwmFrequency = Sm42.pwmFrequency();
  pwmMode = Sm42.pwmMode();
  deadtimeSettingChanA = Sm42.deadtimeSetting(ChanA);
  const uint16_t deadtimeSettingChanB = Sm42.deadtimeSetting(ChanB);
  dutyCycleSettingChanA = Sm42.dutyCycleSetting(ChanA);
  const uint16_t dutyCycleSettingChanB = Sm42.dutyCycleSetting(ChanB);
  prescale = prescaleStr[Sm42.prescaler()];
  ret = snprintf(strBuf, sizeof(strBuf), "Configured PWM 4.2 %luHz %s md:%lu dtA:%hu dcA:%hhu%% dtB:%hu dcB:%hhu%%", pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA, deadtimeSettingChanB, dutyCycleSettingChanB);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  writeLog(F("-----#4 Printing SM42 register values"));
  Sm42.printRegs();

  Tm4.setPwmLdok(true);
  Tm4.enable();

  writeLog(F("-----#5 Printing SM42 register values"));
  Sm42.printRegs();

  if (!Sm40.begin()) {
    writeLog(F("Failed to start SM40"));
    exit(EXIT_FAILURE);
  }

  if (!Sm41.begin()) {
    writeLog(F("Failed to start SM41"));
    exit(EXIT_FAILURE);
  }

  Sm42.setPwmLdok(true);

  if (!Sm42.begin(true, true, false)) {
    writeLog(F("Failed to start SM42"));
    exit(EXIT_FAILURE);
  }

  Sm42.enableOutput(ChanA);
  Sm42.enableOutput(ChanB);

  writeLog(F("-----#6 Printing SM42 register values"));
  Sm42.printRegs();

  if (config.Pwm.PrintRegs) {
    writeLog(F("Printing SM40 register values"));
    Sm40.printRegs();
    writeLog(F("Printing SM41 register values"));
    Sm41.printRegs();
    writeLog(F("Printing SM42 register values"));
    Sm42.printRegs();
  }

  writeLog(F("Started TM4"));
}

uint8_t calculateBestPrescaler(uint32_t pwmFrequency) {
  const size_t numPrescalers = sizeof(PrescalerValues) / sizeof(PrescalerValues[0]);

  for (uint8_t i = 0; i < numPrescalers; i++) {
    uint32_t effectiveClock = F_BUS_ACTUAL / PrescalerValues[i];
    uint32_t periodTicks = effectiveClock / pwmFrequency;

    if (periodTicks <= MAX_COUNTER_VALUE) {
      return i;
    }
  }

  return numPrescalers - 1;
}

void attachInterruptVectors() {
  Serial.println(F("Attaching PWM interrupt vectors"));

  if (config.Pwm.Tm2.UseSpwm) {
    attachModule2PwmInterruptVectors();
    enablePwmInterrupts();
  }
}

void attachModule2PwmInterruptVectors() {
  Serial.println(F("Attaching module 2 PWM interrupt vectors"));
  attachInterruptVector(IRQ_FLEXPWM2_0, &IsrOverflowSm20);
}

void enablePwmInterrupts() {
  Serial.println(F("Enabling PWM interrupts"));

  enableModule2PwmInterrupts();
}

void disablePwmInterrupts() {
  Serial.println(F("Disabling PWM interrupts"));

  disableModule2PwmInterrupts();
}

void enableModule2PwmInterrupts() {
  Serial.println(F("Enabling module 2 PWM interrupts"));
  NVIC_ENABLE_IRQ(IRQ_FLEXPWM2_0);
  Sm20.enableInterrupts(kPWM_CompareVal1InterruptEnable);
}

void disableModule2PwmInterrupts() {
  Serial.println(F("Disabling module 2 PWM interrupts"));
  Sm20.disableInterrupts(kPWM_CompareVal1InterruptEnable);
  NVIC_DISABLE_IRQ(IRQ_FLEXPWM2_0);
}

FASTRUN void IsrOverflowSm20() {
  if (!config.Pwm.Tm2.UseSpwm) {
    return;
  }
  static volatile byte pin13_val = 0;
  digitalWriteFast(TriggerPin, pin13_val);
  pin13_val = 1 - pin13_val;

  if (++vSample >= UINT32_MAX) vSample = 0;

  float32_t s = roundf((MidDutyCycle - 1) * arm_sin_f32(vSpwmUpdateSpeed * static_cast<float32_t>(vSample)));

  Tm2.setPwmLdok(false);

  Sm20.updateDutyCycle(static_cast<uint16_t>(MidDutyCycle + s));
  Sm22.updateDutyCycle(static_cast<uint16_t>(MidDutyCycle - s));

  Sm20.clearStatusFlags(kPWM_CompareVal1Flag);

  Tm2.setPwmLdok(true);

  asm volatile("dsb");
}

bool xbarConnect(uint8_t input, uint8_t output) {
  if (input >= 88 || output >= 132) {
    return false;
  }

  char strBuf[LOG_BUF_SIZE];
  uint32_t ret;
  volatile uint16_t *xbar_select_reg = &XBARA1_SEL0 + (output / 2);
  uint16_t val = *xbar_select_reg;

  ret = snprintf(strBuf, sizeof(strBuf), "  Value of XBARA1_SEL%hu (address 0x%" PRIXPTR ") register before writing is 0x%04X", (output / 2), reinterpret_cast<uintptr_t>(&XBARA1_SEL0 + (output / 2)), *xbar_select_reg);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  if (output & 1) {
    val = (val & 0x00FF) | (input << 8);
  } else {
    val = (val & 0xFF00) | input;
  }

  ret = snprintf(strBuf, sizeof(strBuf), "  Writing value 0x%04X to register XBARA1_SEL%hu (address 0x%" PRIXPTR ")", val, (output / 2), reinterpret_cast<uintptr_t>(&XBARA1_SEL0 + (output / 2)));
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  *xbar_select_reg = val;

  ret = snprintf(strBuf, sizeof(strBuf), "  Value of XBARA1_SEL%hu (address 0x%" PRIXPTR ") register after writing is 0x%04X", (output / 2), reinterpret_cast<uintptr_t>(&XBARA1_SEL0 + (output / 2)), *xbar_select_reg);
  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  return true;
}

void enableXbar() {
  writeLog("Enabling XBAR");

  CCM_CCGR2 |= CCM_CCGR2_XBAR1(CCM_CCGR_ON);

  writeLog("XBAR connecting PIT TRIG0 to PWM1.3 EXT_SYNC");
  if (xbarConnect(XBARA1_IN_PIT_TRIGGER0, XBARA1_OUT_FLEXPWM1_PWM3_EXT_SYNC)) {
    writeLog("XBAR connected PIT TRIG0 to PWM1.3 EXT_SYNC");
  } else {
    writeLog("ERROR: XBAR did not connect PIT TRIG0 to PWM1.3 EXT_SYNC");
  }

  writeLog("XBAR connecting PIT TRIG0 to PWM2.0 EXT_SYNC");
  if (xbarConnect(XBARA1_IN_PIT_TRIGGER0, XBARA1_OUT_FLEXPWM2_PWM0_EXT_SYNC)) {
    writeLog("XBAR connected PIT TRIG0 to PWM2.0 EXT_SYNC");
  } else {
    writeLog("ERROR: XBAR did not connect PIT TRIG0 to PWM2.0 EXT_SYNC");
  }

  writeLog("XBAR connecting PIT TRIG0 to PWM3.1 EXT_SYNC");
  if (xbarConnect(XBARA1_IN_PIT_TRIGGER0, XBARA1_OUT_FLEXPWM3_EXT_SYNC1)) {
    writeLog("XBAR connected PIT TRIG0 to PWM3.1 EXT_SYNC");
  } else {
    writeLog("ERROR: XBAR did not connect PIT TRIG0 to PWM3.1 EXT_SYNC");
  }

  writeLog("XBAR connecting PIT TRIG0 to PWM4.0 EXT_SYNC");
  if (xbarConnect(XBARA1_IN_PIT_TRIGGER0, XBARA1_OUT_FLEXPWM4_EXT_SYNC0)) {
    writeLog("XBAR connected PIT TRIG0 to PWM4.0 EXT_SYNC");
  } else {
    writeLog("ERROR: XBAR did not connect PIT TRIG0 to PWM4.0 EXT_SYNC");
  }

  writeLog("Enabled XBAR");
}

void logPwmModuleStats(const char* moduleId, SubModule& pwmModule, bool hasChanB) {
  char strBuf[LOG_BUF_SIZE];
  uint32_t ret;

  uint32_t pwmFrequency = pwmModule.pwmFrequency();
  uint32_t pwmMode = pwmModule.pwmMode();
  uint16_t deadtimeSettingChanA = pwmModule.deadtimeSetting(ChanA);
  uint16_t dutyCycleSettingChanA = pwmModule.dutyCycleSetting(ChanA);
  const char *prescale = prescaleStr[pwmModule.prescaler()];

  if (hasChanB) {
    uint16_t deadtimeSettingChanB = pwmModule.deadtimeSetting(ChanB);
    uint16_t dutyCycleSettingChanB = pwmModule.dutyCycleSetting(ChanB);
    ret = snprintf(strBuf, sizeof(strBuf), "%s %luHz %s md:%lu dtA:%hu dcA:%hhu%% dtB:%hu dcB:%hhu%%", moduleId, pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA, deadtimeSettingChanB, dutyCycleSettingChanB);
  } else {
    ret = snprintf(strBuf, sizeof(strBuf), "%s %luHz %s md:%lu dtA:%hu dcA:%hhu%%", moduleId, pwmFrequency, prescale, pwmMode, deadtimeSettingChanA, dutyCycleSettingChanA);
  }

  if (ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }

  if (config.Pwm.PrintRegs) {
    pwmModule.printRegs();
  }
}

void printStats() {
  logPwmModuleStats("1.3", Sm13, true);
  logPwmModuleStats("2.0", Sm20, true);
  logPwmModuleStats("2.1", Sm21, false);
  logPwmModuleStats("2.2", Sm22, true);
  logPwmModuleStats("2.3", Sm23, true);
  logPwmModuleStats("3.1", Sm31, true);
  logPwmModuleStats("4.0", Sm40, false);
  logPwmModuleStats("4.1", Sm41, false);
  logPwmModuleStats("4.2", Sm42, true);

  char strBuf[LOG_BUF_SIZE];
  if (const uint32_t ret = snprintf(strBuf, sizeof(strBuf), "IP: %d.%d.%d.%d", qindesign::network::Ethernet.localIP()[0], qindesign::network::Ethernet.localIP()[1], qindesign::network::Ethernet.localIP()[2], qindesign::network::Ethernet.localIP()[3]); ret >= sizeof(strBuf)) {
    writeLog("Log buffer overflow!");
  } else {
    writeLog(strBuf);
  }
}