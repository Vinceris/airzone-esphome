// Modbus scan: use it when the central does not answer at 19200 8E1, address 1.
// Tries different baud rates, parities and addresses with a read (function 03, register 0)
// and logs any byte that comes back from the bus. Read-only.
#pragma once
#include <string>
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart_component.h"

namespace azscan {
using namespace esphome;
using esphome::uart::UARTComponent;

struct Cfg { uint32_t baud; uart::UARTParityOptions par; const char *name; };
static const Cfg CFGS[] = {
  {19200, uart::UART_CONFIG_PARITY_EVEN, "19200 8E1"},
  {19200, uart::UART_CONFIG_PARITY_NONE, "19200 8N1"},
  {9600, uart::UART_CONFIG_PARITY_NONE, "9600 8N1"},
  {9600, uart::UART_CONFIG_PARITY_EVEN, "9600 8E1"},
  {38400, uart::UART_CONFIG_PARITY_EVEN, "38400 8E1"},
  {38400, uart::UART_CONFIG_PARITY_NONE, "38400 8N1"},
};
static const int NCFG = sizeof(CFGS) / sizeof(CFGS[0]);
static const int MAXADDR = 10;

static int cfg_i = -1, addr = MAXADDR, sweeps = 0;
static uint32_t total_rx = 0;
static std::string hits;

static uint16_t crc16(const uint8_t *d, size_t n) {
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) c = (c & 1) ? (c >> 1) ^ 0xA001 : c >> 1;
  }
  return c;
}

// called every 300 ms: reads the answer to the previous request, then sends the next one
static void tick(UARTComponent *u, int *out_cfg, int *out_addr) {
  // 1) what came back
  std::string hex;
  uint8_t b;
  int n = 0;
  while (u->available() && n < 64) {
    if (!u->read_byte(&b)) break;
    char t[4];
    snprintf(t, sizeof t, "%02X ", b);
    hex += t;
    n++;
  }
  if (n > 0 && cfg_i >= 0) {
    total_rx += n;
    ESP_LOGW("scan", "ANSWER %s addr %d: %d bytes: %s", CFGS[cfg_i].name, addr, n, hex.c_str());
    char h[48];
    snprintf(h, sizeof h, "%s@%d(%d) ", CFGS[cfg_i].name, addr, n);
    if (hits.size() < 200) hits += h;
  }
  // 2) next combination
  addr++;
  if (addr > MAXADDR) {
    addr = 1;
    cfg_i++;
    if (cfg_i >= NCFG) { cfg_i = 0; sweeps++; ESP_LOGI("scan", "sweep %d done, total bytes received: %u", sweeps, (unsigned) total_rx); }
    u->set_baud_rate(CFGS[cfg_i].baud);
    u->set_parity(CFGS[cfg_i].par);
    u->load_settings(false);
    ESP_LOGI("scan", "trying %s", CFGS[cfg_i].name);
    while (u->available()) u->read_byte(&b);
  }
  uint8_t f[8] = {(uint8_t) addr, 0x03, 0x00, 0x00, 0x00, 0x01, 0, 0};
  uint16_t c = crc16(f, 6);
  f[6] = c & 0xFF;
  f[7] = c >> 8;
  u->write_array(f, 8);
  *out_cfg = cfg_i;
  *out_addr = addr;
}

static std::string summary() {
  char s[64];
  snprintf(s, sizeof s, "sweeps %d, bytes %u: ", sweeps, (unsigned) total_rx);
  return std::string(s) + (hits.empty() ? "no answer" : hits);
}
}  // namespace azscan
