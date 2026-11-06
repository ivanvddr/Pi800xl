/*
 * Questo file fa parte del progetto Pi800xl.
 *
 * Copyright (C) 2026 Ivan Vettori
 *
 * Questo file è opera originale dell'autore e viene rilasciato
 * sotto licenza GNU General Public License v3.0.
 * Puoi ridistribuirlo e/o modificarlo secondo i termini della GPLv3.
 *
 * Questo programma è distribuito nella speranza che sia utile,
 * ma SENZA ALCUNA GARANZIA; senza neppure la garanzia implicita
 * di COMMERCIABILITÀ o IDONEITÀ PER UN PARTICOLARE SCOPO.
 * Vedi la licenza GPLv3 per maggiori dettagli.
 */
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <math.h>
#include "config.h"
#include "Video.h"
#include "hardware/spi.h"
#include "hardware/clocks.h"

uint8_t g_fb[FB_W * FB_H] __attribute__((aligned(4)));
volatile uint8_t g_fbState = 0;
volatile bool g_videoReady = false;
volatile uint32_t g_fbDirty = FB_DIRTY_ALL;       // al primo invio va mandato tutto
volatile uint32_t g_videoSendUs = 0, g_videoChunks = 0, g_spiBaud = 0, g_spiBaud1 = 0, g_videoBytes = 0;
volatile uint32_t g_tConv = 0, g_tWait = 0, g_tPush = 0;
uint32_t g_cfgSpiHz = 0, g_cfgPio = 0, g_clkPeriHz = 0, g_clkSysHz = 0;

static TFT_eSPI tft;
static uint16_t chunk[2][FB_W * TFT_CHUNK_LINES];
static uint16_t pal[256];            // RGB565 gia' byte-swapped (MSB first sul bus SPI)

TFT_eSPI* videoTFT() { return &tft; }

static void buildPalette() {
  for (int c = 0; c < 256; c++) {
    int hue = c >> 4; float lum = ((c >> 1) & 7) / 7.0f;
    float Y = 0.04f + 0.96f * lum, I = 0, Q = 0;
    if (hue) { float th = (-15.0f + (hue - 1) * 24.0f) * 3.14159265f / 180.0f; I = 0.26f * cosf(th); Q = 0.26f * sinf(th); }
    float r = Y + 0.956f * I + 0.621f * Q, g = Y - 0.272f * I - 0.647f * Q, b = Y - 1.106f * I + 1.703f * Q;
    r = r < 0 ? 0 : r > 1 ? 1 : r; g = g < 0 ? 0 : g > 1 ? 1 : g; b = b < 0 ? 0 : b > 1 ? 1 : b;
    uint16_t v = ((uint16_t)(r * 31) << 11) | ((uint16_t)(g * 63) << 5) | (uint16_t)(b * 31);
    pal[c] = (v >> 8) | (v << 8);
  }
}

// Conversione in RAM (non in flash/XIP): evita di litigare con core0 per la cache della flash
static void __attribute__((section(".time_critical.video"), noinline)) convertLines(const uint8_t* s, uint16_t* d, int n) {
  for (int i = 0; i < n; i += 4) {
    uint32_t w = *(const uint32_t*)(s + i);
    d[i]     = pal[w & 0xFF];
    d[i + 1] = pal[(w >> 8) & 0xFF];
    d[i + 2] = pal[(w >> 16) & 0xFF];
    d[i + 3] = pal[w >> 24];
  }
}

void videoBegin() {
#ifdef SPI_FREQUENCY
  g_cfgSpiHz = SPI_FREQUENCY;
#endif
#ifdef RP2040_PIO_INTERFACE
  g_cfgPio = 1;
#endif
  g_clkPeriHz = clock_get_hz(clk_peri); g_clkSysHz = clock_get_hz(clk_sys);
  buildPalette();
  tft.init();
  tft.setRotation(1);                // 320x240
  tft.setSwapBytes(false);
  tft.fillScreen(TFT_BLACK);
  tft.initDMA();
  __sync_synchronize();
  g_videoReady = true;
}

static uint32_t baudOf(spi_inst_t* s) { return spi_get_hw(s)->cpsr ? spi_get_baudrate(s) : 0; }

void videoLoop() {
  if (g_fbState != 1) { tight_loop_contents(); return; }
  g_fbState = 2;
  const uint32_t mask = g_fbDirty; g_fbDirty = 0;      // core0 non scrive finche' lo stato e' 2
  const uint32_t t0 = micros();
  int sent = 0;
  uint32_t tc = 0, tw = 0, tp = 0;
  if (mask) {
    tft.startWrite();
    for (int c = 0, k = 0; c < FB_CHUNKS; c++) {
      if (!(mask & (1u << c))) continue;
      const int y = c * TFT_CHUNK_LINES;
      uint32_t a = micros();
      convertLines(g_fb + y * FB_W, chunk[k], FB_W * TFT_CHUNK_LINES);   // mentre il DMA precedente e' in corso
      uint32_t b = micros();
      tft.dmaWait();
      uint32_t d = micros();
      tft.pushImageDMA(0, y, FB_W, TFT_CHUNK_LINES, chunk[k]);
      uint32_t e = micros();
      tc += b - a; tw += d - b; tp += e - d;
      k ^= 1; sent++;
    }
    uint32_t w0 = micros();
    tft.dmaWait();
    tw += micros() - w0;
    if (!g_spiBaud && !g_spiBaud1) { g_spiBaud = baudOf(spi0); g_spiBaud1 = baudOf(spi1); }
    tft.endWrite();
  }
  g_tConv = tc; g_tWait = tw; g_tPush = tp;
  g_videoChunks = sent;
  g_videoBytes = (uint32_t)sent * FB_W * TFT_CHUNK_LINES * 2;
  g_videoSendUs = micros() - t0;
  __sync_synchronize();
  g_fbState = 0;                     // core0 puo' tornare a scrivere nel prossimo frame
}