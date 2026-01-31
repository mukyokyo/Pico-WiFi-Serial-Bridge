/*
  Bridge UART via Wireless or USB

  A simple bridge designed for the Raspberry Pi Pico W/2W, connecting WiFi or USB to UART.
  All configuration is done via the USB port and saved to the NVM.
  Except for the parts decoding PUSR's VCOM UART updates and the IOCTL_SERIAL_LSRMST_INSERT protocol, it is extremely straightforward.
  Unnecessary source code is scattered throughout, but that's just lazy reuse.
  Be careful, as it treats RAM as if it were virtually limitless.

  remark:
    CPU Speed -> 150MHz
    USB Stack -> Pico SDK

  SPDX-License-Identifier: MIT
  SPDX-FileCopyrightText: (C) 2024-2026 mukyokyo
*/

#include <tusb.h>
#include "led.hpp"
#include "net.hpp"
#include "nvm.hpp"
#include "us.h"
#include "us_dma.h"

template< typename typ, std::size_t size > size_t GetNumOfElems(const typ (&array)[size]) {
  return size;
}

#define STRINGIFY(x) #x
#define TOSTRING(x)  STRINGIFY(x)

#define _TX 4
#define _RX 5

#if PICO_RP2040
#define _MAX_BAUDRATE 3000000
#else
#define _MAX_BAUDRATE 9000000
#endif
#define _MIN_BAUDRATE 300

CSysNVM nvm;
CNet Net;
CLED led;
CUartDMA uart1dma;

const char *databits_s = "5678";
const char *parity_s = "NOEMS";
const char *stopbit_s = "122";

const TNetInfo default_netinfo = {
  key: { '0', '0' },            // nvm reserved
  "Pico_WiFi2Serial",           // Host name
  1,                            // WiFi operating mode
  "Pico_WiFi2Serial",           // SSID
  "12345678",                   // PSK
  IPAddress(10, 0, 0, 1),       // Pico's IP address
  IPAddress(255, 255, 255, 0),  // Pico's IP mask
  23,                           // Client connection port

  0,       // Method for including baudrate and configuration in serial data from a PC
  115200,  // boottime baudrate
  "8N1"    // boottime config
};

TNetInfo netinfo;

// For detecting parameter updates by the CDC
uint32_t cdc_baud, cdc_prevbaud;
String cdc_config, cdc_prevconfig;

// Parameter update via WiFi
uint32_t current_baud;
String current_serconfig;

IPAddress clientip;
uint16_t clientport;


// Switching Settings Mode Using the BOOTSEL button
CDelay bootsel_delay(CDelay::tOnOffDelay, false, 500, 50);
bool u2s_config = false;

//---------------------
// etc
//---------------------
// Convert the “8N1” style parameters to the values required by the hardware serial
uint32_t conv_str2serconfig(const char *s, char *d = NULL) {
  struct {
    const char *str;
    uint16_t param;
  } const cparam[]{
    { "5N1", SERIAL_5N1 }, { "6N1", SERIAL_6N1 }, { "7N1", SERIAL_7N1 }, { "8N1", SERIAL_8N1 },
    { "5N2", SERIAL_5N2 }, { "6N2", SERIAL_6N2 }, { "7N2", SERIAL_7N2 }, { "8N2", SERIAL_8N2 },
    { "5E1", SERIAL_5E1 }, { "6E1", SERIAL_6E1 }, { "7E1", SERIAL_7E1 }, { "8E1", SERIAL_8E1 },
    { "5E2", SERIAL_5E2 }, { "6E2", SERIAL_6E2 }, { "7E2", SERIAL_7E2 }, { "8E2", SERIAL_8E2 },
    { "5O1", SERIAL_5O1 }, { "6O1", SERIAL_6O1 }, { "7O1", SERIAL_7O1 }, { "8O1", SERIAL_8O1 },
    { "5O2", SERIAL_5O2 }, { "6O2", SERIAL_6O2 }, { "7O2", SERIAL_7O2 }, { "8O2", SERIAL_8O2 },

    { "5M1", SERIAL_5M1 }, { "6M1", SERIAL_6M1 }, { "7M1", SERIAL_7M1 }, { "8M1", SERIAL_8M1 },
    { "5M2", SERIAL_5M2 }, { "6M2", SERIAL_6M2 }, { "7M2", SERIAL_7M2 }, { "8M2", SERIAL_8M2 },
    { "5S1", SERIAL_5S1 }, { "6S1", SERIAL_6S1 }, { "7S1", SERIAL_7S1 }, { "8S1", SERIAL_8S1 },
    { "5S2", SERIAL_5S2 }, { "6S2", SERIAL_6S2 }, { "7S2", SERIAL_7S2 }, { "8S2", SERIAL_8S2 }
  };
  for (int i = 0; i < GetNumOfElems(cparam); i++) {
    if (strcasecmp(s, cparam[i].str) == 0) {
      if (s != NULL) strcpy(d, cparam[i].str);
      return cparam[i].param;
    }
  }
  if (d != NULL) strcpy(d, "8N1");
  return SERIAL_8N1;
}

