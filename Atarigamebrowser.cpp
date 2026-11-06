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
#include "AtariGamebrowser.h"
#include <strings.h>

TFT_eSPI* AtariGameBrowser::staticTFT = nullptr;

AtariGameBrowser::AtariGameBrowser(TFT_eSPI* display, AtariLoader* loaderRef, NavReadFn navRead) {
  tft = display;
  loader = loaderRef;
  nav = navRead;
  staticTFT = tft;
  currentFS = FSType::FS_NONE;
  totalGames = 0;
  currentIndex = 0;
  currentFile[0] = '\0';
  lastFile[0] = '\0';
  lastJoyTime = 0;
  needsRedraw = true;
  backRequested = false;
  logo = nullptr;
  logoWidth = 0;
  logoHeight = 0;
  hasLogo = false;
  sortedFileNames = nullptr;
}

// ── Inizializzazione ─────────────────────────────────────────────────────────
bool AtariGameBrowser::begin(FSType fsType) {
  Serial.println("AtariGameBrowser: Init...");
  freeSortedFileList();
  totalGames = 0;
  backRequested = false;

  if (browserPath[0] == '\0') snprintf(browserPath, sizeof(browserPath), "%s", GAME_DIR);

  if (!loader->initFilesystem(fsType)) {
    Serial.println("Filesystem unavailable!");
    showError("NO FILESYSTEM!");
    return false;
  }
  currentFS = loader->currentFS();
  Serial.printf("Filesystem: %s\n", currentFS == FSType::FS_SD ? "SD" : "LittleFS");

  Serial.printf("Path: %s\n", browserPath);
  File dir = loader->openFile(browserPath, "r");
  if (!dir || !dir.isDirectory()) {
    Serial.printf("Directory %s not found!\n", browserPath);
    showError("NO GAMES FOLDER!");
    return false;
  }
  dir.close();

  totalGames = countMediaFiles();
  if (totalGames == 0) {
    Serial.println("no games found!");
    showError("NO GAMES FOUND!");
    return false;
  }
  Serial.printf("found %d games\n", totalGames);

  if (!buildSortedFileList()) {
    Serial.println("Unable to sort games list!");
    showError("SORT ERROR!");
    return false;
  }

#if ATARI_BROWSER_JPG
  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(tftOutput);
#endif

  // ripristina l'ultimo gioco scelto, se ancora presente
  currentIndex = 0;
  if (lastFile[0]) for (int i = 0; i < totalGames; i++) if (!strcmp(sortedFileNames[i], lastFile)) { currentIndex = i; break; }
  if (!getFileAtIndex(currentIndex, currentFile)) {
    Serial.println("Unable to load first game!");
    showError("LOAD ERROR!");
    return false;
  }
  Serial.printf("First Game: %s (index %d/%d)\n", currentFile, currentIndex + 1, totalGames);

  needsRedraw = true;
  draw();
  return true;
}

// ── Elenco ordinato ──────────────────────────────────────────────────────────
bool AtariGameBrowser::buildSortedFileList() {
  if (totalGames == 0 || totalGames > MAX_SORTED_GAMES) {
    Serial.printf("Too many games for sorting (%d > %d)\n", totalGames, MAX_SORTED_GAMES);
    return false;
  }
  sortedFileNames = (char**)malloc(totalGames * sizeof(char*));
  if (!sortedFileNames) { Serial.println("Malloc failed!"); return false; }
  for (int i = 0; i < totalGames; i++) sortedFileNames[i] = nullptr;

  File dir = openFile(browserPath, "r");
  if (!dir || !dir.isDirectory()) { free(sortedFileNames); sortedFileNames = nullptr; return false; }

  int count = 0;
  File entry;
  while ((entry = dir.openNextFile()) && count < totalGames) {
    if (!entry.isDirectory() && isMedia(entry.name())) {
      size_t n = strlen(entry.name());
      sortedFileNames[count] = (char*)malloc(n + 1);
      if (sortedFileNames[count]) { memcpy(sortedFileNames[count], entry.name(), n + 1); count++; }
    }
    entry.close();
  }
  dir.close();

  if (count != totalGames) { Serial.printf("Count mismatch: expected %d, found %d\n", totalGames, count); totalGames = count; }

  // ordine alfabetico (insertion sort: stabile, nessun buffer extra)
  for (int i = 1; i < totalGames; i++) {
    char* key = sortedFileNames[i];
    int j = i - 1;
    while (j >= 0 && strcasecmp(sortedFileNames[j], key) > 0) { sortedFileNames[j + 1] = sortedFileNames[j]; j--; }
    sortedFileNames[j + 1] = key;
  }
  Serial.println("Array sorted:");
  for (int i = 0; i < (totalGames < 5 ? totalGames : 5); i++) Serial.printf(" [%d] %s\n", i, sortedFileNames[i]);
  if (totalGames > 5) Serial.println(" ...");
  return totalGames > 0;
}

