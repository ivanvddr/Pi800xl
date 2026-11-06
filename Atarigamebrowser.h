/*
 * Questo file fa parte del progetto Pi800XL (emulatore Atari 800XL per Pico).
 * Struttura e aspetto ripresi da SMSGameBrowser (PiSMS).
 * 
 * Copyright (C) 2026 Ivan Vettori — GPLv3
 * 
 * Elenca da GAME_DIR ("/Pi800xl/games") tutti i media supportati dal loader
 * (.xex .com .exe .atr .xfd .cas .rom .car .cart .bin), in ordine alfabetico, con anteprima
 * "<nome>.jpg" (200x150, non progressiva) oppure "noImage.jpg".
 */
#ifndef ATARI_GAMEBROWSER_H
#define ATARI_GAMEBROWSER_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SD.h>
#include <LittleFS.h>
#include "config.h"
#if ATARI_BROWSER_JPG
#include <TJpg_Decoder.h>
#endif
#include "AtariLoader.h"
#include "AtariMenu.h"      // NavReadFn

class AtariGameBrowser {
public:
  AtariGameBrowser(TFT_eSPI* display, AtariLoader* loaderRef, NavReadFn navRead = inputNavState);
  ~AtariGameBrowser() { freeSortedFileList(); }

  bool begin(FSType fsType = FSType::FS_SD);
  void setLogo(const unsigned short* logoData, int width, int height);
  void draw();
  void nextGame();
  void prevGame();
  void jumpGames(int delta);                 // salto di N voci con giro (DOWN = +10)

  // Ritorna true quando e' stato scelto un gioco (FIRE). Joystick: SX/DX = precedente/successivo,
  // GIU' = +10, SU = torna al menu (vedi wantsBack()).
  bool update(uint8_t joyState);
  bool update() { return update(nav()); }
  bool wantsBack() { bool b = backRequested; backRequested = false; return b; }

  String getPrgSelected();                   // percorso completo; lo registra anche nel Loader (setSelectedROM)

  void setPath(const char* cpath);           // sottocartella di GAME_DIR (es. "/atari")
  void setFileExt(const char* fext);         // filtro su una sola estensione (es. ".atr"); vuoto = tutti i formati

  int count() const { return totalGames; }
  void notify(const char* msg);              // messaggio rosso temporaneo (es. errore di caricamento), poi ridisegna

private:
  TFT_eSPI* tft;
  AtariLoader* loader;
  NavReadFn nav;
  FSType currentFS;

  char browserPath[64] = {0};
  char browserExt[8] = {0};

  const unsigned short* logo;
  int logoWidth, logoHeight;
  bool hasLogo;

  int totalGames;
  int currentIndex;
  char currentFile[64];
  char lastFile[64];

  uint32_t lastJoyTime;
  static const uint32_t BROWSER_DEBOUNCE_MS = 200;
  bool needsRedraw;
  bool backRequested;
  char** sortedFileNames;                    // elenco ordinato alfabeticamente

  File openFile(const char* path, const char* mode = "r");
  bool fileExists(const char* path);

  int countMediaFiles();
  bool buildSortedFileList();
  void freeSortedFileList();
  bool getFileAtIndex(int index, char* outName);

  bool isMedia(const char* name);
  String getDisplayName(const char* filename);
  const char* getTypeTag(const char* filename);
  bool hasImageFile(const char* name);

  void drawBackground();
  void drawLogo();
  void drawGameImage();
  void drawGameName();
  void drawInfo();
  void drawArrows();
  void drawCredits();
  void showError(const char* msg);

#if ATARI_BROWSER_JPG
  static bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);
#endif
  static TFT_eSPI* staticTFT;
};

#endif