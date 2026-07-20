#ifndef WEB_HANDLERS_H
#define WEB_HANDLERS_H

#include <aWOT.h>

void configureWebServer();
void processWebServer();

extern volatile bool configSaveNeeded;

void index(Request &req, Response &res);
void settings_pwm(Request &req, Response &res);
void settings_pwm_update(Request &req, Response &res);
void settings_pwm_timer(Request &req, Response &res);
void settings_pwm_timer_update(Request &req, Response &res);

void DumpText(EthernetClient &client);

static constexpr char PROGMEM PwmTimerSettingsPageTemplate[] = R"delimiter(<!DOCTYPE html>
<html lang="en">
<head>
<title>PWM Timer Settings</title>
<style>
body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }
.input { padding-top: 4px; padding-bottom: 4px; padding-left: 4px; padding-right: 4px; }
.switch {
  position: relative;
  display: inline-block;
  width: 60px;
  height: 34px;
}
.switch input {
  opacity: 0;
  width: 0;
  height: 0;
}
.slider {
  position: absolute;
  cursor: pointer;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background-color: #ccc;
  -webkit-transition: .4s;
  transition: .4s;
}
.slider:before {
  position: absolute;
  content: "";
  height: 26px;
  width: 26px;
  left: 4px;
  bottom: 4px;
  background-color: white;
  -webkit-transition: .4s;
  transition: .4s;
}
input:checked + .slider {
  background-color: #2196F3;
}
input:focus + .slider {
  box-shadow: 0 0 1px #2196F3;
}
input:checked + .slider:before {
  -webkit-transform: translateX(26px);
  -ms-transform: translateX(26px);
  transform: translateX(26px);
}
.slider.round {
  border-radius: 34px;
}
.slider.round:before {
  border-radius: 50%%;
}
</style>
</head>
<body onload="recalcValues()">
<form method="post" enctype="application/x-www-form-urlencoded" action="/settings/pwm-timer/update" accept-charset="utf-8">
    <div>
        <div>
            <h1>
                PWM Timer Settings
            </h1>
            <div>
                <nav>
                    <ol>
                        <li>
                            <a href="/">Home</a>
                        </li>
                        <li>
                            <a href="/settings">Settings</a>
                        </li>
                        <li>
                            PWM Timer
                        </li>
                    </ol>
                </nav>
            </div>
            <hr>
            <p>The periodic timer setting controls the period during which the PWM signal is output. The periods are repeated.</p>
            <div class="input">
                <label>
                    1.3A Period
                </label>
                <input type="number" style="width: 75px;" id="period-13a" name="period-13a" value="%lu"> us
                <label class="switch">
                  <input type="checkbox" id="toggle-13a" name="toggle-13a" value="on"%s>
                  <span class="slider round"></span>
                </label>
            </div>
            <div class="input">
                <label>
                    1.3B Period
                </label>
                <input type="number" style="width: 75px;" id="period-13b" name="period-13b" value="%lu"> us
                <label class="switch">
                  <input type="checkbox" id="toggle-13b" name="toggle-13b" value="on"%s>
                  <span class="slider round"></span>
                </label>
            </div>
        </div>
    </div>
    <br>
    <input type="submit" value="Submit">
</form>
</body>
</html>)delimiter";