void AtariGameBrowser::freeSortedFileList() {
  if (sortedFileNames) {
    for (int i = 0; i < totalGames; i++) if (sortedFileNames[i]) free(sortedFileNames[i]);
    free(sortedFileNames);
    sortedFileNames = nullptr;
  }
}

bool AtariGameBrowser::getFileAtIndex(int targetIndex, char* outName) {
  if (targetIndex < 0 || targetIndex >= totalGames) { Serial.printf("Index %d out of range (0-%d)\n", targetIndex, totalGames - 1); return false; }
  if (!sortedFileNames || !sortedFileNames[targetIndex]) { Serial.println("Sorted array not available!"); return false; }
  strncpy(outName, sortedFileNames[targetIndex], 63);
  outName[63] = '\0';
  return true;
}

// ── Navigazione (wrap-around) ────────────────────────────────────────────────
void AtariGameBrowser::jumpGames(int delta) {
  if (totalGames == 0) return;
  int oldIndex = currentIndex;
  currentIndex = ((currentIndex + delta) % totalGames + totalGames) % totalGames;
  if (getFileAtIndex(currentIndex, currentFile)) {
    Serial.printf("Game: %s (%d/%d)\n", currentFile, currentIndex + 1, totalGames);
    drawGameImage();
    drawGameName();
    drawInfo();
  } else {
    currentIndex = oldIndex;
  }
}
void AtariGameBrowser::nextGame() { jumpGames(+1); }
void AtariGameBrowser::prevGame() { jumpGames(-1); }

bool AtariGameBrowser::update(uint8_t joyState) {
  if (totalGames == 0) {                       // niente da scegliere: SU = indietro
    uint32_t n0 = millis();
    if (!(joyState & 0x01) && n0 - lastJoyTime >= BROWSER_DEBOUNCE_MS) { lastJoyTime = n0; backRequested = true; }
    return false;
  }
  uint32_t now = millis();
  if (now - lastJoyTime < BROWSER_DEBOUNCE_MS) return false;

  if (!(joyState & 0x08)) { lastJoyTime = now; prevGame(); return false; }          // SINISTRA
  if (!(joyState & 0x04)) { lastJoyTime = now; nextGame(); return false; }          // DESTRA
  if (!(joyState & 0x02)) { lastJoyTime = now; jumpGames(+10); return false; }      // GIU'
  if (!(joyState & 0x01)) { lastJoyTime = now; backRequested = true; return false; } // SU = menu
  if (!(joyState & 0x10)) { lastJoyTime = now; return true; }                       // FIRE = gioco scelto
  return false;
}

void AtariGameBrowser::setPath(const char* cpath) {
  snprintf(browserPath, sizeof(browserPath), "%s%s", GAME_DIR, cpath ? cpath : "");
}
void AtariGameBrowser::setFileExt(const char* fext) {
  snprintf(browserExt, sizeof(browserExt), "%s", fext ? fext : "");
}

