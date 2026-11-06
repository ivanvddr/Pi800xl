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
#include "config.h"
#include "Input.h"

uint8_t g_joyA = 0xFF, g_trig0 = 1, g_consol = 7;

// ---------------------------------------------------------------------------
//  Coda eventi (ISR PS/2 -> loop core0)
// ---------------------------------------------------------------------------
static volatile InEvent q[32];
static volatile uint8_t qh = 0, qt = 0;
static void qpush(uint8_t type, uint8_t code, uint8_t down) {
  uint8_t n = (qh + 1) & 31; if (n == qt) return;
  q[qh].type = type; q[qh].code = code; q[qh].down = down; qh = n;
}
bool inputPoll(InEvent& e) {
  if (qt == qh) return false;
  e.type = q[qt].type; e.code = q[qt].code; e.down = q[qt].down; qt = (qt + 1) & 31; return true;
}

// ---------------------------------------------------------------------------
//  Mappa PS/2 set 2 -> codice tastiera Atari (0xFF = nessuno). Layout fisico Atari.
// ---------------------------------------------------------------------------
static uint8_t amap[128];
struct Ov { uint8_t ps2, atari; };      // override quando SHIFT e' premuto (bit6 = shift Atari)
static const Ov ovShift[] = {
  {0x1E, 0x40 | 0x35},  // @  -> shift+8
  {0x36, 0x40 | 0x07},  // ^  -> shift+*
  {0x3D, 0x40 | 0x1B},  // &  -> shift+6
  {0x3E, 0x07},         // *  -> *
  {0x55, 0x06},         // +  -> +
  {0x41, 0x36},         // <  -> <
  {0x49, 0x37},         // >  -> >
  {0x52, 0x40 | 0x1E},  // "  -> shift+2
  {0x5D, 0x40 | 0x0F},  // |  -> shift+=
};
static void buildMap() {
  memset(amap, 0xFF, sizeof(amap));
  const struct { uint8_t p, a; } t[] = {
    {0x1C,0x3F},{0x32,0x15},{0x21,0x12},{0x23,0x3A},{0x24,0x2A},{0x2B,0x38},{0x34,0x3D},{0x33,0x39},{0x43,0x0D},
    {0x3B,0x01},{0x42,0x05},{0x4B,0x00},{0x3A,0x25},{0x31,0x23},{0x44,0x08},{0x4D,0x0A},{0x15,0x2F},{0x2D,0x28},
    {0x1B,0x3E},{0x2C,0x2D},{0x3C,0x0B},{0x2A,0x10},{0x1D,0x2E},{0x22,0x16},{0x35,0x2B},{0x1A,0x17},
    {0x16,0x1F},{0x1E,0x1E},{0x26,0x1A},{0x25,0x18},{0x2E,0x1D},{0x36,0x1B},{0x3D,0x33},{0x3E,0x35},{0x46,0x30},{0x45,0x32},
    {0x4E,0x0E},{0x55,0x0F},{0x5D,0x46},{0x66,0x34},{0x29,0x21},{0x0D,0x2C},{0x58,0x3C},{0x5A,0x0C},{0x76,0x1C},
    {0x41,0x20},{0x49,0x22},{0x4A,0x26},{0x4C,0x02},{0x52,0x40|0x33},{0x54,0x40|0x20},{0x5B,0x40|0x22},
    {0x0E,0x11},   // ` -> HELP
    {0x0B,0x11},   // F6 -> HELP
    {0x11,0x27},   // Alt -> tasto Atari (inverse)
    {0x0A,0x27},   // F8 -> tasto Atari
  };
  for (auto& e : t) amap[e.p] = e.a;
}

static volatile uint8_t navKb = 0xFF;      // frecce / Invio / Spazio (0 = premuto), stesso formato di inputNavState()
static bool shiftDown = false, ctrlDown = false;
static uint8_t heldPs2 = 0;

