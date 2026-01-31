/*
  delay

  Class for easily handling ON/OFF delays.

  SPDX-License-Identifier: MIT
  SPDX-FileCopyrightText: (C) 2024-2026 mukyokyo
*/

#pragma once

class CDelay {
  bool prev_result;
  uint32_t on_delaytime, on_targettime, off_delaytime, off_targettime;
public:
  typedef enum {
    tNone,
    tOnDelay,
    tOffDelay,
    tOnOffDelay
  } TDelayType;

private:
  TDelayType stat;

public:
  bool config(TDelayType t, bool inistat, uint32_t on_delayms, uint32_t off_delayms) {
    uint32_t nowt = millis();
    on_delaytime = on_delayms;
    off_delaytime = off_delayms;
    on_targettime = nowt + on_delaytime;
    off_targettime = nowt + off_delaytime;
    stat = t;
    prev_result = inistat;
    return (inistat);
  }

  CDelay(TDelayType t, bool inistat, uint32_t on_delayms, uint32_t off_delayms) {
    config(t, inistat, on_delayms, off_delayms);
  }

  bool update(bool nowstat) {
    uint32_t nowt = millis();
    bool result = prev_result;
    switch (stat) {
      case tOnDelay:
        if (nowstat) {
          if (nowt >= on_targettime) result = nowstat;
        } else {
          on_targettime = nowt + on_delaytime;
          result = nowstat;
        }
        break;
      case tOffDelay:
        if (!nowstat) {
          if (nowt >= off_targettime) result = nowstat;
        } else {
          off_targettime = nowt + off_delaytime;
          result = nowstat;
        }
        break;
      case tOnOffDelay:
        if (nowstat) {
          if (nowt >= on_targettime) result = nowstat;
          off_targettime = nowt + off_delaytime;
        } else {
          if (nowt >= off_targettime) result = nowstat;
          on_targettime = nowt + on_delaytime;
        }
        break;
      default:
        result = nowstat;
        break;
    }
    prev_result = result;
    return result;
  }
};