String AtariGameBrowser::getPrgSelected() {
  char fullPath[128];
  snprintf(fullPath, sizeof(fullPath), "%s/%s", browserPath, currentFile);
  strncpy(lastFile, currentFile, sizeof(lastFile) - 1); lastFile[sizeof(lastFile) - 1] = '\0';
  loader->setSelectedROM(fullPath);
  Serial.printf("File selected: %s\n", fullPath);
  return String(fullPath);
}

// ── Utility ──────────────────────────────────────────────────────────────────
int AtariGameBrowser::countMediaFiles() {
  int count = 0;
  File dir = openFile(browserPath, "r");
  if (!dir || !dir.isDirectory()) return 0;
  File entry;
  while ((entry = dir.openNextFile())) {
    if (!entry.isDirectory() && isMedia(entry.name())) count++;
    entry.close();
  }
  dir.close();
  Serial.printf("Scan completed: %d media files\n", count);
  return count;
}

File AtariGameBrowser::openFile(const char* path, const char* mode) { return loader->openFile(path, mode); }
bool AtariGameBrowser::fileExists(const char* path) { return loader->fileExists(path); }

bool AtariGameBrowser::isMedia(const char* name) {
  if (!AtariLoader::isMediaFile(name)) return false;
  if (browserExt[0]) {
    const char* dot = strrchr(name, '.');
    return dot && !strcasecmp(dot, browserExt);
  }
  return true;
}

String AtariGameBrowser::getDisplayName(const char* filename) {
  String name = String(filename);
  int dotPos = name.lastIndexOf('.');
  return (dotPos > 0) ? name.substring(0, dotPos) : name;
}

const char* AtariGameBrowser::getTypeTag(const char* filename) {
  const char* dot = strrchr(filename, '.');
  if (!dot) return "";
  if (!strcasecmp(dot, ".atr") || !strcasecmp(dot, ".xfd")) return "DISK";
  if (!strcasecmp(dot, ".cas")) return "TAPE";
  if (!strcasecmp(dot, ".xex") || !strcasecmp(dot, ".com") || !strcasecmp(dot, ".exe")) return "XEX";
  return "CART";
}

bool AtariGameBrowser::hasImageFile(const char* name) {
  String baseName = getDisplayName(name);
  char jpgPath[128];
  snprintf(jpgPath, sizeof(jpgPath), "%s/%s.jpg", browserPath, baseName.c_str());
  return fileExists(jpgPath);
}

// ── Disegno ──────────────────────────────────────────────────────────────────
void AtariGameBrowser::draw() {
  if (!needsRedraw) return;
  drawBackground();
  drawLogo();
  drawGameImage();
  drawGameName();
  drawInfo();
  drawArrows();
  drawCredits();
  needsRedraw = false;
}

void AtariGameBrowser::drawBackground() { tft->fillScreen(TFT_WHITE); }

void AtariGameBrowser::drawLogo() {
  if (!hasLogo) {
    tft->setTextColor(TFT_BLUE);
    tft->setTextSize(2);
    tft->setTextDatum(TL_DATUM);
    tft->drawString("Pi800XL", 5, 5);
    return;
  }
  tft->setSwapBytes(true);
  tft->pushImage(5, 5, logoWidth, logoHeight, logo, 0xFFFF);
  tft->setSwapBytes(false);
}

