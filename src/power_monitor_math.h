#ifndef POWER_MONITOR_MATH_H
#define POWER_MONITOR_MATH_H

// Aux power monitor math: INA226 register scaling and TPS25983 IMON
// conversion for the MOSFET driver board's built-in telemetry. No hardware
// dependencies, unit-tested natively.
//
// The driver board (MOSFET_IGBT_Driver_Board_NoDiodes_AC) carries a 10 mOhm
// shunt (R24) in its DC input ahead of the eFuse, an INA226 across it, and
// a TPS25983 whose IMON pin mirrors the eFuse current at 243 uA/A into an
// optional ground-referenced resistor. The INA226 measures TOTAL board
// draw - gate drive, buck, LDO, isolated supplies and logic together.

#include <stdint.h>

// Device-fixed INA226 scale factors (datasheet section 7.5)
constexpr uint32_t Ina226BusLsbNanoVolt = 1250000;  // 1.25 mV/count
constexpr uint32_t Ina226ShuntLsbNanoVolt = 2500;   // 2.5 uV/count
// CAL = 0.00512 / (Current_LSB[A] * Rshunt[Ohm]), datasheet equation 1
constexpr uint64_t Ina226CalNumerator = 5120000000ULL; // 0.00512 scaled by 1e6*1e6

// TPS25983 current-monitor gain, IMON current per output amp (datasheet:
// 243 uA/A typical, 235.3-249.6 over 3-18 A)
constexpr uint32_t Tps25983GimonMicroAmpPerAmp = 243;

// Calibration register from shunt (micro-ohm) and chosen current LSB
// (micro-amp). 10 mOhm / 50 uA -> 10240. Returns 0 when either input is 0
// (caller treats as invalid); values above 0xFFFF are also invalid.
inline uint32_t ina226CalRegister(uint32_t shuntMicroOhm, uint32_t currentLsbMicroAmp) {
  if (shuntMicroOhm == 0 || currentLsbMicroAmp == 0) {
    return 0;
  }
  return static_cast<uint32_t>(
    Ina226CalNumerator / (static_cast<uint64_t>(shuntMicroOhm) * currentLsbMicroAmp));
}

inline bool ina226CalValid(uint32_t shuntMicroOhm, uint32_t currentLsbMicroAmp) {
  const uint32_t cal = ina226CalRegister(shuntMicroOhm, currentLsbMicroAmp);
  return cal >= 1 && cal <= 0xFFFF;
}

// Bus voltage register -> millivolts (LSB 1.25 mV, rounded)
inline uint32_t ina226BusMv(uint16_t raw) {
  return (static_cast<uint32_t>(raw) * 5 + 2) / 4;
}

// Current register (two's complement) -> milliamps, rounded toward nearest
inline int32_t ina226CurrentMa(int16_t raw, uint32_t currentLsbMicroAmp) {
  const int32_t microAmp = static_cast<int32_t>(raw) * static_cast<int32_t>(currentLsbMicroAmp);
  return (microAmp + (microAmp >= 0 ? 500 : -500)) / 1000;
}

// Power register -> milliwatts. Power LSB = 25 * Current_LSB (datasheet
// equation 3): mW = raw * 25 * lsb_uA / 1000 = raw * lsb_uA / 40, rounded.
inline uint32_t ina226PowerMw(uint16_t raw, uint32_t currentLsbMicroAmp) {
  return (static_cast<uint32_t>(raw) * currentLsbMicroAmp + 20) / 40;
}

// Shunt-overvoltage alert limit register for a threshold in milliamps:
// counts = V / 2.5 uV where V = mA*1e-3 * uOhm*1e-6, so counts = mA*uOhm/2500.
// 1500 mA on 10 mOhm -> 15 mV -> 6000. The shunt register and SOL limit are
// SIGNED two's-complement values, so a positive threshold stops at 0x7FFF;
// 0x8000..0xFFFF are negative limits that would trip on an ordinary positive
// reading. 0 means "no alert" to the caller.
inline uint16_t ina226AlertLimitCounts(uint32_t alertMilliAmp, uint32_t shuntMicroOhm) {
  const uint64_t counts = (static_cast<uint64_t>(alertMilliAmp) * shuntMicroOhm + 1250) / 2500;
  return counts > 0x7FFF ? 0x7FFF : static_cast<uint16_t>(counts);
}

// IMON ADC counts -> milliamps through R_IMON (Ohm) at the TPS25983 gain.
// mA = counts * vrefMv * 1e6 / (fullScale * R * G). With the recommended
// R_IMON = 4.53k and a 12-bit, 3.3 V ADC: full scale reads ~2998 mA and one
// count is ~0.73 mA. Returns 0 for a zero/invalid divisor.
inline int32_t imonMilliAmp(uint32_t counts, uint32_t fullScaleCounts, uint32_t vrefMv,
                            uint32_t rimonOhm, uint32_t gimonMicroAmpPerAmp) {
  const uint64_t div = static_cast<uint64_t>(fullScaleCounts) * rimonOhm * gimonMicroAmpPerAmp;
  if (div == 0) {
    return 0;
  }
  const uint64_t num = static_cast<uint64_t>(counts) * vrefMv * 1000000ULL;
  return static_cast<int32_t>((num + div / 2) / div);
}

// INA226 configuration register value: AVG=16, VBUSCT=VSHCT=1.1 ms,
// continuous shunt+bus. One full result every 16*(1.1+1.1) = 35.2 ms, so a
// 100 ms poll always sees a fresh, well-averaged conversion.
constexpr uint16_t Ina226ConfigValue = 0x4527;

// Mask/Enable: shunt-overvoltage alert, latched until read (SOL | LEN)
constexpr uint16_t Ina226MaskSolLatched = 0x8001;
// Alert Function Flag bit in the Mask/Enable register
constexpr uint16_t Ina226MaskAffBit = 0x0010;

#endif
