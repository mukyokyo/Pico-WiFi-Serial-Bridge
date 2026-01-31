/*
  CRC8
  
  CRC Generator Polynomial for 8-SAE-J1850.

  SPDX-License-Identifier: MIT
  SPDX-FileCopyrightText: (C) 2024-2026 mukyokyo
*/

#include "crc8.hpp"

// Peripheral CRCs are used to store data flash, so we'll do the math straight up here.
// Polynomial 0x1d, Ini Value 0xff, Final xor Value 0xff
struct CCRC8_SAE_J1850 {
  uint8_t ary[256];
  constexpr CCRC8_SAE_J1850 () : ary () {
    for (int i = 0; i < 256; i++) {
      uint8_t crc = i;
      for (int b = 0; b < 8; b++)
        if ((crc & 0x80) != 0) crc = (crc << 1) ^ 0x1d; else crc = (crc << 1);
      ary[i] = crc;
    }
  }
} static constexpr CRC8_SAE_J1850;

uint8_t CCRC8::calc (const void *buf, size_t size) {
    uint8_t *data = (uint8_t *)buf;
    uint8_t crc8 = 0xFF;

    while (size-- != 0) crc8 = CRC8_SAE_J1850.ary[crc8 ^ *data++];
    return crc8 ^ 0xff;
}

uint8_t CCRC8::get (uint8_t *crc, uint8_t dat) {
  *crc = CRC8_SAE_J1850.ary[(*crc) ^ dat];
  return *crc;
}