void AtariGameBrowser::drawGameImage() {
  const int imgWidth = 200;
  const int imgHeight = 150;
  const int imgX = (320 - imgWidth) / 2;
  const int imgY = 40;

  tft->fillRect(imgX, imgY, imgWidth, imgHeight, TFT_WHITE);
  if (totalGames == 0 || currentFile[0] == '\0') return;

  char imagePath[128];
  String displayName = getDisplayName(currentFile);
  if (hasImageFile(currentFile)) snprintf(imagePath, sizeof(imagePath), "%s/%s.jpg", browserPath, displayName.c_str());
  else                           snprintf(imagePath, sizeof(imagePath), "%s/noImage.jpg", browserPath);

#if ATARI_BROWSER_JPG
  if (fileExists(imagePath)) {
    if (currentFS == FSType::FS_SD) TJpgDec.drawSdJpg(imgX, imgY, imagePath);
    else if (currentFS == FSType::FS_LITTLEFS) TJpgDec.drawFsJpg(imgX, imgY, imagePath);
    return;
  }
#endif
  // nessuna anteprima: riquadro con il tipo di media
  tft->drawRect(imgX, imgY, imgWidth, imgHeight, TFT_BLACK);
  tft->setTextColor(TFT_BLACK);
  tft->setTextSize(2);
  tft->setTextDatum(MC_DATUM);
  tft->drawString(getTypeTag(currentFile), 160, imgY + imgHeight / 2);
}

void AtariGameBrowser::drawGameName() {
  if (totalGames == 0 || currentFile[0] == '\0') return;
  String displayName = getDisplayName(currentFile);
  if (displayName.length() > 24) displayName = displayName.substring(0, 23) + "~";   // 24 car. a size 2 = 288 px
  tft->setTextColor(TFT_BLUE);
  tft->setTextSize(2);
  tft->setTextDatum(TC_DATUM);
  tft->fillRect(0, 200, 320, 20, TFT_WHITE);
  tft->drawString(displayName, 160, 200);
}

void AtariGameBrowser::drawInfo() {
  if (totalGames == 0) return;
  char info[40];
  snprintf(info, sizeof(info), "%s  %d/%d", getTypeTag(currentFile), currentIndex + 1, totalGames);
  tft->fillRect(0, 224, 165, 16, TFT_WHITE);
  tft->setTextSize(1);
  tft->setTextColor(TFT_BLACK);
  tft->setTextDatum(BL_DATUM);
  tft->drawString(info, 5, 235);
}

void AtariGameBrowser::drawArrows() {
  if (totalGames <= 1) return;
  tft->setTextColor(TFT_BLACK);
  tft->setTextSize(4);
  tft->setTextDatum(MC_DATUM);
  tft->drawString("<", 20, 120);
  tft->drawString(">", 300, 120);
}

void AtariGameBrowser::drawCredits() {
  tft->setTextSize(1);
  tft->setTextColor(TFT_BLACK);
  tft->setTextDatum(BR_DATUM);
  tft->drawString("@IV ATARI 800XL emulator", 315, 235);
}

void AtariGameBrowser::showError(const char* msg) {
  tft->fillScreen(TFT_WHITE);
  tft->setTextColor(TFT_RED);
  tft->setTextSize(2);
  tft->setTextDatum(MC_DATUM);
  tft->drawString(msg, 160, 120);
  tft->setTextSize(1);
  tft->setTextColor(TFT_BLACK);
  tft->drawString(browserPath[0] ? browserPath : GAME_DIR, 160, 150);
  tft->drawString("UP = back", 160, 175);
}

void AtariGameBrowser::notify(const char* msg) {
  tft->fillRect(0, 100, 320, 40, TFT_WHITE);
  tft->drawRect(10, 100, 300, 40, TFT_RED);
  tft->setTextColor(TFT_RED);
  tft->setTextSize(2);
  tft->setTextDatum(MC_DATUM);
  tft->drawString(msg, 160, 120);
  delay(1500);
  needsRedraw = true;
  draw();
}

void AtariGameBrowser::setLogo(const unsigned short* logoData, int width, int height) {
  logo = logoData;
  logoWidth = width;
  logoHeight = height;
  hasLogo = (logo != nullptr);
}

#if ATARI_BROWSER_JPG
bool AtariGameBrowser::tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (bitmap && staticTFT) {
    staticTFT->setSwapBytes(true);
    staticTFT->pushImage(x, y, w, h, bitmap);
    staticTFT->setSwapBytes(false);
  }
  return true;
}
#endif