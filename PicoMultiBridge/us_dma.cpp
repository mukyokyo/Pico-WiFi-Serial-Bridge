/*
  us_dma

  UART Transmission and Reception via DMA.

  Incidentally, no ring buffer is configured for transmission.
  Referenced “Copyright (c) 2025 https://github.com/qqqlab”

  SPDX-License-Identifier: MIT
  SPDX-FileCopyrightText: (C) 2026 mukyokyo
*/

#include <arduino.h>
#include <string.h>
#include <stdlib.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <api/HardwareSerial.h>
#include "us_dma.h"

uint32_t CUartDMA::begin(uint8_t uart_ch, uint32_t baudrate, uint16_t config, uint16_t txblen, uint16_t rxblen) {
  seluart = UART_INSTANCE(uart_ch);
  if (seluart != nullptr) {
    actualbaudrate = begin(baudrate, config);
    rxbuf_len_pow = log_2(rxblen);
    txbuf_len_pow = log_2(txblen);
    rxbuf_len = 1 << (rxbuf_len_pow);
    txbuf_len = 1 << (txbuf_len_pow);
    rxbuf = (uint8_t*)aligned_alloc(rxbuf_len, rxbuf_len);
    txbuf = (uint8_t*)aligned_alloc(txbuf_len, txbuf_len);

    init_dma(uart_ch);
    return actualbaudrate;
  }
  return 0;
}

uint8_t CUartDMA::log_2(uint16_t val) {
  uint8_t i = 0;
  val--;
  while (val > 0) {
    i++;
    val >>= 1;
  }
  return i;
}

uint32_t CUartDMA::begin(uint32_t baudrate, uint16_t config) {
  if (seluart != nullptr) {
    int bits, stop;
    uart_parity_t parity;
    switch (config & SERIAL_PARITY_MASK) {
      case SERIAL_PARITY_EVEN:
        parity = UART_PARITY_EVEN;
        break;
      case SERIAL_PARITY_ODD:
        parity = UART_PARITY_ODD;
        break;
      default:
        parity = UART_PARITY_NONE;
        break;
    }
    switch (config & SERIAL_STOP_BIT_MASK) {
      case SERIAL_STOP_BIT_1:
        stop = 1;
        break;
      default:
        stop = 2;
        break;
    }
    switch (config & SERIAL_DATA_MASK) {
      case SERIAL_DATA_5:
        bits = 5;
        break;
      case SERIAL_DATA_6:
        bits = 6;
        break;
      case SERIAL_DATA_7:
        bits = 7;
        break;
      default:
        bits = 8;
        break;
    }
    actualbaudrate = uart_init(seluart, baudrate);
    uart_set_format(seluart, bits, stop, parity);
    return (actualbaudrate);
  }
  return 0;
}

uint32_t CUartDMA::getActualBaud(void) {
  if (seluart != nullptr) return actualbaudrate;
  return 0;
}

void CUartDMA::init_dma(int usch) {
  int dreq_tx_num, dreq_rx_num;
  uart_hw_t* reg;
  if (usch == 0) {
    dreq_tx_num = DREQ_UART0_TX;
    dreq_rx_num = DREQ_UART0_RX;
    reg = uart0_hw;
  } else if (usch == 1) {
    dreq_tx_num = DREQ_UART1_TX;
    dreq_rx_num = DREQ_UART1_RX;
    reg = uart1_hw;
  } else
    return;

#if PICO_RP2040
  rx_dma_ch = dma_claim_unused_channel(true);
  // RP2040 does not have self trigger, use second dma channel to re-trigger rx channel
  rx_trg_dma_ch = dma_claim_unused_channel(true);
  // DMA control to re-trigger uart read channel (performs dummy 1 byte transfer from rx_ctrl_dummy_read to rx_ctrl_dummy_write)
  dma_channel_config trg_config = dma_channel_get_default_config(rx_trg_dma_ch);
  channel_config_set_transfer_data_size(&trg_config, DMA_SIZE_8);
  channel_config_set_read_increment(&trg_config, false);
  channel_config_set_write_increment(&trg_config, false);
  channel_config_set_chain_to(&trg_config, rx_dma_ch);
  channel_config_set_enable(&trg_config, true);
  dma_channel_configure(rx_trg_dma_ch, &trg_config, &rx_ctrl_dummy_write, &rx_ctrl_dummy_read, 1, false);

  // DMA uart read
  dma_channel_config rx_config = dma_channel_get_default_config(rx_dma_ch);
  channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_8);
  channel_config_set_read_increment(&rx_config, false);
  channel_config_set_write_increment(&rx_config, true);
  channel_config_set_ring(&rx_config, true, rxbuf_len_pow);
  channel_config_set_dreq(&rx_config, dreq_rx_num);
  channel_config_set_chain_to(&rx_config, rx_trg_dma_ch);
  channel_config_set_enable(&rx_config, true);
  dma_channel_configure(rx_dma_ch, &rx_config, rxbuf, &reg->dr, rxbuf_len, true);