//----------------------------------------------------------------
// Decoding from packets including baudrate and other parameters
//----------------------------------------------------------------
// Extracted from USB CDC events
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *p_line_coding) {
  String s = "   ";
  /// p_line_coding->data_bits  < can be 5, 6, 7, 8 or 16
  /// p_line_coding->parity     < 0: None - 1: Odd - 2: Even - 3: Mark - 4: Space
  /// p_line_coding->stop_bits  < 0: 1 stop bit - 1: 1.5 stop bits - 2: 2 stop bits
  cdc_baud = max(min(p_line_coding->bit_rate, _MAX_BAUDRATE), _MIN_BAUDRATE);
  s[0] = databits_s[max(min(p_line_coding->data_bits, 8), 5) - 5];
  s[1] = parity_s[max(min(p_line_coding->parity, 4), 0)];
  s[2] = stopbit_s[max(min(p_line_coding->stop_bits, 2), 0)];
  cdc_config = s;
}

// Extracted from PUSR's proprietary implementation
bool PUSR_portconfig_check(uint8_t *p) {
  uint32_t baud = 0;
  uint16_t config = 0;
  static uint32_t prevbaud = 0;
  static uint16_t prevconf = 0;

  String s = "   ";

  if (p[0] == 0x55) {
    if (p[1] == 0xaa) {
      if (p[2] == 0x55) {
        if ((uint8_t)(p[3] + p[4] + p[5] + p[6]) == p[7]) {
          baud = max(min((p[3] << 16) | (p[4] << 8) | p[5], _MAX_BAUDRATE), _MIN_BAUDRATE);
          union {
            struct {
              uint8_t databits : 2;    // 0:5bit 1:6bit 2:7bit 3:8bit (LSB side)
              uint8_t stopbits : 1;    // 0:1bit 1:2bit
              uint8_t parityen : 1;    // 0:di 1:en
              uint8_t paritytype : 2;  // 0:ODD 1:EVEN 2:MARK 3:SPACE
            };
            uint8_t byte;
          } bitset;
          bitset.byte = p[6];

          s[0] = databits_s[bitset.databits];
          s[1] = parity_s[(bitset.parityen == 1) ? bitset.paritytype + 1 : 0];
          s[2] = stopbit_s[bitset.stopbits];
          char tmp[10];
          config = conv_str2serconfig(s.c_str(), tmp);
          // If change requests occur frequently, ignore them if no changes are needed from the current state.
          if (prevconf != config || prevbaud != baud) {
            uart1dma.flush();
            uart1dma.begin(baud, config);
            Serial.printf("Update UART to %ubps %s\n", uart1dma.getActualBaud(), tmp);
            current_baud = baud;
            current_serconfig = s;
            prevconf = config;
            prevbaud = baud;
          }
          return true;
        }
      }
    }
  }
  return false;
}

