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
#include "AtariMenu.h"

AtariMenu::AtariMenu(TFT_eSPI* display, NavReadFn navRead) {
  tft = display;
  nav = navRead;
  selectedItem = MENU_BASIC;
  lastSelectedItem = MENU_BASIC;
  lastJoyTime = 0;
  logo = nullptr;
  logoWidth = 0;
  logoHeight = 0;
  hasLogo = false;
}

void AtariMenu::setLogo(const unsigned short* logoData, int width, int height) {
  logo = logoData;
  logoWidth = width;
  logoHeight = height;
  hasLogo = (logo != nullptr);
}

void AtariMenu::begin() {
  Serial.println("AtariMenu: Init...");
  selectedItem = MENU_BASIC;
  lastSelectedItem = MENU_BASIC;
  draw();
}

void AtariMenu::draw() {
  drawBackground();
  drawLogo();
  drawMenuItems();
  drawCredits();
}

void AtariMenu::drawBackground() {
  tft->fillScreen(TFT_WHITE);
}

void AtariMenu::drawLogo() {
  if (!hasLogo) {
    // Nessun logo: titolo testuale con tre bande colorate
    tft->setTextColor(TFT_BLACK);
    tft->setTextSize(3);
    tft->setTextDatum(TC_DATUM);
    tft->drawString("Pi800XL", 160, 20);
    tft->setTextSize(2);
    tft->setTextColor(TFT_NAVY);
    tft->drawString("ATARI 800XL", 160, 52);
    tft->fillRect(100, 78, 40, 5, TFT_RED);
    tft->fillRect(140, 78, 40, 5, TFT_ORANGE);
    tft->fillRect(180, 78, 40, 5, TFT_BLUE);
    return;
  }
  int logoX = (320 - logoWidth) / 2;
  int logoY = 20;
  tft->setSwapBytes(true);
  tft->pushImage(logoX, logoY, logoWidth, logoHeight, logo, 0xFFFF);
  tft->setSwapBytes(false);
  tft->fillRect(100, 83, 40, 5, TFT_RED);
  tft->fillRect(140, 83, 40, 5, TFT_ORANGE);
  tft->fillRect(180, 83, 40, 5, TFT_BLUE);
}

void AtariMenu::drawCredits() {
  tft->setTextSize(1);
  tft->setTextColor(TFT_BLACK);
  tft->setTextDatum(BR_DATUM);
  tft->drawString("@IV ATARI 800XL emulator", 315, 235);
}

void AtariMenu::drawMenuItems() {
  for (int i = 0; i < MENU_ITEMS_COUNT; i++) {
    drawMenuItem((MenuItem)i, i == selectedItem);
  }
}

void AtariMenu::drawMenuItem(MenuItem item, bool selected) {
  int y = getMenuItemY(item);
  const char* text = getMenuItemText(item);

  // Cancella area voce (per aggiornamento)
  tft->fillRect(0, y - 2, 320, 26, TFT_WHITE);

  tft->setTextSize(2);
  tft->setTextDatum(TC_DATUM);

  if (selected) {
    tft->setTextColor(TFT_BLUE);
    int textWidth = strlen(text) * 12;               // approssimazione larghezza
    int cursorX = (320 - textWidth) / 2 - 20;        // 20px prima del testo
    tft->drawString(">", cursorX, y);
    tft->drawString(text, 160, y);
  } else {
    tft->setTextColor(TFT_NAVY);
    tft->drawString(text, 160, y);
  }
}

int AtariMenu::getMenuItemY(MenuItem item) {
  int startY = 110;
  int spacing = 35;
  return startY + (item * spacing);
}

const char* AtariMenu::getMenuItemText(MenuItem item) {
  switch (item) {
    case MENU_BASIC: return "START BASIC";
    case MENU_GAMES: return "GAMES BROWSER";
    default: return "???";
  }
}

MenuAction AtariMenu::update() {
  uint8_t joyState = nav();

  // debouncing: ignora input troppo veloci
  uint32_t now = millis();
  if (now - lastJoyTime < JOY_DEBOUNCE_MS) return ACTION_NONE;

  // UP: selezione precedente
  if (!(joyState & 0x01)) {
    lastJoyTime = now;
    selectedItem = (selectedItem > 0) ? (MenuItem)(selectedItem - 1) : (MenuItem)(MENU_ITEMS_COUNT - 1);
    if (selectedItem != lastSelectedItem) {
      drawMenuItem(lastSelectedItem, false);
      drawMenuItem(selectedItem, true);
      lastSelectedItem = selectedItem;
    }
    return ACTION_NONE;
  }

  // DOWN: selezione successiva
  if (!(joyState & 0x02)) {
    lastJoyTime = now;
    selectedItem = (selectedItem < MENU_ITEMS_COUNT - 1) ? (MenuItem)(selectedItem + 1) : (MenuItem)0;
    if (selectedItem != lastSelectedItem) {
      drawMenuItem(lastSelectedItem, false);
      drawMenuItem(selectedItem, true);
      lastSelectedItem = selectedItem;
    }
    return ACTION_NONE;
  }

  // FIRE: conferma selezione
  if (!(joyState & 0x10)) {
    lastJoyTime = now;
    // Feedback visivo: lampeggio
    drawMenuItem(selectedItem, false);
    delay(100);
    drawMenuItem(selectedItem, true);
    delay(100);
    switch (selectedItem) {
      case MENU_BASIC: return ACTION_START_BASIC;
      case MENU_GAMES: return ACTION_START_GAMES;
      default: return ACTION_NONE;
    }
  }
  return ACTION_NONE;
}