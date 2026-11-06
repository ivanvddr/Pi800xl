/*
 * Questo file fa parte del progetto Pi800xl.
 *
 * Copyright (C) 2026 Ivan Vettori
 *
 * Rilasciato sotto licenza GNU General Public License v3.0.
 *
 *  Atari 800XL su Raspberry Pi Pico (RP2040) - Arduino IDE (core arduino-pico)
 *  core0: menu / browser giochi / emulazione (CPU/ANTIC/GTIA/POKEY/PIA) + audio + rendering nel framebuffer
 *  core1: solo invio del framebuffer al TFT via DMA (non blocca mai core0)
 *
 *  Flusso  :             MENU --START BASIC--> emulazione
 *                        MENU --GAMES BROWSER--> BROWSER --FIRE--> carica il media e avvia l'emulazione
 *  Ritorno al menu dall'emulazione: F12 oppure START+SELECT premuti ~1s sul joystick/pulsanti.
 */

#include <Arduino.h>
#include "config.h"
#include "Atari.h"
#include "Video.h"
#include "Audio.h"
#include "Input.h"
#include "AtariLoader.h"
#include "AtariMenu.h"
#include "Pi800Logo.h"
#include "AtariGamebrowser.h"
#include "pico/stdlib.h"      // set_sys_clock_khz, tight_loop_contents
#include "hardware/vreg.h"
#include "hardware/clocks.h"

static Atari atari;     // ~65KB di RAM emulata in .bss
static AtariLoader loader;
static void serialCommands();

// ---- stato applicazione ----
enum AppState { APP_MENU, APP_BROWSER, APP_EMU };
static AppState app = APP_MENU;
static AtariMenu menu(videoTFT());
static AtariGameBrowser browser(videoTFT(), &loader);
static int menuHold = 0;

// core1 deve aver finito di inviare l'ultimo frame prima che core0 disegni sul TFT
static void waitVideoIdle() { while (g_fbState != 0) tight_loop_contents(); }
static void enterEmu() { waitVideoIdle(); g_fbDirty = FB_DIRTY_ALL; menuHold = 0; app = APP_EMU; }   // il TFT mostra il menu: ridisegno completo
static void showMenu()  { waitVideoIdle(); app = APP_MENU; menu.setLogo(logoPi800, 143, 70); menu.begin(); }

// core1 aspetta che core0 abbia finito l'overclock prima di inizializzare SPI/TFT
static volatile bool canStartVideo = false;

void setup() {
#if PICO_CLOCK_KHZ
  vreg_set_voltage(PICO_VREG_VOLTAGE);
  delay(10);
  set_sys_clock_khz(PICO_CLOCK_KHZ, true);
#endif
#if PICO_PERI_FROM_SYS
  {   // il clock delle periferiche (SPI/UART/I2C) resta a 48MHz: lo aggancio a clk_sys (il TFT parte dopo, vedi canStartVideo)
    uint32_t f = clock_get_hz(clk_sys);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, f, f);
  }
#endif
  __sync_synchronize();
  canStartVideo = true;      // da qui core1 puo' inizializzare il display al clock definitivo
  Serial.begin(115200);
  inputBegin();
  atari.init();
  loader.setMachine(&atari);
  loader.initFilesystem(FSType::FS_SD, SD_CS);      // SD, con fallback su LittleFS
  atari.setHost(&loader);                           // SIOV intercettato: boot rapido anche senza media
  atari.reset(true);
  audioBegin();
  while (!g_videoReady) { tight_loop_contents(); }    // core1 ha inizializzato il display
#ifdef ATARI_AUTOLOAD
  if (loader.loadROM(ATARI_AUTOLOAD)) { enterEmu(); return; }
#endif
  showMenu();
}