// Extracted from information inserted based on Windows IOCTL
void LSRMSTINS_portconfig_check(uint8_t *pBuf, int len) {

  #define SERIAL_LSRMST_ESCAPE ((uint8_t)0x00)
  #define SERIAL_LSRMST_LSR_DATA ((uint8_t)0x01)
  #define SERIAL_LSRMST_LSR_NODATA ((uint8_t)0x02)
  #define SERIAL_LSRMST_MST ((uint8_t)0x03)
  #define C0CE_INSERT_RBR ((uint8_t)16)
  #define C0CE_INSERT_RLC ((uint8_t)17)

  uint32_t baud = 0;
  uint32_t config = 0;
  static uint32_t prevbaud = 0;
  static uint32_t prevconf = 0;
  static String s = "   ";

  const char escapeChar = 'a';
  uint8_t baud_byte[sizeof(uint32_t)];
  static int state = 0;
  static uint8_t code = 0;
  static int subState = 0;

  uint32_t _baudrate;
  int _byteSize;
  int _parity;
  int _stopBits;

  uint8_t mybuf[len];
  int mylen = 0;

  for (; len; len--) {
    uint8_t ch = *pBuf++;

    switch (state) {
      case 0:
        break;
      case 1:
        code = ch;
        state++;
      case 2:
        switch (code) {
          case SERIAL_LSRMST_ESCAPE:
            mybuf[mylen++] = escapeChar;
            state = subState = 0;
            break;
          case SERIAL_LSRMST_LSR_DATA:
            if (subState == 0) subState++;
            else if (subState == 1) subState++;
            else if (subState == 2) state = subState = 0;
            else state = subState = 0;
            break;
          case SERIAL_LSRMST_LSR_NODATA:
            if (subState == 0) subState++;
            else if (subState == 1) state = subState = 0;
            else state = subState = 0;
            break;
          case SERIAL_LSRMST_MST:
            if (subState == 0) subState++;
            else if (subState == 1) state = subState = 0;
            else state = subState = 0;
            break;
          case C0CE_INSERT_RBR:
            if (subState == 0) {
              subState++;
            } else if (subState >= 1 && subState < (int)(sizeof(uint32_t) + 1)) {
              baud_byte[subState - 1] = ch;
              if (subState < (int)sizeof(uint32_t)) {
                subState++;
              } else {
                baud = max(min((long)*(uint32_t *)baud_byte, _MAX_BAUDRATE), _MIN_BAUDRATE);
                Serial.printf("BaudRate=%lu\n", baud);
                if (baud != prevbaud) {
                  uart1dma.write(mybuf, mylen);
                  mylen = 0;
                  uart1dma.flush();
                  uart1dma.begin(baud, prevconf);
                  Serial.printf("Update UART to *%ubps %s\n", uart1dma.getActualBaud(), s.c_str());
                  current_baud = baud;
                  prevbaud = baud;
                }
                state = subState = 0;
              }
            } else
              state = subState = 0;
            break;
          case C0CE_INSERT_RLC:
            if (subState == 0) {
              subState++;
            } else if (subState == 1) {
              _byteSize = ch & 0xFF;
              subState++;
            } else if (subState == 2) {
              _parity = ch & 0xFF;
              subState++;
            } else if (subState == 3) {
              _stopBits = ch & 0xFF;
              Serial.printf("ByteSize=%d Parity=%d StopBits=%d\n", _byteSize, _parity, _stopBits);
              s[0] = databits_s[max(min(_byteSize - 5, 3), 0)];
              s[1] = parity_s[max(min(_parity, 4), 0)];
              s[2] = stopbit_s[max(min(_stopBits, 2), 0)];
              char tmp[10];
              config = conv_str2serconfig(s.c_str(), tmp);
              if (config != prevconf) {
                uart1dma.write(mybuf, mylen);
                mylen = 0;
                uart1dma.flush();
                uart1dma.begin(prevbaud, config);
                Serial.printf("Update UART to %ubps *%s\n", uart1dma.getActualBaud(), tmp);
                current_serconfig = s;
                prevconf = config;
              }
              state = subState = 0;
            } else
              state = subState = 0;
            break;
          default:
            state = subState = 0;
            break;
        }
        continue;

      default:
        state = subState = 0;
    }
    if (ch == escapeChar) {
      state = 1;
      continue;
    }
    mybuf[mylen++] = ch;
  }
  uart1dma.write(mybuf, mylen);
}

