/****************************************************************************************************************************
  defines.h
  EthernetWebServer is a library for the Ethernet shields to run WebServer

  Based on and modified from ESP8266 https://github.com/esp8266/Arduino/releases
  Built by Khoi Hoang https://github.com/khoih-prog/EthernetWebServer
  Licensed under MIT license
 ***************************************************************************************************************************************/
#ifndef defines_h
#define defines_h

#define NTP_DEBUG_PORT                      Serial
#define DEBUG_ETHERNET_WEBSERVER_PORT       Serial

// Debug Level from 0 to 4
#define _ETHERNET_WEBSERVER_LOGLEVEL_       3
#define _NTP_LOGLEVEL_                      4

#define USING_SPI2                          false

#ifndef __IMXRT1062__
  #error __IMXRT1062__ not defined
#endif

#ifndef ARDUINO_TEENSY41
  #error ARDUINO_TEENSY41 not defined
#endif

#ifndef TEENSYDUINO
  #error TEENSYDUINO not defined
#endif

#ifndef CORE_TEENSY
  #error CORE_TEENSY not defined
#endif

#if ( defined(CORE_TEENSY) && defined(__IMXRT1062__) && defined(ARDUINO_TEENSY41) )
  // For Teensy 4.1
  #define BOARD_TYPE      "TEENSY 4.1"
  // Use true for NativeEthernet Library, false if using other Ethernet libraries
  #define USE_NATIVE_ETHERNET     false
  #define USE_QN_ETHERNET         true
  #define USE_THIS_SS_PIN         10
#else
  #error Only Teensy 4.1 supported
#endif

#ifndef BOARD_NAME
  #define BOARD_NAME    "TEENSY 4.1"
#endif

// Use true  for ENC28J60 and UIPEthernet library (https://github.com/UIPEthernet/UIPEthernet)
// Use false for W5x00 and Ethernetx library      (https://www.arduino.cc/en/Reference/Ethernet)

#define USE_UIP_ETHERNET      false
#define USE_ETHERNET_GENERIC  false
#define USE_ETHERNET_ESP8266  false
#define USE_ETHERNET_ENC      false
#define USE_CUSTOM_ETHERNET   false

#if USE_NATIVE_ETHERNET
  #include "NativeEthernet.h"
  // #warning Using NativeEthernet lib for Teensy 4.1. Must also use Teensy Packages Patch or error
  #define SHIELD_TYPE           "using NativeEthernet"
#elif USE_QN_ETHERNET
  #include "QNEthernet.h"       // https://github.com/ssilverman/QNEthernet
  using namespace qindesign::network;
  // #warning Using QNEthernet lib for Teensy 4.1. Must also use Teensy Packages Patch or error
  #define SHIELD_TYPE           "using QNEthernet"
#endif

#ifdef USE_NATIVE_ETHERNET

#define USING_DHCP    true

#if !USING_DHCP
  // Set the static IP address to use if the DHCP fails to assign
  IPAddress myIP(192, 168, 2, 222);
  IPAddress myNetmask(255, 255, 255, 0);
  IPAddress myGW(192, 168, 2, 1);
  //IPAddress mydnsServer(192, 168, 2, 1);
  IPAddress mydnsServer(8, 8, 8, 8);
#endif

#endif

#endif    //defines_h
