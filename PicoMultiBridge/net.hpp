/*
  net

  When handling Pico's WiFi, we consolidated the processing for cumbersome tasks like connection and reconnection.

  Originally created for web servers, it contains elements that are either meaningless or redundant.

  SPDX-License-Identifier: MIT
  SPDX-FileCopyrightText: (C) 2024-2026 mukyokyo
*/

#pragma once

#include <WiFi.h>
#include <WiFiClient.h>
#include "led.hpp"
#include "delay.hpp"

typedef struct {
  char key[2];          // nvm reserved. don't care !!

  char hostname[64];    // Host name
  uint8_t mode;         // 0:OFF 1:AP 2:STA
  char ssid[64];        // SSID
  char psk[64];         // Passkey
  IPAddress ip;         // IP address
  IPAddress mask;       // Net mask
  uint16_t port;        // Port for client connection

  uint8_t encprotocol;  // 0:OFF 1:PUSR 2:LsrMstInsert
  uint32_t baudrate;    // default baudrate
  char serconfig[10];   // default serial config
} TNetInfo;

typedef void(net_hp_callback)(WiFiClient *cli, String *header, void *any);

class CNet {
  const uint32_t WIFI_CONNECTION_ATTEMPT_TIME = 10000;
  const uint32_t WIFI_UNCONNECTED_DURATION_TIME = 1000;
  const uint32_t CLIENT_TIMEOUT_MS = 10000;

  TNetInfo NetInfo;

  String Header;
  uint32_t CurrentTime;
  uint32_t PreviousTime;
  uint32_t ConnectTime;

  const char *hostname;

  int8_t pollstat;

  CDelay *WiFiConnectedDelay;

  bool SetWiFiMode(void);

public:
  WiFiServer *server;

  void print_stat(void);
  bool is_Connected(void);

  uint8_t poll(CLED *led, net_hp_callback *func, void *any = NULL);

  void reset(void);

  CNet();
  uint8_t begin(TNetInfo info);
  void end(void);
};