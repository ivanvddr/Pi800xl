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
#pragma once
#include "config.h"
// Framebuffer 8 bit (valore = byte colore Atari: hue<<4 | lum<<1). Condiviso core0 -> core1.
// g_fbState: 0 = libero (core0 puo' scrivere), 1 = frame pronto (core1 deve inviarlo), 2 = in invio
extern uint8_t g_fb[FB_W * FB_H];
extern volatile uint8_t g_fbState;
void videoBegin();   // da chiamare in setup1() (core1)
void videoLoop();    // da chiamare in loop1()

// Aggiornamento parziale: core0 segna i blocchi (TFT_CHUNK_LINES righe) cambiati, core1 invia solo quelli
#define FB_CHUNKS     (FB_H / TFT_CHUNK_LINES)
#define FB_DIRTY_ALL  ((1u << FB_CHUNKS) - 1u)
static_assert(FB_H % TFT_CHUNK_LINES == 0 && FB_CHUNKS <= 32, "TFT_CHUNK_LINES deve dividere FB_H (max 32 blocchi)");
extern volatile uint32_t g_fbDirty;
// Diagnostica (scritte da core1, lette da core0)
extern volatile uint32_t g_videoSendUs, g_videoChunks, g_spiBaud, g_spiBaud1, g_videoBytes;
extern volatile uint32_t g_tConv, g_tWait, g_tPush;   // ripartizione del tempo di invio (us)
extern uint32_t g_cfgSpiHz, g_cfgPio, g_clkPeriHz, g_clkSysHz;   // da User_Setup.h / clock

// Il menu e il browser (core0) disegnano con lo stesso oggetto TFT di core1, ma solo quando core1 e' fermo
// (nessun frame in invio: g_fbState == 0) e dopo videoReady().
class TFT_eSPI;
extern volatile bool g_videoReady;
TFT_eSPI* videoTFT();