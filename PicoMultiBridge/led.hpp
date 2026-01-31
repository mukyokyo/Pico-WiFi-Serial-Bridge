/*
  led

  Repeatedly flash the LED in any pattern.

  SPDX-License-Identifier: MIT
  SPDX-FileCopyrightText: (C) 2024-2026 mukyokyo
*/

#pragma once

class CLED {
  int LedPattern;

public:
  void poll(void);
  void set_pattern(int p);

  CLED();
  void begin(void);
};