#else
  // DMA uart read
  rx_dma_ch = dma_claim_unused_channel(true);
  dma_channel_config rx_config = dma_channel_get_default_config(rx_dma_ch);
  channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_8);
  channel_config_set_read_increment(&rx_config, false);
  channel_config_set_write_increment(&rx_config, true);
  channel_config_set_ring(&rx_config, true, rxbuf_len_pow);
  channel_config_set_dreq(&rx_config, dreq_rx_num);
  channel_config_set_enable(&rx_config, true);
  dma_channel_configure(rx_dma_ch, &rx_config, rxbuf, &reg->dr, dma_encode_transfer_count_with_self_trigger(rxbuf_len), true);
  dma_channel_set_irq0_enabled(rx_dma_ch, false);
#endif

  // DMA uart write
  tx_dma_ch = dma_claim_unused_channel(true);
  dma_channel_config tx_config = dma_channel_get_default_config(tx_dma_ch);
  channel_config_set_transfer_data_size(&tx_config, DMA_SIZE_8);
  channel_config_set_read_increment(&tx_config, true);
  channel_config_set_write_increment(&tx_config, false);
  channel_config_set_dreq(&tx_config, dreq_tx_num);
  dma_channel_configure(tx_dma_ch, &tx_config, &reg->dr, txbuf, 1, false);

  rx_dma_hw = dma_channel_hw_addr(rx_dma_ch);
  tx_dma_hw = dma_channel_hw_addr(tx_dma_ch);
}

void CUartDMA::flush(void) {
  if (seluart) {
    clear_err();
    while ((uart_get_hw(seluart)->fr & UART_UARTFR_BUSY_BITS))
      delay(0);
  }
}

size_t CUartDMA::availableForWrite(void) {
  if (seluart) {
    clear_err();
    return txbuf_len - 1 - tx_dma_hw->transfer_count;
  }
  return 0;
}

size_t CUartDMA::write(const uint8_t* data, uint16_t length) {
  if (seluart) {
    clear_err();
    if (length == 0) return 0;
    int l = 0;
    for (int i = 0; i < length; i += l) {
      dma_channel_wait_for_finish_blocking(tx_dma_ch);
      l = min(txbuf_len, length);
      memcpy(txbuf, &data[i], l);
      tx_dma_hw->read_addr = (uintptr_t)txbuf;
      tx_dma_hw->al1_transfer_count_trig = length;
    }
    return length;
  }
  return 0;
}

size_t CUartDMA::available(void) {
  if (seluart) {
    clear_err();
#if PICO_RP2040
  uint16_t idx = (rxbuf_len - rx_dma_hw->transfer_count) & (rxbuf_len - 1);
  
  if (read_ptr <= idx) return idx - read_ptr;
  else return rxbuf_len + idx - read_ptr;
#else
    int s = (int)rxbuf_len - (int)(rx_dma_hw->transfer_count & 0x0fffffff) - (int)read_ptr;
    if (s < 0) s += rxbuf_len;
    return s;
#endif
  }
  return 0;
}

bool CUartDMA::pop(uint8_t* ch) {
  if (seluart) {
    uint32_t current_dma_pos = rx_dma_hw->write_addr - (uint32_t)rxbuf;
    if (read_ptr == current_dma_pos) return false;
    *ch = rxbuf[read_ptr++];
    read_ptr %= rxbuf_len;
    return true;
  }
  return false;
}

int CUartDMA::read(void) {
  if (seluart) {
    while (!available()) delay(0);
    uint8_t c;
    if (pop(&c)) return c;
  }
  return -1;
}

size_t CUartDMA::readBytes(uint8_t* data, uint16_t length) {
  if (seluart) {
    if (length == 0) return 0;
    for (int i = 0; i < length; i++) {
      while (!available()) delay(0);
      pop(&data[i]);
    }
    return length;
  }
  return 0;
}