void loop() {
  inputUpdate();
  InEvent e;

  if (app == APP_MENU) {
    while (inputPoll(e)) {}                                   // tastiera ignorata: la navigazione usa inputNavState()
    MenuAction act = menu.update();
    if (act == ACTION_START_BASIC) {
      loader.unloadROM();                                     // smonta qualsiasi media...
      atari.forceConsol(0); atari.reset(true);                // ...e riavvia a freddo con il BASIC
      enterEmu();
    } else if (act == ACTION_START_GAMES) {
      browser.setLogo(logoPi800_small, 60, 30);
      browser.begin();                                        // in caso di errore mostra il messaggio (SU = indietro)
      app = APP_BROWSER;
    }
    serialCommands();
    return;
  }

  if (app == APP_BROWSER) {
    while (inputPoll(e)) {}
    if (browser.update()) {                                   // FIRE: gioco scelto
      String path = browser.getPrgSelected();
      if (loader.loadROM(path.c_str())) enterEmu();           // monta il media, riavvia la macchina e parte
      else browser.notify("LOAD ERROR!");
    } else if (browser.wantsBack()) {
      showMenu();
    }
    serialCommands();
    return;
  }

  // ---- APP_EMU ----
  while (inputPoll(e)) {
    switch (e.type) {
      case EV_KEY:     atari.keyEvent(e.code, e.down); break;
      case EV_SHIFT:   atari.shiftEvent(e.down); break;
      case EV_CONSOLE: inputConsoleKb(e.code, e.down); break;
      case EV_BREAK:   atari.breakKey(); break;
      case EV_WARM:    atari.reset(false); break;
      case EV_COLD:    atari.reset(true); break;
      case EV_LOAD_NEXT: loader.loadNext(+1); break;
      case EV_LOAD_PREV: loader.loadNext(-1); break;
      case EV_UNLOAD:    loader.unloadROM(); break;
      case EV_MENU:      showMenu(); return;
    }
  }
  // START+SELECT tenuti per ~1 s: ritorno al menu (utile col solo joystick)
  if ((g_consol & 0x03) == 0) { if (++menuHold >= MENU_BACK_FRAMES) { showMenu(); return; } } else menuHold = 0;

  atari.runFrame();     // ~20ms: il ritmo lo da' il blocco su I2S (audioPush)
  loader.tick();
  serialCommands();
#if ATARI_DEBUG
  static uint32_t t0 = 0;
  if (millis() - t0 >= 1000) {
    static char buf[900];
    uint32_t blocked = g_audioBlockedUs; g_audioBlockedUs = 0;
    Serial.printf("--- %lu ms | frame emulati/s=%lu disegnati/s=%lu (reale=%d) | carico core0 ~%lu%% (100%% = non regge) ---\n",
                  (unsigned long)millis(), (unsigned long)atari.statFrames, (unsigned long)atari.statDrawn, ATARI_PAL ? 50 : 60,
                  (unsigned long)(blocked >= 1000000 ? 0 : 100 - blocked / 10000));
    {
      uint32_t us = g_videoSendUs ? g_videoSendUs : 1;
      Serial.printf("    TFT: invio=%lu us, blocchi=%lu/%d, effettivo=%lu kbit/s\n",
                    (unsigned long)g_videoSendUs, (unsigned long)g_videoChunks, FB_CHUNKS, (unsigned long)((uint64_t)g_videoBytes * 8000 / us));
      Serial.printf("    TFT tempi: conversione=%lu us, attesa DMA=%lu us, avvio DMA (pushImageDMA)=%lu us\n",
                    (unsigned long)g_tConv, (unsigned long)g_tWait, (unsigned long)g_tPush);
      Serial.printf("    TFT cfg: SPI_FREQUENCY=%lu Hz PIO=%lu | clk_sys=%lu clk_peri=%lu Hz | baud HW spi0=%lu spi1=%lu Hz\n",
                    (unsigned long)g_cfgSpiHz, (unsigned long)g_cfgPio, (unsigned long)g_clkSysHz, (unsigned long)g_clkPeriHz,
                    (unsigned long)g_spiBaud, (unsigned long)g_spiBaud1);
    }
    atari.statFrames = atari.statDrawn = 0;
    atari.debugState(buf, sizeof(buf));
    Serial.print(buf);
    t0 = millis();
  }
#endif
}

// Comandi sulla seriale: ls | load <percorso> | status | unload
static void serialCommands() {
  static char line[128]; static int n = 0;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch != '\n') { if (n < (int)sizeof(line) - 1) line[n++] = ch; continue; }
    line[n] = 0; n = 0;
    if (!strcmp(line, "ls")) loader.listROMs();
    else if (!strncmp(line, "load ", 5)) loader.loadROM(line + 5);
    else if (!strcmp(line, "status")) loader.debugStatus();
    else if (!strcmp(line, "unload")) loader.unloadROM();
  }
}

void setup1() {
  while (!canStartVideo) { tight_loop_contents(); }
  videoBegin();
}
void loop1()  { videoLoop(); }
