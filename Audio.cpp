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
#include <I2S.h>
#include "config.h"
#include "Audio.h"

static I2S i2s(OUTPUT);
volatile uint32_t g_audioBlockedUs = 0;

void audioBegin() {
  i2s.setBCLK(PIN_I2S_BCLK);          // LRCLK = BCLK + 1
  i2s.setDATA(PIN_I2S_DIN);
  i2s.setBitsPerSample(16);
  i2s.setBuffers(AUDIO_DMA_BUFFERS, AUDIO_DMA_WORDS);
  i2s.begin(AUDIO_SAMPLE_RATE);
}
// Mono duplicato su L/R. La write bloccante e' il "clock master" dell'emulazione.
void audioPush(int16_t s) {
  uint32_t t0 = micros();
  i2s.write(s); i2s.write(s);
  uint32_t dt = micros() - t0;
  if (dt > 12) g_audioBlockedUs += dt;      // una write normale dura ~1-2us: oltre e' attesa del buffer pieno
}