static constexpr char PROGMEM PwmSettingsPageTemplate[] = R"delimiter(<!DOCTYPE html>
<html lang="en">
<head>
<title>PWM Settings</title>
<style>
body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }
.input { padding-top: 4px; padding-bottom: 4px; padding-left: 4px; padding-right: 4px; }
</style>
</head>
<body onload="recalcValues()">
<form method="post" enctype="application/x-www-form-urlencoded" action="/settings/pwm/update" accept-charset="utf-8">
    <div>
        <div>
            <h1>
                PWM Settings
            </h1>
            <div>
                <nav>
                    <ol>
                        <li>
                            <a href="/">Home</a>
                        </li>
                        <li>
                            <a href="/settings">Settings</a>
                        </li>
                        <li>
                            PWM
                        </li>
                    </ol>
                </nav>
            </div>
            <hr>
            <p><strong>Periodic TImer</strong></p>
            <p>The periodic timer setting controls the period during which the PWM signal is output. The periods are repeated.</p>
            <div class="input">
                <label>
                    1.3A Period
                </label>
                <input type="number" style="width: 75px;" id="period-13a" name="period-13a" value="%lu"> us
            </div>
            <div class="input">
                <label>
                    1.3B Period
                </label>
                <input type="number" style="width: 75px;" id="period-13b" name="period-13b" value="%lu"> us
            </div>
            <hr>
            <p><strong>PWM Frequency</strong></p>
            <p>The PWM Frequency setting controls how many times per second the PWM signal is generated.</p>
            <div class="input">
                <label>
                    1.3 PWM Frequency
                </label>
                <input type="number" style="width: 75px;" id="pwm-frequency-13" name="pwm-frequency-13" value="%lu" oninput="calcPwmFrequency(this)"> Hz
            </div>
            <div class="input">
                <label>
                    2.0 PWM Frequency
                </label>
                <input type="number" style="width: 75px;" id="pwm-frequency-20" name="pwm-frequency-20" value="%lu" oninput="calcPwmFrequency(this)"> Hz
            </div>
            <div class="input">
                <label>
                    2.1 PWM Frequency
                </label>
                <input type="number" style="width: 75px;" id="pwm-frequency-21" name="pwm-frequency-21" value="%lu" oninput="calcPwmFrequency(this)"> Hz
            </div>
            <div class="input">
                <label>
                    2.2 PWM Frequency
                </label>
                <input type="number" style="width: 75px;" id="pwm-frequency-22" name="pwm-frequency-22" value="%lu" oninput="calcPwmFrequency(this)"> Hz
            </div>
            <div class="input">
                <label>
                    2.3 PWM Frequency
                </label>
                <input type="number" style="width: 75px;" id="pwm-frequency-23" name="pwm-frequency-23" value="%lu" oninput="calcPwmFrequency(this)"> Hz
            </div>
            <div class="input">
                <label>
                    3.1 PWM Frequency
                </label>
                <input type="number" style="width: 75px;" id="pwm-frequency-31" name="pwm-frequency-31" value="%lu" oninput="calcPwmFrequency(this)"> Hz
            </div>
            <div class="input">
                <label>
                    4.0 PWM Frequency
                </label>
                <input type="number" style="width: 75px;" id="pwm-frequency-40" name="pwm-frequency-40" value="%lu" oninput="calcPwmFrequency(this)"> Hz
            </div>
            <div class="input">
                <label>
                    4.1 PWM Frequency
                </label>
                <input type="number" style="width: 75px;" id="pwm-frequency-41" name="pwm-frequency-41" value="%lu" oninput="calcPwmFrequency(this)"> Hz
            </div>
            <div class="input">
                <label>
                    4.2 PWM Frequency
                </label>
                <input type="number" style="width: 75px;" id="pwm-frequency-42" name="pwm-frequency-42" value="%lu" oninput="calcPwmFrequency(this)"> Hz
            </div>
            <hr>
            <p><strong>Dead Time</strong></p>
            <p>This controls how much of a delay in nanoseconds to wait in-between the PWM waveforms.</p>
            <div class="input">
                <label>
                    1.3 Dead Time
                </label>
                <input type="number" style="width: 75px;" id="dead-time-13" name="dead-time-13" value="%u"> ns
            </div>
            <div class="input">
                <label>
                    2.0 Dead Time
                </label>
                <input type="number" style="width: 75px;" id="dead-time-20" name="dead-time-20" value="%u"> ns
            </div>
            <div class="input">
                <label>
                    2.1 Dead Time
                </label>
                <input type="number" style="width: 75px;" id="dead-time-21" name="dead-time-21" value="%u"> ns
            </div>
            <div class="input">
                <label>
                    2.2 Dead Time
                </label>
                <input type="number" style="width: 75px;" id="dead-time-22" name="dead-time-22" value="%u"> ns
            </div>
            <div class="input">
                <label>
                    2.3 Dead Time
                </label>
                <input type="number" style="width: 75px;" id="dead-time-23" name="dead-time-23" value="%u"> ns
            </div>
            <div class="input">
                <label>
                    3.1 Dead Time
                </label>
                <input type="number" style="width: 75px;" id="dead-time-31" name="dead-time-31" value="%u"> ns
            </div>
            <div class="input">
                <label>
                    4.0 Dead Time
                </label>
                <input type="number" style="width: 75px;" id="dead-time-40" name="dead-time-40" value="%u"> ns
            </div>
            <div class="input">
                <label>
                    4.1 Dead Time
                </label>
                <input type="number" style="width: 75px;" id="dead-time-41" name="dead-time-41" value="%u"> ns
            </div>
            <div class="input">
                <label>
                    4.2 Dead Time
                </label>
                <input type="number" style="width: 75px;" id="dead-time-42" name="dead-time-42" value="%u"> ns
            </div>
            <hr>
            <p><strong>Duty Cycle</strong></p>
            <p>The Duty Cycle setting controls how long each waveform is switched on for as a percentage of a single PWM time period. Duty Cycle range is 0-65535 (0-100%%).</p>
            <div class="input">
                <label>
                    1.3A Duty Cycle
                </label>
                <input type="number" style="width: 75px;" id="duty-cycle-13a" name="duty-cycle-13a" value="%u" oninput="calcDutyCycle(this)">
                <text class="input" id="duty-cycle-13a-percent" name="duty-cycle-13a-percent"></text>
                <text class="input" id="duty-cycle-13a-period" name="duty-cycle-13a-period"></text>
                <text class="input" id="duty-cycle-13a-pulse-width" name="duty-cycle-13a-pulse-width"></text>
            </div>
            <div class="input">
                <label>
                    1.3B Duty Cycle
                </label>
                <input type="number" style="width: 75px;" id="duty-cycle-13b" name="duty-cycle-13b" value="%u" oninput="calcDutyCycle(this)">
                <text class="input" id="duty-cycle-13b-percent" name="duty-cycle-13b-percent"></text>
                <text class="input" id="duty-cycle-13b-period" name="duty-cycle-13b-period"></text>
                <text class="input" id="duty-cycle-13b-pulse-width" name="duty-cycle-13b-pulse-width"></text>
            </div>
            <div class="input">
                <label>
                    2.0A Duty Cycle
                </label>
                <input type="number" style="width: 75px;" id="duty-cycle-20a" name="duty-cycle-20a" value="%u" oninput="calcDutyCycle(this)">
                <text class="input" id="duty-cycle-20a-percent" name="duty-cycle-20a-percent"></text>
                <text class="input" id="duty-cycle-20a-period" name="duty-cycle-20a-period"></text>
                <text class="input" id="duty-cycle-20a-pulse-width" name="duty-cycle-20a-pulse-width"></text>
            </div>
            <div class="input">
                <label>
                    2.0B Duty Cycle
                </label>
                <input type="number" style="width: 75px;" id="duty-cycle-20b" name="duty-cycle-20b" value="%u" oninput="calcDutyCycle(this)">
                <text class="input" id="duty-cycle-20b-percent" name="duty-cycle-20b-percent"></text>
                <text class="input" id="duty-cycle-20b-period" name="duty-cycle-20b-period"></text>
                <text class="input" id="duty-cycle-20b-pulse-width" name="duty-cycle-20b-pulse-width"></text>
            </div>
            <div class="input">
                <label>
                    2.1A Duty Cycle
                </label>
                <input type="number" style="width: 75px;" id="duty-cycle-21a" name="duty-cycle-21a" value="%u" oninput="calcDutyCycle(this)">
                <text class="input" id="duty-cycle-21a-percent" name="duty-cycle-21a-percent"></text>
                <text class="input" id="duty-cycle-21a-period" name="duty-cycle-21a-period"></text>
                <text class="input" id="duty-cycle-21a-pulse-width" name="duty-cycle-21a-pulse-width"></text>
            </div>
            <div class="input">
                <label>
                    2.2A Duty Cycle
                </label>
                <input type="number" style="width: 75px;" id="duty-cycle-22a" name="duty-cycle-22a" value="%u" oninput="calcDutyCycle(this)">
                <text class="input" id="duty-cycle-22a-percent" name="duty-cycle-22a-percent"></text>
                <text class="input" id="duty-cycle-22a-period" name="duty-cycle-22a-period"></text>
                <text class="input" id="duty-cycle-22a-pulse-width" name="duty-cycle-22a-pulse-width"></text>
            </div>
            <div class="input">
                <label>
                    2.2B Duty Cycle
                </label>
                <input type="number" style="width: 75px;" id="duty-cycle-22b" name="duty-cycle-22b" value="%u" oninput="calcDutyCycle(this)">
                <text class="input" id="duty-cycle-22b-percent" name="duty-cycle-22b-percent"></text>
                <text class="input" id="duty-cycle-22b-period" name="duty-cycle-22b-period"></text>
                <text class="input" id="duty-cycle-22b-pulse-width" name="duty-cycle-22b-pulse-width"></text>
            </div>
            <div class="input">
                <label>
                    2.3A Duty Cycle
                </label>
                <input type="number" style="width: 75px;" id="duty-cycle-23a" name="duty-cycle-23a" value="%u" oninput="calcDutyCycle(this)">
                <text class="input" id="duty-cycle-23a-percent" name="duty-cycle-23a-percent"></text>
                <text class="input" id="duty-cycle-23a-period" name="duty-cycle-23a-period"></text>
                <text class="input" id="duty-cycle-23a-pulse-width" name="duty-cycle-23a-pulse-width"></text>
            </div>
            <div class="input">
                <label>
                    2.3B Duty Cycle
                </label>
                <input type="number" style="width: 75px;" id="duty-cycle-23b" name="duty-cycle-23b" value="%u" oninput="calcDutyCycle(this)">
                <text class="input" id="duty-cycle-23b-percent" name="duty-cycle-23b-percent"></text>
                <text class="input" id="duty-cycle-23b-period" name="duty-cycle-23b-period"></text>
                <text class="input" id="duty-cycle-23b-pulse-width" name="duty-cycle-23b-pulse-width"></text>
            </div>
            <div class="input">
                <label>
                    3.1A Duty Cycle
                </label>
                <input type="number" style="width: 75px;" id="duty-cycle-31a" name="duty-cycle-31a" value="%u" oninput="calcDutyCycle(this)">
                <text class="input" id="duty-cycle-31a-percent" name="duty-cycle-31a-percent"></text>
                <text class="input" id="duty-cycle-31a-period" name="duty-cycle-31a-period"></text>
                <text class="input" id="duty-cycle-31a-pulse-width" name="duty-cycle-31a-pulse-width"></text>
            </div>
            <div class="input">
                <label>
                    3.1B Duty Cycle
                </label>
                <input type="number" style="width: 75px;" id="duty-cycle-31b" name="duty-cycle-31b" value="%u" oninput="calcDutyCycle(this)">
                <text class="input" id="duty-cycle-31b-percent" name="duty-cycle-31b-percent"></text>
                <text class="input" id="duty-cycle-31b-period" name="duty-cycle-31b-period"></text>
                <text class="input" id="duty-cycle-31b-pulse-width" name="duty-cycle-31b-pulse-width"></text>
            </div>
            <div class="input">
                <label>
                    4.0A Duty Cycle
                </label>
                <input type="number" style="width: 75px;" id="duty-cycle-40a" name="duty-cycle-40a" value="%u" oninput="calcDutyCycle(this)">
                <text class="input" id="duty-cycle-40a-percent" name="duty-cycle-40a-percent"></text>
                <text class="input" id="duty-cycle-40a-period" name="duty-cycle-40a-period"></text>
                <text class="input" id="duty-cycle-40a-pulse-width" name="duty-cycle-40a-pulse-width"></text>
            </div>
            <div class="input">
                <label>
                    4.1A Duty Cycle
                </label>
                <input type="number" style="width: 75px;" id="duty-cycle-41a" name="duty-cycle-41a" value="%u" oninput="calcDutyCycle(this)">
                <text class="input" id="duty-cycle-41a-percent" name="duty-cycle-41a-percent"></text>
                <text class="input" id="duty-cycle-41a-period" name="duty-cycle-41a-period"></text>
                <text class="input" id="duty-cycle-41a-pulse-width" name="duty-cycle-41a-pulse-width"></text>
            </div>
            <div class="input">
                <label>
                    4.2A Duty Cycle
                </label>
                <input type="number" style="width: 75px;" id="duty-cycle-42a" name="duty-cycle-42a" value="%u" oninput="calcDutyCycle(this)">
                <text class="input" id="duty-cycle-42a-percent" name="duty-cycle-42a-percent"></text>
                <text class="input" id="duty-cycle-42a-period" name="duty-cycle-42a-period"></text>
                <text class="input" id="duty-cycle-42a-pulse-width" name="duty-cycle-42a-pulse-width"></text>
            </div>
            <div class="input">
                <label>
                    4.2B Duty Cycle
                </label>
                <input type="number" style="width: 75px;" id="duty-cycle-42b" name="duty-cycle-42b" value="%u" oninput="calcDutyCycle(this)">
                <text class="input" id="duty-cycle-42b-percent" name="duty-cycle-42b-percent"></text>
                <text class="input" id="duty-cycle-42b-period" name="duty-cycle-42b-period"></text>
                <text class="input" id="duty-cycle-42b-pulse-width" name="duty-cycle-42b-pulse-width"></text>
            </div>
            <hr>
            <p><strong>Phase Shift</strong></p>
            <p>The phase shift setting controls the phase shift between 2.0-2.1, 2.0-2.2 & 2.0-2.3.</p>
            <div class="input">
                <label>
                    2.1A Phase Shift
                </label>
                <input type="number" style="width: 75px;" id="phase-shift-21a" name="phase-shift-21a" value="%u">
            </div>
            <div class="input">
                <label>
                    2.2A Phase Shift
                </label>
                <input type="number" style="width: 75px;" id="phase-shift-22a" name="phase-shift-22a" value="%u">
            </div>
            <div class="input">
                <label>
                    2.2B Phase Shift
                </label>
                <input type="number" style="width: 75px;" id="phase-shift-22b" name="phase-shift-22b" value="%u">
            </div>
            <div class="input">
                <label>
                    2.3A Phase Shift
                </label>
                <input type="number" style="width: 75px;" id="phase-shift-23a" name="phase-shift-23a" value="%u">
            </div>
            <div class="input">
                <label>
                    2.3B Phase Shift
                </label>
                <input type="number" style="width: 75px;" id="phase-shift-23b" name="phase-shift-23b" value="%u">
            </div>
            <div class="input">
                <label>
                    4.1A Phase Shift
                </label>
                <input type="number" style="width: 75px;" id="phase-shift-41a" name="phase-shift-41a" value="%u">
            </div>
            <div class="input">
                <label>
                    4.2A Phase Shift
                </label>
                <input type="number" style="width: 75px;" id="phase-shift-42a" name="phase-shift-42a" value="%u">
            </div>
            <div class="input">
                <label>
                    4.2B Phase Shift
                </label>
                <input type="number" style="width: 75px;" id="phase-shift-42b" name="phase-shift-42b" value="%u">
            </div>
            <hr>
            <p><strong>Misc</strong></p>
            <div class="input">
                <label>
                    Print Register Values?
                </label>
                <input type="text" style="width: 75px;" id="print-regs" name="print-regs" value="%s"> Yes / No
            </div>
            <div class="input">
                <label>
                    Synchronise PWM With PIT0 Timer?
                </label>
                <input type="text" style="width: 75px;" id="sync-pwm" name="sync-pwm" value="%s"> Yes / No
            </div>
            <hr>
            <p><strong>Inverter PWM</strong></p>
            <p>This is the configuration for the inverter PWM.</p>
            <p>This is a full bridge SPWM using PWM outputs 2.0A, 2.0B, 2.2A &amp; 2.2B</p>
            <p>The carrier signal of SPWM is usually a triangular wave with a high frequency, generally in several KHz. The modulation signal of SPWM is a sinusoidal waveform with a frequency equal to the desired output voltage frequency (50 or 60 Hz).</p>
            <p>2.0A controls the positive half cycle of the output inverter and should be used by switches 1 & 2.</p>
            <p>2.2A controls the negative half cycle of the output inverter and should be used by switches 3 & 4.</p>
            <p>The SPWM Carrier Signal Frequency setting controls how many times per second the SPWM carrier signal is generated.</p>
            <div class="input">
                <label>
                    Use SPWM
                </label>
                <input type="text" style="width: 75px;" id="use-spwm" name="use-spwm" value="%s"> Yes / No
            </div>
            <div class="input">
                <label>
                    SPWM Carrier Signal Frequency
                </label>
                <input type="number" style="width: 75px;" id="spwm-carrier-signal-frequency" name="spwm-carrier-signal-frequency" value="%lu"> Hz
            </div>
            <p>The SPWM Modulation Frequency setting controls how many times per second the modulation signal is generated. Typical values are 50Hz (Europe) or 60Hz (North America).</p>
            <div class="input">
                <label>
                    SPWM Modulation Frequency
                </label>
                <input type="number" style="width: 75px;" id="spwm-modulation-frequency" name="spwm-modulation-frequency" value="%lu"> Hz
            </div>
            <p>Modulation Scheme: 0 = fixed duty (no modulation), 1 = unipolar SPWM, 2 = bipolar SPWM, 3 = THIPWM (1/6 third-harmonic injection), 4 = level-shifted (see Carrier Disposition), 5 = phase-shifted (alternating 180&deg; carriers).</p>
            <div class="input">
                <label>
                    Modulation Scheme
                </label>
                <input type="number" style="width: 75px;" id="modulation-scheme" name="modulation-scheme" value="%u"> 0-5
            </div>
            <p>Modulation Index in thousandths (1000 = 1.0). Plain SPWM is linear up to 1000; THIPWM up to 1155. Values beyond the linear range clamp to the rails (overmodulation).</p>
            <div class="input">
                <label>
                    Modulation Index
                </label>
                <input type="number" style="width: 75px;" id="modulation-index" name="modulation-index" value="%u"> /1000
            </div>
            <p>Modulation Cells: how many inverter legs are driven, in the order 2.0, 2.2, 2.1, 2.3. Use 2 for a single-phase H-bridge.</p>
            <div class="input">
                <label>
                    Modulation Cells
                </label>
                <input type="number" style="width: 75px;" id="modulation-cells" name="modulation-cells" value="%u"> 1-4
            </div>
            <p>Carrier Disposition (level-shifted scheme only): 0 = PD (all carriers in phase), 1 = POD (carriers below zero in antiphase), 2 = APOD (alternate carriers in antiphase).</p>
            <div class="input">
                <label>
                    Carrier Disposition
                </label>
                <input type="number" style="width: 75px;" id="carrier-disposition" name="carrier-disposition" value="%u"> 0-2
            </div>
            <p>Dead-Time Compensation adds a polarity-signed duty correction of 2&middot;t<sub>d</sub>&middot;f<sub>sw</sub> to remove the crossover distortion dead-time causes at low modulation.</p>
            <div class="input">
                <label>
                    Dead-Time Compensation
                </label>
                <input type="text" style="width: 75px;" id="deadtime-compensation" name="deadtime-compensation" value="%s"> Yes / No
            </div>
            <p>Soft Start ramps the modulation index from its current value to the target over this time. 0 = instant. Also limits the slew rate of closed-loop corrections.</p>
            <div class="input">
                <label>
                    Soft Start
                </label>
                <input type="number" style="width: 75px;" id="soft-start-ms" name="soft-start-ms" value="%u"> ms
            </div>
            <hr>
            <p><strong>Closed-Loop Regulation</strong></p>
            <p>Regulates the modulation index so the feedback pin (a DC voltage proportional to the regulated quantity, e.g. rectified output or DC bus) tracks the setpoint. PI gains are in thousandths of index per volt (Kp) and per volt-second (Ki).</p>
            <div class="input">
                <label>
                    Feedback Enabled
                </label>
                <input type="text" style="width: 75px;" id="feedback-enabled" name="feedback-enabled" value="%s"> Yes / No
            </div>
            <div class="input">
                <label>
                    Setpoint
                </label>
                <input type="number" style="width: 75px;" id="feedback-setpoint-mv" name="feedback-setpoint-mv" value="%lu"> mV
            </div>
            <div class="input">
                <label>
                    Kp
                </label>
                <input type="number" style="width: 75px;" id="feedback-kp" name="feedback-kp" value="%u"> /1000
            </div>
            <div class="input">
                <label>
                    Ki
                </label>
                <input type="number" style="width: 75px;" id="feedback-ki" name="feedback-ki" value="%u"> /1000
            </div>
            <hr>
            <p><strong>Fault Protection</strong></p>
            <p>A transition on the fault pin immediately masks every PWM output (software trip, about 1&micro;s). The trip is latched; saving settings clears it. Wire an overcurrent comparator or thermal switch here.</p>
            <div class="input">
                <label>
                    Fault Trip Enabled
                </label>
                <input type="text" style="width: 75px;" id="fault-enabled" name="fault-enabled" value="%s"> Yes / No
            </div>
            <div class="input">
                <label>
                    Fault Pin
                </label>
                <input type="number" style="width: 75px;" id="fault-pin" name="fault-pin" value="%u">
            </div>
            <div class="input">
                <label>
                    Fault Active High
                </label>
                <input type="text" style="width: 75px;" id="fault-active-high" name="fault-active-high" value="%s"> Yes / No
            </div>
            <hr>
            <p><strong>Asymmetric Induction</strong></p>
            <p>This is the configuration for asymmetric induction.</p>
            <div class="input">
                <label>
                    Enable Asymmetric Induction?
                </label>
                <input type="text" style="width: 75px;" id="enable-asymmetric-induction" name="enable-asymmetric-induction" value="%s"> Yes / No
            </div>
            <div class="input">
                <label>
                    PWM Frequency
                </label>
                <input type="number" style="width: 75px;" id="pwm-frequency-42" name="pwm-frequency-42" value="%lu" oninput="calcPwmFrequency(this)"> Hz
            </div>
            <div class="input">
                <label>
                    Duty Cycle
                </label>
                <input type="number" style="width: 75px;" id="duty-cycle-42a" name="duty-cycle-42a" value="%u" oninput="calcDutyCycle(this)">
                <text class="input" id="duty-cycle-42a-percent" name="duty-cycle-42a-percent"></text>
                <text class="input" id="duty-cycle-42a-period" name="duty-cycle-42a-period"></text>
                <text class="input" id="duty-cycle-42a-pulse-width" name="duty-cycle-42a-pulse-width"></text>
            </div>
            <div class="input">
                <label>
                    Pre Shift Nanos
                </label>
                <input type="number" style="width: 75px;" id="asymmetric-induction-preshiftnanos" name="asymmetric-induction-preshiftnanos" value="%ld"> ns
            </div>
            <div class="input">
                <label>
                    Post Shift Nanos
                </label>
                <input type="number" style="width: 75px;" id="asymmetric-induction-postshiftnanos" name="asymmetric-induction-postshiftnanos" value="%ld"> ns
            </div>
        </div>
    </div>
    <br>
    <input type="submit" value="Submit">
