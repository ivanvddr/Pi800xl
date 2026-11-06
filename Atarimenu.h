/*
 * Questo file fa parte del progetto Pi800XL (emulatore Atari 800XL per Pico).
 * Struttura e aspetto ripresi da SMSMenu (PiSMS). GPLv3.
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
#ifndef ATARI_MENU_H
#define ATARI_MENU_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Input.h"

// ===== VOCI MENU =====
enum MenuItem {
  MENU_BASIC = 0,
  MENU_GAMES = 1,
  MENU_ITEMS_COUNT = 2
};

// ===== AZIONI MENU =====
enum MenuAction {
  ACTION_NONE = 0,
  ACTION_START_BASIC = 1,
  ACTION_START_GAMES = 2
};

// Lettura della navigazione: stesso formato di FunduinoJoystick::read() (0 = premuto):
// bit0 SU, bit1 GIU', bit2 DESTRA, bit3 SINISTRA, bit4 FIRE
typedef uint8_t (*NavReadFn)();

class AtariMenu {
public:
  AtariMenu(TFT_eSPI* display, NavReadFn navRead = inputNavState);

  // Inizializza menu (disegna tutto)
  void begin();

  // Aggiorna menu (chiamare in loop). Ritorna ACTION_NONE finche' non viene selezionata una voce
  MenuAction update();

  // Disegna menu completo
  void draw();

  // Imposta logo RGB565 (opzionale, prima di begin()); senza logo si disegna il titolo testuale
  void setLogo(const unsigned short* logoData, int width, int height);

private:
  TFT_eSPI* tft;
  NavReadFn nav;

  const unsigned short* logo;
  int logoWidth, logoHeight;
  bool hasLogo;

  MenuItem selectedItem, lastSelectedItem;

  uint32_t lastJoyTime;
  static const uint32_t JOY_DEBOUNCE_MS = 200;

  void drawBackground();
  void drawLogo();
  void drawMenuItems();
  void drawCredits();
  void drawMenuItem(MenuItem item, bool selected);
  int getMenuItemY(MenuItem item);
  const char* getMenuItemText(MenuItem item);
};

#endif