/*
  net

  When handling Pico's WiFi, we consolidated the processing for cumbersome tasks like connection and reconnection.

  Originally created for web servers, it contains elements that are either meaningless or redundant.

  SPDX-License-Identifier: MIT
  SPDX-FileCopyrightText: (C) 2024-2026 mukyokyo
*/

#include <Arduino.h>
#include <LEAmDNS.h>
#include "net.hpp"

static String pass(const char *s) {
  String p;
  for (int i = 0; i < strlen(s); i++) {
    p += '*';
  }
  return p;
}

void CNet::print_stat(void) {
  const char *mode_s[] = { "Off", "AP", "STA" };
  Serial.printf("Net info:\n Hostname is %s\n", NetInfo.hostname); 
  Serial.printf(" Mode is %s\n", mode_s[NetInfo.mode]);
  Serial.printf(" My AP is '%s' with '%s'\n", NetInfo.ssid, pass(NetInfo.psk).c_str());
  Serial.printf(" My IP is %s/%s\n", WiFi.softAPIP().toString().c_str(), WiFi.subnetMask().toString().c_str());
  Serial.printf(" RSSI is %ddBm\n", WiFi.RSSI());
  Serial.printf(" TCP server started at %s:%d\n", WiFi.localIP().toString().c_str(), NetInfo.port);
}

bool CNet::SetWiFiMode(void) {
  if (server != NULL) server->end();
  WiFi.disconnect();
  WiFi.end();

  switch (NetInfo.mode) {
    case 1:
//      Serial.printf("Waiting for connection [%s]...\n", NetInfo.ssid);
      WiFi.mode(WIFI_AP);
      WiFi.setHostname(NetInfo.hostname);
      WiFi.softAPConfig(NetInfo.ip, NetInfo.ip, NetInfo.mask);
      WiFi.softAP(NetInfo.ssid, NetInfo.psk, 1, 0, 1);
      break;
    case 2:
//      Serial.printf("Trying to connect to [%s]...\n", NetInfo.ssid);
      WiFi.mode(WIFI_STA);
      WiFi.setHostname(NetInfo.hostname);
      if (NetInfo.ip != IPAddress(0, 0, 0, 0)) WiFi.config(NetInfo.ip, NetInfo.ip, NetInfo.mask);
      WiFi.begin(NetInfo.ssid, NetInfo.psk);
      break;
    default:
      break;
  }
  pollstat = -1;
  return NetInfo.mode;
}

bool CNet::is_Connected(void) {
  if (NetInfo.mode == 1) return true;
  else return WiFiConnectedDelay->update(WiFi.connected());
}

uint8_t CNet::poll(CLED *led, net_hp_callback *func, void *any) {
  if (server == NULL || NetInfo.mode == 0) return -1;
  MDNS.update();
  switch (pollstat) {
    case -1:
      led->set_pattern(3);
      SetWiFiMode();
      server->end();
      ConnectTime = millis() + WIFI_CONNECTION_ATTEMPT_TIME;
      pollstat = 0;
      break;
    case 0:
      if (!is_Connected()) {
        if (millis() > ConnectTime) pollstat = -1;
      } else {
        if (NetInfo.mode == 1) {
          led->set_pattern(0);
          server->begin();
          server->setNoDelay(true);
//          Serial.printf("Connected to '%s' %ddBm\nTCP server started at %s:%d\n", WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.localIP().toString().c_str(), NetInfo.port);
          MDNS.begin(NetInfo.hostname);
          pollstat = 1;
        } else {
          if (WiFi.RSSI() != 0 && WiFi.RSSI() != -255) {
            led->set_pattern(0);
            server->begin();
            server->setNoDelay(true);
//            Serial.printf("Connected to '%s' %ddBm\nTCP server started at %s:%d\n", WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.localIP().toString().c_str(), NetInfo.port);
            MDNS.begin(NetInfo.hostname);
            pollstat = 1;
          } else {
            if (millis() > ConnectTime) {
              pollstat = -1;
            }
          }
        }
      }
      break;
    case 1:
      if (is_Connected()) {
        if (server->status() == 0) {
          led->set_pattern(7);
          server->end();
          server->begin();
          server->setNoDelay(true);
        } else {
          if (func != NULL) {
            WiFiClient client = server->accept();

            if (client) {
              uint32_t tout = millis() + CLIENT_TIMEOUT_MS;
//              Serial.println("Connection from client..");
              String CurrentLine = "";
              while (client.connected() && (tout > millis())) {

                if (client.available()) {
                  char _c = client.read();
                  Header += _c;
                  if (_c == '\n') {
                    if (CurrentLine.length() == 0) {
                      func(&client, &Header, any);
                      break;
                    } else {  // if you got a newline, then clear currentLine
                      CurrentLine = "";
                    }
                  } else if (_c != '\r') {  // if you got anything else but a carriage return character,
                    CurrentLine += _c;     // add it to the end of the currentLine
                  }
                }
              }
              Header = "";
              client.flush();
            }
          }
        }
      } else pollstat = -1;
      break;
  }
  return pollstat;
}

void CNet::reset(void) {
  if (server != NULL) delete server;
  server = new WiFiServer(NetInfo.port);
}

CNet::CNet() {
  server = NULL;
  pollstat = -1;
  WiFiConnectedDelay = new CDelay(CDelay::tOffDelay, false, 0, WIFI_UNCONNECTED_DURATION_TIME);
}

void CNet::end() {
  if (server != NULL) delete server;
  server = NULL;
  WiFi.disconnect();
  WiFi.end();
  pollstat = -1;
}

uint8_t CNet::begin(const TNetInfo info) {
  end();
  memcpy((void *)&NetInfo, (void *)&info, sizeof(TNetInfo));
  CurrentTime = millis();
  PreviousTime = 0;
  server = new WiFiServer(NetInfo.port);
  pollstat = -1;

  return info.mode;
}