</form>
<script>
    function calcDutyCycle(element) {
      let dutyCycle = element.value;
      dutyCycle = ((dutyCycle / 65535.0)*100.0);
      document.getElementById(element.id + "-percent").innerHTML = "Percent: " + dutyCycle.toFixed(4) + "%%";

      let pwmFrequencyElementId = element.id.replace("duty-cycle", "pwm-frequency").replace(/a|b$/gm,'');

      let pwmVal = document.getElementById(pwmFrequencyElementId).value;
      let period = (1 / pwmVal);
      let periodMs = (period * 1000.0).toFixed(4);
      let periodUs = (period * 1000000.0).toFixed(4);
      let periodNs = (period * 1000000000.0).toFixed(0);
      let pulseWidth = ((dutyCycle / 100.0) * period);
      let pulseWidthMs = (pulseWidth * 1000.0).toFixed(4);
      let pulseWidthUs = (pulseWidth * 1000000.0).toFixed(4);
      let pulseWidthNs = (pulseWidth * 1000000000.0).toFixed(0);

      document.getElementById(element.id + "-period").innerHTML = "Period: " + period.toFixed(4) + "s " + periodMs + "ms " + periodUs + "us " + periodNs + "ns";
      document.getElementById(element.id + "-pulse-width").innerHTML = "Pulse Width: " + pulseWidth.toFixed(4) + "s " + pulseWidthMs + "ms " + pulseWidthUs + "us " + pulseWidthNs + "ns";
    }

    function processDutyCycleElement(channel) {
      let dutyCycleElement = document.getElementById("duty-cycle-" + channel);

      if (typeof(dutyCycleElement) != 'undefined' && dutyCycleElement != null) {
          calcDutyCycle(dutyCycleElement);
      }
    }

    function calcPwmFrequency(element) {
      let pwmVal = element.value;
      let moduleNum = element.id.replace("pwm-frequency-", "");

      const channels = [moduleNum + "a", moduleNum + "b"];

      channels.forEach(processDutyCycleElement);
    }

    function recalcPwm(module) {
      let pwmElement = document.getElementById("pwm-frequency-" + module);

      if (typeof(pwmElement) != 'undefined' && pwmElement != null) {
          calcPwmFrequency(pwmElement);
      }
    }

    function recalcValues() {
        const modules = [13, 20, 21, 22, 23, 31, 40, 41, 42];

        modules.forEach(recalcPwm);
    }
</script>
</body>
</html>)delimiter";

#endif