//----------------------------------------------------------------
// setup
//----------------------------------------------------------------
void setup() {
  nvm.Init();
  led.begin();
  Serial.begin(115200);
  Serial.ignoreFlowControl();

  nvm.Read(
    [] {
      EEPROM.get(0, netinfo);
    },
    [] {
      EEPROM.put(0, default_netinfo);
      EEPROM.get(0, netinfo);
    });

  Net.end();
  if (netinfo.mode != 0) Net.begin(netinfo);
}

void setup1() {
  delay(500);
  gpio_pull_up(_RX);

  // Initialize the DMA UART1 class
  gpio_set_function(_TX, GPIO_FUNC_UART);
  gpio_set_function(_RX, GPIO_FUNC_UART);
  current_serconfig = netinfo.serconfig;
  uart1dma.begin(1, (current_baud = max(min(netinfo.baudrate, _MAX_BAUDRATE), _MIN_BAUDRATE)), conv_str2serconfig(current_serconfig.c_str()), 2048, 2048);
}

//----------------------------------------------------------------
// loop
//----------------------------------------------------------------
void loop() {
  const char *serprot_s[] = { "Off", "PUSR", "LsrMstIns" };
  String s;
  char b[10];
  uint8_t mode = 0;
  char bu[5][64];
  uint16_t port = 0;
  IPAddress ip;
  uint8_t protocol = 0;
  int baudrate = 115200;
  char bc[10];

  int available = 0;

  if (netinfo.mode == 0) {
    if (u2s_config) {
      available = Serial.available();
    } else
      available = 0;
  } else
    available = Serial.available();

  if (available > 0) {
    char c = Serial.read();
    switch (c) {
      case '\33':
        Serial.printf("\x1b[2J");
        break;

      // Reboot
      case '#':
        Net.end();
        tud_disconnect();
        delay(250);
        watchdog_enable(1, 1);
        while (1)
          ;
        break;
      // Switch to the bootloader
      case '!':
        Net.end();
        tud_disconnect();
        delay(250);
        reset_usb_boot(0, 0);
        while (1)
          ;
        break;
      // Network status
      case 'i':
        us_rx_flush();
        Net.print_stat();
        if (clientip != IPAddress(0, 0, 0, 0)) Serial.printf(" Client connection is %s:%d\n", clientip.toString().c_str(), clientport);
        Serial.printf(" UART protocol is %s\n", serprot_s[netinfo.encprotocol]);
        Serial.printf(" UART is %lubps %s\n", (netinfo.mode == 0) ? cdc_baud : current_baud, (netinfo.mode == 0) ? cdc_config.c_str() : current_serconfig.c_str());
        Serial.printf(" actual UART is %lubps %s\n", uart1dma.getActualBaud(), (netinfo.mode == 0) ? cdc_config.c_str() : current_serconfig.c_str());
        break;
      // Format
      case 'f':
        Serial.println("Format");
        if (are_you_sure()) {
          nvm.Write(
            [] {
              EEPROM.put(0, default_netinfo);
            });
          nvm.Flush();
        }
        break;

      // Configure network and uart settings from the terminal
      case 's':
        Serial.printf("Configure system settings\n");
        memset(bu, 0, sizeof(bu));
        memset(b, 0, sizeof(b));
        memset(bc, 0, sizeof(bc));
        Serial.print("Select WiFi mode (0:Off 1:AP 2:STA)=");
        if (us_gets(b, sizeof(b)) > 0 && strlen(b) > 0) {
          s = b;
          mode = s.toInt();
          if (2 >= mode && mode >= 0) {
            if (mode != 0) {
              Serial.print("hostname=");
              us_gets(bu[0], sizeof(bu[0]));
              Serial.print("ssid=");
              us_gets(bu[1], sizeof(bu[1]));
              Serial.print("psk=");
              us_gets(bu[2], sizeof(bu[2]));
              Serial.printf("ip%s=", (mode == 2) ? "(If blank, use DHCP)" : "");
              us_gets(bu[3], sizeof(bu[3]));
              Serial.printf("mask%s=", (mode == 2) ? "(If blank, use DHCP)" : "");
              us_gets(bu[4], sizeof(bu[4]));
              Serial.print("port(0..65535)=");
              if (us_gets(b, sizeof(b)) > 0) {
                s = b;
                port = max(min(s.toInt(), 65535), 0);
              }
              Serial.print("serial protocol (0:Off, 1:PUSR, 2:LsrMstIns)=");
              if (us_gets(b, sizeof(b)) > 0) {
                s = b;
                protocol = max(min(s.toInt(), 2), 0);
              } else
                protocol = 0;
            }

            Serial.print("serial baudrate(" TOSTRING(_MIN_BAUDRATE) "..." TOSTRING(_MAX_BAUDRATE) ")=");
            if (us_gets(b, 7) > 0) {
              s = b;
              baudrate = max(min(s.toInt(), _MAX_BAUDRATE), _MIN_BAUDRATE);
            } else
              baudrate = 115200;
            Serial.print("serial config(ex.8N1)=");
            us_gets(bc, 3);
            conv_str2serconfig(bc, bc);

            Serial.println("Input values");
            Serial.printf(" hostname:%s\n", bu[0]);
            Serial.printf(" mode:%d\n", mode);
            Serial.printf(" ssid:%s\n", bu[1]);
            Serial.printf(" psk :%s\n", pass(bu[2]).c_str());//bu[2]);
            if (strlen(bu[3]) == 0) strcpy(bu[3], "0.0.0.0");
            ip.fromString(bu[3]);
            Serial.printf(" ip  :%s\n", ip.toString().c_str());
            if (strlen(bu[4]) == 0) strcpy(bu[4], "0.0.0.0");
            ip.fromString(bu[4]);
            Serial.printf(" mask:%s\n", ip.toString().c_str());
            Serial.printf(" port:%d\n", port);
            Serial.printf(" serial protocol:%d\n", protocol);
            Serial.printf(" serial baudrate:%lu\n", baudrate);
            Serial.printf(" serial config:%s\n", bc);
            if (are_you_sure()) {
              netinfo.mode = mode;
              strncpy(netinfo.hostname, bu[0], sizeof(netinfo.hostname) - 1);
              strncpy(netinfo.ssid, bu[1], sizeof(netinfo.ssid) - 1);
              strncpy(netinfo.psk, bu[2], sizeof(netinfo.psk) - 1);
              netinfo.ip.fromString(bu[3]);
              netinfo.mask.fromString(bu[4]);
              netinfo.port = port;
              netinfo.encprotocol = protocol;
              netinfo.baudrate = baudrate;
              strncpy(netinfo.serconfig, bc, sizeof(netinfo.serconfig) - 1);

              nvm.Write(
                [] {
                  EEPROM.put(0, netinfo);
                });
              nvm.Flush();

              tud_disconnect();
              delay(250);
              watchdog_enable(1, 1);
              while (1)
                ;
            } else
              Serial.printf("canceled\n");
          } else
            Serial.printf("canceled\n");
        } else
          Serial.printf("canceled\n");
        break;
      // Contents of the configuration file
      case 'g':
        us_rx_flush();
        Serial.println("System settings");
        nvm.Read(
          [] {
            EEPROM.get(0, netinfo);
          },
          [] {
            EEPROM.put(0, default_netinfo);
            EEPROM.get(0, netinfo);
          });
        Serial.printf(" hostname:%s\n", netinfo.hostname);
        Serial.printf(" mode:%d\n", netinfo.mode);
        Serial.printf(" ssid:%s\n", netinfo.ssid);
        Serial.printf(" psk :%s\n", pass(netinfo.psk).c_str());
        Serial.printf(" ip  :%s\n", netinfo.ip.toString().c_str());
        Serial.printf(" mask:%s\n", netinfo.mask.toString().c_str());
        Serial.printf(" port:%d\n", netinfo.port);
        Serial.printf(" protocol:  %d\n", netinfo.encprotocol);
        Serial.printf(" baudrate:  %lu\n", netinfo.baudrate);
        Serial.printf(" serconfig: %s\n", netinfo.serconfig);
        break;
      default:
        Serial.println(
          "Command list\n"
          " !:bootloader #:reboot i:system status\n"
          " l:file list f:format s:system setting g:print settings");
        break;
    }
  }

  // Network condition monitoring and reaction
  if (netinfo.mode != 0) Net.poll(&led, NULL);
  else delay(200);

  // Led
  led.poll();
}

