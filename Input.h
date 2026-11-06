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
// Stato letto dalla macchina (core0)
extern uint8_t g_joyA;     // PORTA: bit0 su, bit1 giu', bit2 sx, bit3 dx (0 = premuto); nibble alto = 0xF0
extern uint8_t g_trig0;    // bit0: 0 = FIRE premuto
extern uint8_t g_consol;   // bit0 START, bit1 SELECT, bit2 OPTION (0 = premuto)

enum { EV_KEY, EV_SHIFT, EV_CONSOLE, EV_BREAK, EV_WARM, EV_COLD, EV_LOAD_NEXT, EV_LOAD_PREV, EV_UNLOAD, EV_MENU };
struct InEvent { uint8_t type, code, down; };

void inputBegin();
void inputUpdate();               // legge joystick/pulsanti (1 volta per frame)
bool inputPoll(InEvent& e);       // estrae un evento tastiera
void inputConsoleKb(uint8_t bit, bool down);

// Navigazione per menu/browser, STESSO formato di FunduinoJoystick::read() di PiSMS (0 = premuto):
// bit0 SU, bit1 GIU', bit2 DESTRA, bit3 SINISTRA, bit4 FIRE.  Unisce joystick e frecce/Invio/Spazio della tastiera.
uint8_t inputNavState();