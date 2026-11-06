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
// ============================================================================
//  Atari 800XL per Raspberry Pi Pico (RP2040) - configurazione
// ============================================================================
#include <stdint.h>

// ---- Clock ---------------------------------------------------------------
#define PICO_CLOCK_KHZ        300000   // 0 = non toccare il clock (usa il menu Tools)
#define PICO_VREG_VOLTAGE     VREG_VOLTAGE_1_30   // 1_20 fino a ~280MHz, 1_30 oltre
#define PICO_PERI_FROM_SYS    1        // 1 = clk_peri = clk_sys (di default resta 48MHz: SPI limitato a 24MHz!)
#define ATARI_ROM_IN_RAM      0        // 1 = copia OS+BASIC in SRAM (+24KB): attivare solo se avanza RAM
 

// ---- Standard video ------------------------------------------------------
#define ATARI_PAL             1        // 1 = PAL (312 righe, 50Hz)  0 = NTSC (262 righe, 60Hz)
#if ATARI_PAL
  #define SCANLINES           312
  #define CPU_HZ              1773447
#else
  #define SCANLINES           262
  #define CPU_HZ              1789773
#endif
#define CYC_LINE              114

// ---- Audio (MAX98357 I2S) ------------------------------------------------
#define PIN_I2S_BCLK          26       // LRCLK = BCLK+1 = GP27 (vincolo della libreria I2S)
#define PIN_I2S_DIN           28
#define AUDIO_SAMPLE_RATE     31250    // >= 16000. Il blocco su I2S sincronizza l'emulazione
#define AUDIO_DMA_BUFFERS     6
#define AUDIO_DMA_WORDS       128      // 6*128 frame = ~24ms di latenza a 31250Hz
#define AUDIO_GAIN            2        // moltiplicatore (1..2)
// Canali POKEY con periodo (in cicli CPU) sotto questa soglia sono oltre Nyquist: si usa il livello medio
#define POKEY_ULTRA_PER       ((int)(CPU_HZ / (0.8 * AUDIO_SAMPLE_RATE)))

// ---- Video ---------------------------------------------------------------
#define FB_W                  320
#define FB_H                  240
#define VIDEO_FRAMESKIP       0        // frame NON disegnati ogni frame disegnato (0 = tutti)
#define COLLISIONS_ON_SKIPPED 1        // 1 = calcola le collisioni P/M anche sui frame saltati
#define TFT_CHUNK_LINES       8        // righe per trasferimento DMA

// ---- Input ---------------------------------------------------------------
// Direzionale passiva 5 vie (verso GND, pull-up interni). Centro = FIRE.
#define PIN_JOY_UP            2
#define PIN_JOY_DOWN          3
#define PIN_JOY_LEFT          4
#define PIN_JOY_RIGHT         5
#define PIN_JOY_FIRE          6
// Pulsanti console opzionali (-1 = non usato). Duplicati da F2/F3/F4 della tastiera.
#define PIN_BTN_START         7
#define PIN_BTN_SELECT        0
#define PIN_BTN_OPTION        1
// PS/2: GP27/GP28 sono ora occupati dall'audio, quindi pin nuovi
#define PIN_PS2_CLK           15
#define PIN_PS2_DATA          14

// ---- Diagnostica ---------------------------------------------------------
#define ATARI_DEBUG           1        // 1 = stampa lo stato sulla seriale (115200) ogni secondo

// ---- Loader (SD / LittleFS) ----------------------------------------------
#define SD_CS                 13
#define SD_MISO               12
#define SD_MOSI               11
#define SD_SCLK               10
#define SD_SPI_HZ             10000000
#define ATARI_ROM_FOLDER      "/atari"      // cartella sfogliata con F9/F10
#define ATARI_CART_POOL_BANKS 3             // pool di bank da 8KB per le cartucce grandi (allocato solo quando serve)
#define ATARI_ATR_CACHE_SECTORS 16          // cache LRU dei settori ATR (max 256B ciascuno)
#define ATARI_ATR_WRITABLE    1             // scrittura sui dischi ATR (solo SD; su LittleFS resta in sola lettura)
#define ATARI_SIO_FAST_FAIL   1             // senza supporto montato SIOV risponde subito "timeout": boot rapido
#define ATARI_BOOT_TIMEOUT_FRAMES 3000      // attesa massima del completamento del boot OS (60 s a 50 Hz)
// #define ATARI_AUTOLOAD     "/atari/gioco.xex"   // se definito, caricato all'avvio
 
// ---- Menu e browser giochi ------------------------------------------------
#define GAME_DIR              "/Pi800xl/games"   // cartella dei giochi (+ anteprime .jpg 200x150 con lo stesso nome, e noImage.jpg)
#define ATARI_BROWSER_JPG     1                  // 1 = anteprime JPG (serve la libreria TJpg_Decoder), 0 = solo testo
#define MAX_SORTED_GAMES      200                // limite dell'elenco ordinato
#define MENU_BACK_FRAMES      50                 // START+SELECT premuti per ~1 s durante il gioco = ritorno al menu