static void onKey(uint8_t code, bool ext, bool down) {
  {   // stato di navigazione per i menu
    uint8_t nb = 0;
    if (ext) { if (code == 0x75) nb = 0x01; else if (code == 0x72) nb = 0x02; else if (code == 0x74) nb = 0x04; else if (code == 0x6B) nb = 0x08; }
    else if (code == 0x5A || code == 0x29) nb = 0x10;
    if (nb) { if (down) navKb &= ~nb; else navKb |= nb; }
  }
  // modificatori
  if (!ext && (code == 0x12 || code == 0x59)) { shiftDown = down; qpush(EV_SHIFT, 0, down); return; }
  if (code == 0x14) { ctrlDown = down; return; }
  if (!ext) {
    if (code == 0x06) { qpush(EV_CONSOLE, 0, down); return; }   // F2 START
    if (code == 0x04) { qpush(EV_CONSOLE, 1, down); return; }                      // F3 SELECT
    if (code == 0x0C) { qpush(EV_CONSOLE, 2, down); return; }                      // F4 OPTION
    if (code == 0x03) { if (down) qpush(shiftDown ? EV_COLD : EV_WARM, 0, 1); return; }  // F5 reset
    if (code == 0x83) { if (down) qpush(EV_BREAK, 0, 1); return; }                 // F7 BREAK
    if (code == 0x01) { if (down) qpush(EV_LOAD_NEXT, 0, 1); return; }             // F9  media successivo
    if (code == 0x09) { if (down) qpush(EV_LOAD_PREV, 0, 1); return; }             // F10 media precedente
    if (code == 0x78) { if (down) qpush(EV_UNLOAD, 0, 1); return; }                // F11 smonta e riavvia (BASIC)
    if (code == 0x07) { if (down) qpush(EV_MENU, 0, 1); return; }                  // F12 torna al menu
  } else {
    if (code == 0x7C || code == 0x7E) { if (down) qpush(EV_BREAK, 0, 1); return; }  // Pause/Print
  }
  uint8_t a = 0xFF;
  if (ext) {
    switch (code) {   // frecce = CTRL + (- = + *)
      case 0x75: a = 0x80 | 0x0E; break; case 0x72: a = 0x80 | 0x0F; break;
      case 0x6B: a = 0x80 | 0x06; break; case 0x74: a = 0x80 | 0x07; break;
      case 0x71: a = 0x34; break;      // Canc -> backspace
    }
  } else if (code < 128) {
    a = amap[code];
    if (shiftDown) for (auto& o : ovShift) if (o.ps2 == code) { a = o.atari; goto done; }
    if (a != 0xFF) { if (shiftDown) a |= 0x40; if (ctrlDown) a |= 0x80; }
  }
done:
  if (a == 0xFF) return;
  if (down) { heldPs2 = code; qpush(EV_KEY, a, 1); }
  else if (heldPs2 == code) { qpush(EV_KEY, a, 0); heldPs2 = 0; }
}

// ---------------------------------------------------------------------------
//  ISR clock PS/2 (fronte di discesa)
// ---------------------------------------------------------------------------
static volatile uint8_t bitn = 0; static volatile uint16_t shreg = 0; static volatile uint32_t lastUs = 0;
static bool fExt = false, fBreak = false;

static void ps2Isr() {
  uint32_t now = micros();
  if (now - lastUs > 1500) { bitn = 0; shreg = 0; }
  lastUs = now;
  int d = digitalRead(PIN_PS2_DATA);
  if (bitn == 0) { if (d) return; }                     // start bit = 0
  else if (bitn <= 8) shreg |= (uint16_t)d << (bitn - 1);
  else if (bitn == 10) {
    uint8_t c = shreg & 0xFF; bitn = 0; shreg = 0;
    if (c == 0xE0) fExt = true;
    else if (c == 0xF0) fBreak = true;
    else if (c != 0xAA && c != 0xFA) { onKey(c, fExt, !fBreak); fExt = false; fBreak = false; }
    return;
  }
  bitn++;
}

// ---------------------------------------------------------------------------
static uint8_t kbConsole = 0, btnConsole = 0;   // bit = premuto
void inputBegin() {
  buildMap();
  const int pins[] = {PIN_JOY_UP, PIN_JOY_DOWN, PIN_JOY_LEFT, PIN_JOY_RIGHT, PIN_JOY_FIRE, PIN_BTN_START, PIN_BTN_SELECT, PIN_BTN_OPTION};
  for (int p : pins) if (p >= 0) pinMode(p, INPUT_PULLUP);
  pinMode(PIN_PS2_CLK, INPUT_PULLUP); pinMode(PIN_PS2_DATA, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_PS2_CLK), ps2Isr, FALLING);
}

void inputUpdate() {
  uint8_t j = 0xF0;
  if (digitalRead(PIN_JOY_UP))    j |= 1;
  if (digitalRead(PIN_JOY_DOWN))  j |= 2;
  if (digitalRead(PIN_JOY_LEFT))  j |= 4;
  if (digitalRead(PIN_JOY_RIGHT)) j |= 8;
  g_joyA = j;
  g_trig0 = digitalRead(PIN_JOY_FIRE) ? 1 : 0;
  btnConsole = 0;
  if (PIN_BTN_START  >= 0 && !digitalRead(PIN_BTN_START))  btnConsole |= 1;
  if (PIN_BTN_SELECT >= 0 && !digitalRead(PIN_BTN_SELECT)) btnConsole |= 2;
  if (PIN_BTN_OPTION >= 0 && !digitalRead(PIN_BTN_OPTION)) btnConsole |= 4;
  // gli eventi EV_CONSOLE della tastiera sono applicati dal chiamante tramite inputConsoleKb()
  g_consol = (~(kbConsole | btnConsole)) & 7;
}
void inputConsoleKb(uint8_t bit, bool down) { if (down) kbConsole |= 1 << bit; else kbConsole &= ~(1 << bit); }

uint8_t inputNavState() {
  uint8_t s = 0xFF;
  if (!(g_joyA & 0x01)) s &= ~0x01;      // su
  if (!(g_joyA & 0x02)) s &= ~0x02;      // giu'
  if (!(g_joyA & 0x08)) s &= ~0x04;      // destra
  if (!(g_joyA & 0x04)) s &= ~0x08;      // sinistra
  if (!(g_trig0 & 1))   s &= ~0x10;      // fire
  return s & navKb;
}