void loop1() {
  bool lon = false;
  static uint32_t blink_t = 0;
  static uint8_t buf[2048];
  size_t l, ll;
  static bool prevbootsel = false;

  // WiFi Off (USB <-> UART Bridge)
  if (netinfo.mode == 0) {
    if (bootsel_delay.update(BOOTSEL)) {
      if (!prevbootsel) {
        u2s_config = !u2s_config;
        Serial.printf("%s\n", u2s_config ? "Enter config mode" : "Exit config mode");
        prevbootsel = true;
        led.set_pattern(u2s_config ? 4 : -1);
      }
    } else
      prevbootsel = false;

    // Not in configuration mode
    if (!u2s_config) {
      // USB rx -> UART tx
      while ((l = Serial.available()) > 0) {
        while ((ll = Serial.readBytes(buf, min(sizeof(buf), l))) > 0) {
          uart1dma.write(buf, ll);
          lon = true;
          l -= ll;
        }
        uart1dma.flush();
      }
      // UART rx -> USB tx
      while ((l = uart1dma.available()) > 0) {
        while ((ll = uart1dma.readBytes(buf, min(sizeof(buf), l))) > 0) {
          Serial.write(buf, ll);
          Serial.flush();
          lon = true;
          l -= ll;
        }
      }
      // Detection of baudrate or parameter changes
      uint32_t b = cdc_baud;
      String c = cdc_config;
      if (b != cdc_prevbaud || c != cdc_prevconfig) {
        uart1dma.flush();
        uart1dma.begin(b, conv_str2serconfig(c.c_str()));
        cdc_prevbaud = b;
        cdc_prevconfig = c;
      }
      if (lon) {
        blink_t = millis() + 10;
        digitalWrite(LED_BUILTIN, 1);
        lon = false;
      }
      if (millis() > blink_t) digitalWrite(LED_BUILTIN, 0);
    } else
      delay(2000);
  // WiFi On (WiFi <-> UART Bridge)
  } else if (Net.server != NULL) {
    if (Net.server->status() != 0) led.set_pattern(6);

    // Check for incoming client connections
    WiFiClient client = Net.server->accept();
    Net.server->setNoDelay(true);
  
    if (client) {
      Serial.println("Client connected");
      led.set_pattern(-1);
      client.setNoDelay(true);
      clientip = client.remoteIP();
      clientport = client.remotePort();
      while (client.connected()) {
        // WiFi rx -> UART tx
        while ((l = client.available()) > 0) {
          while ((ll = client.readBytes(buf, min(sizeof(buf), l))) > 0) {
            switch (netinfo.encprotocol) {
              case 0: // no encode
                uart1dma.write(buf, ll);
                lon = true;
                break;
              case 1: // PUSR encode
                for (int j = 0; j < ll;) {
                  if (j + 8 <= ll) {
                    if (PUSR_portconfig_check(&buf[j])) {
                      j += 8;
                      continue;
                    }
                  }
                  uart1dma.write(buf[j++]);
                }
                lon = true;
                break;
              case 2: // LsrMstIns encode
                LSRMSTINS_portconfig_check(buf, ll);
                lon = true;
                break;
            }
            l -= ll;
          }
        }
        // UART rx -> WiFi tx
        while ((l = uart1dma.available()) > 0) {
          lon = true;
          while ((ll = uart1dma.readBytes(buf, min(sizeof(buf), l))) > 0) {
            client.write((uint8_t *)buf, ll);
            l -= ll;
          }
        }

        if (lon) {
          blink_t = millis() + 10;
          digitalWrite(LED_BUILTIN, 1);
          lon = false;
        }
        if (millis() > blink_t) digitalWrite(LED_BUILTIN, 0);
      }
      led.set_pattern(0);
      clientip = IPAddress(0, 0, 0, 0);
      clientport = 0;
      // Client disconnected
      client.stop();
      Serial.println("Client disconnected");
    }
  }
}