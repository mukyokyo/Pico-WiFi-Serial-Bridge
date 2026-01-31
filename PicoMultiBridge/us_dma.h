/*
  us_dma

  UART Transmission and Reception via DMA.

  Incidentally, no ring buffer is configured for transmission.
  Referenced “Copyright (c) 2025 https://github.com/qqqlab”

  SPDX-License-Identifier: MIT
  SPDX-FileCopyrightText: (C) 2026 mukyokyo
*/

#pragma once

#include <stdint.h>
#include <hardware/dma.h>
#include <hardware/uart.h>

class CUartDMA {
  uart_inst_t* seluart;

#if PICO_RP2040
  uint8_t rx_trg_dma_ch;
  uint8_t rx_ctrl_dma_ch;
  uint8_t rx_ctrl_dummy_read;
  uint8_t rx_ctrl_dummy_write;
#endif
  uint8_t rx_dma_ch;
  uint8_t rxbuf_len_pow;
  uint16_t rxbuf_len;
  uint8_t* rxbuf;
  dma_channel_hw_t *rx_dma_hw;

  uint8_t tx_dma_ch;
  uint8_t txbuf_len_pow;
  uint16_t txbuf_len;
  uint8_t* txbuf;
  dma_channel_hw_t *tx_dma_hw;

  void init_dma(int ch);
  uint8_t log_2(uint16_t val);
  uint32_t actualbaudrate;

  inline void clear_err(void) {
    hw_clear_bits(&uart_get_hw(seluart)->rsr, UART_UARTRSR_BITS);
  }

  uint32_t read_ptr;
  bool pop(uint8_t* ch);

public:
  uint32_t begin(uint8_t uart_ch, uint32_t baudrate, uint16_t config, uint16_t txblen, uint16_t rxblen);
  uint32_t begin(uint32_t baudrate, uint16_t config);

  size_t getTxBufferSize(void) { return txbuf_len; }
  size_t getRxBufferSize(void) { return rxbuf_len; }

  size_t write(const uint8_t* data, uint16_t length);
  inline void write(char c) {
    write((const uint8_t*)&c, 1);
  }

  int read(void);
  size_t readBytes(uint8_t* data, uint16_t length);
  size_t available(void);
  size_t availableForWrite(void);
  uint32_t getActualBaud(void);
  void flush(void);

  CUartDMA()
    : seluart(nullptr),
      rxbuf(nullptr),
      txbuf(nullptr),
      rxbuf_len(0),
      txbuf_len(0),
      read_ptr(0) {}
};