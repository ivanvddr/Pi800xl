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
#include <stdint.h>
void audioBegin();
void audioPush(int16_t sample);   // BLOCCANTE quando il buffer I2S e' pieno: tiene l'emulazione a tempo reale
extern volatile uint32_t g_audioBlockedUs;   // microsecondi passati in attesa di I2S (= margine di CPU)