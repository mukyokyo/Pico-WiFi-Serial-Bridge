/*
  nvm

  The first two bytes and the last byte of the region are used to track the state of the NVM.
  Specifically, the first two bytes must be reserved at the beginning of the data to be written.

  SPDX-License-Identifier: MIT
  SPDX-FileCopyrightText: (C) 2024-2026 mukyokyo
*/

#pragma once

#include <EEPROM.h>
#include "crc8.hpp"

class CSysNVM {
  void _NVMClear(void) {
    for(int i = 0; i < EEPROM.length(); i++) {
      EEPROM.write(i, 0xff);
    }
    EEPROM.commit();
  }

  bool _NVMFullFF(void) {
    for(int i = 0; i < EEPROM.length(); i++) {
      volatile uint8_t n = EEPROM.read(i);
      if (n != 0xff) return false;
    }
    return true;
  }

  uint8_t _NVMCalcCRC(void) {
    CCRC8 CRC8;
    uint8_t crc = 0xff;
    for(int i = 0; i < EEPROM.length() - 1; i++) {
      CRC8.get(&crc, EEPROM.read(i));
    }
    crc = crc ^ 0xff;
    return crc;
  }

  uint8_t _NVMGetCRC(void) {
    return EEPROM.read(EEPROM.length() - 1);
  }

  void _NVMSetCRC(void) {
    EEPROM.write(EEPROM.length() - 1, _NVMCalcCRC());
  }

  bool _NVMCheck(void) {
    if(_NVMGetCRC() == _NVMCalcCRC()) {
      return true;
    } else {
      _NVMClear();
      return false;
    }
  }

public:
  void Init(void) {
    EEPROM.begin(4096);
  }

  void Read(void (*r)(void), void (*d)(void) = NULL) {
    if(_NVMCheck() && !_NVMFullFF()) {
      char n[2];
      EEPROM.get(0, n);
      if('0' <= n[0] && n[0] <= '9' && '0' <= n[1] && n[1] <= '9') {
        if (r != NULL) (*r)();
      } else {
        if (d != NULL) (*d)();
        _NVMSetCRC();
      }
    } else {
      if (d != NULL) (*d)();
      _NVMSetCRC();
    }
  }

  void Write(void (*w)(void)) {
    if (w != NULL) (*w)();
    _NVMSetCRC();
  }

  int Flush(void) {
    if(_NVMCheck() && !_NVMFullFF()) {
      char n[2];
      EEPROM.get(0, n);
      if('0' <= n[0] && n[0] <= '9' && '0' <= n[1] && n[1] <= '9') {
        int v;
        v = (n[0] - '0') * 10 + (n[1] - '0');
        v = (v + 1) % 100;
        n[0] = ((v / 10) % 10) + '0';
        n[1] = (v % 10) + '0';
        EEPROM.put(0, n);
        _NVMSetCRC();
        EEPROM.commit();
        return v;
      } else
        return -1;
    } else
      return -1;
  }
};