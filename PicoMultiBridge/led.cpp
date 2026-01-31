/*
  led

  Repeatedly flash the LED in any pattern.

  SPDX-License-Identifier: MIT
  SPDX-FileCopyrightText: (C) 2024-2026 mukyokyo
*/

#include <Arduino.h>
#include "led.hpp"

typedef struct {
  bool on;
  uint32_t duration;
} ttled_trg, ptled_trg;

typedef struct {
  uint32_t sz;
  ttled_trg pat[6];
} tled_pattern;

static const tled_pattern ledpattern[] = {
  { 1, { { false, 1000 }, } },
  { 1, { { true, 1000 }, } },
  { 2, { { true, 50 }, { false, 50 }, } },
  { 2, { { true, 200 }, { false, 200 }, } },
  { 2, { { true, 500 }, { false, 500 }, } },
  { 2, { { true, 1000 }, { false, 1000 }, } },
  { 2, { { true, 50 }, { false, 4950 }, } },
  { 4, { { true, 50 }, { false, 150 }, { true, 50 }, { false, 1000 }, } },
};

CLED::CLED() {
  LedPattern = 0;
}

void CLED::begin(void) {
  pinMode(LED_BUILTIN, OUTPUT);
}

void CLED::poll(void) {
  static uint32_t flashtiming = 0;
  static uint8_t flashind = 0;
  static int prev_lptn = -1;

  uint32_t t = millis();

  if (LedPattern >= 0) {
    if (prev_lptn != LedPattern) {
      flashtiming = t;
      flashind = 0;
    }
    tled_pattern *p = (tled_pattern *)&ledpattern[LedPattern];
    if (t > flashtiming) {
      digitalWrite(LED_BUILTIN, p->pat[flashind].on);
      flashtiming = t + p->pat[flashind].duration;
      if (++flashind >= p->sz) flashind = 0;
    }
  } else {
    flashtiming = t;
    flashind = 0;
  }
  prev_lptn = LedPattern;
}

void CLED::set_pattern(int p) {
  /*if (p < 8)*/ LedPattern = p;
}