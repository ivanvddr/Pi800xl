#pragma once
/*
 * AtariLoader.h — Loader per Atari 800XL (RP2040) Pi800xl
 * 
 * Copyright (C) 2026 Ivan Vettori — GPLv3
 * 
 * Stessa interfaccia pubblica di SMSLoader (PiSMS), esteso ai formati Atari:
 *
 *   .xex .com .exe   eseguibili DOS binari      -> caricati in RAM emulata e lanciati (INIT/RUN), BASIC escluso
 *   .atr .xfd        immagini disco (D1:)       -> SIOV intercettato (HLE), boot dal disco, cache LRU dei settori
 *   .cas             immagini cassetta (FUJI)   -> SIOV dispositivo $60, avvio con START forzato  (SPERIMENTALE)
 *   .rom .car .cart .bin  cartucce              -> finestra $8000-$BFFF, tabelle di pagina + bank switching ($D5xx)
 *
 * Sorgente: SD (SPI1) con fallback su LittleFS.
 * Tipi di cartuccia supportati: STD 8K, STD 16K, XEGS 32K-1MB. Altri (OSS, Williams, Diamond, SDX, ...) sono rifiutati.
 *
 * INTERCETTAZIONE (niente emulazione seriale a livello di bit):
 *   - $E459 (SIOV): tutta l'I/O seriale dell'OS passa da qui -> disco/cassetta emulati ad alto livello
 *   - $E45A: ritorno da una routine INIT di un XEX (trampolino)
 *
 * Il file resta aperto solo se serve (ATR, CAS, XEX in corso, cartucce piu' grandi del pool).
 */
#include <Arduino.h>
#include <SD.h>
#include <LittleFS.h>
#include "config.h"
#include "Atari.h"

#define LOADER_RAMFUNC __attribute__((section(".time_critical.loader")))

enum class FSType { FS_NONE, FS_SD, FS_LITTLEFS };
enum class AtariMedia : uint8_t { NONE, XEX, ATR, CAS, CART };
enum class CartType : uint8_t { NONE, STD8, STD16, XEGS };

static constexpr uint32_t CART_BANK_SIZE = 0x2000;   // 8 KB
static constexpr uint8_t  CART_SLOTS     = 2;        // $8000-$9FFF, $A000-$BFFF
static constexpr uint16_t CART_NOBANK    = 0xFFFF;
static constexpr uint8_t  CART_POOL_MAX  = 8;

class AtariLoader : public AtariHost {
public:
  AtariLoader();
  ~AtariLoader() { unloadROM(); }

  void setMachine(Atari* a) { _atari = a; }

  // ── Filesystem (come SMSLoader) ────────────────────────────────────────
  bool initFilesystem(FSType preferred = FSType::FS_SD, int sdCsPin = SD_CS);
  FSType currentFS() const { return _currentFS; }
  bool fsReady() const { return _fsReady; }
  void closeFilesystem();

  // ── Media ───────────────────────────────────────────────────────────────
  // loadROM: riconosce il tipo, monta il media e RIAVVIA la macchina (avvio automatico)
  bool loadROM(const char* path);
  void unloadROM();                  // smonta e riavvia a freddo (BASIC)
  bool isLoaded() const { return _loaded; }
  void tick();                       // da chiamare 1 volta per frame dopo runFrame()

  // ── Accesso memoria (finestra cartuccia $8000-$BFFF, come SMSLoader::read/write) ──
  inline uint8_t read(uint16_t addr) const {
    const uint8_t* p = (addr >= 0x8000 && addr < 0xC000) ? _rd[(addr - 0x8000) >> 8] : nullptr;
    return p ? p[addr & 0xFF] : 0xFF;
  }
  inline void write(uint16_t, uint8_t) {}                 // ROM: scritture ignorate
  void writeSlow(uint16_t, uint8_t) {}
  const uint8_t* const* readPages() const { return _rd; }     // 64 pagine da 256 B per Atari::setCart()
  uint8_t* const* writePages() { return _wr; }

  // ── Info ────────────────────────────────────────────────────────────────
  uint32_t romSize() const { return _romSize; }
  uint8_t numBanks() const { return (uint8_t)_numBanks; }
  CartType mapperType() const { return _cartType; }
  AtariMedia mediaType() const { return _media; }
  const char* romName() const { return _romName; }
  uint8_t slotBank(int s) const { return (s < CART_SLOTS) ? (uint8_t)_bankReg[s] : 0; }

  // ── Debug / utilita' ────────────────────────────────────────────────────
  void debugStatus() const;
  void listROMs(const char* folder = ATARI_ROM_FOLDER);
  bool fileExists(const char* path);
  void setSelectedROM(const char* path);
  File openFile(const char* path, const char* mode);
  FSType getCurrentFilesystem() const { return _currentFS; }
  const char* getSelectedROM() const { return _romPath; }

  static bool isMediaFile(const char* name);       // estensione supportata? (.xex .com .exe .atr .xfd .cas .rom .car .cart .bin)

  // ── Sfoglia la cartella (tasti F9/F10) ─────────────────────────────────
  int  countROMs(const char* folder = ATARI_ROM_FOLDER);
  bool loadIndex(int index, const char* folder = ATARI_ROM_FOLDER);
  bool loadNext(int delta, const char* folder = ATARI_ROM_FOLDER);   // +1 / -1 con giro

  // ── AtariHost ───────────────────────────────────────────────────────────
  void    onTrap(Atari& a, uint16_t pc) override;
  uint8_t cartIo(uint16_t addr, uint8_t value, bool write) override;

private:
  Atari* _atari = nullptr;

  // filesystem
  FSType _currentFS = FSType::FS_NONE;
  bool _fsReady = false;
  int  _sdCsPin = SD_CS;
  bool initSD();

  // media
  AtariMedia _media = AtariMedia::NONE;
  CartType   _cartType = CartType::NONE;
  bool _loaded = false;
  uint32_t _romSize = 0;
  uint16_t _numBanks = 0;
  char _romName[64]; char _romPath[128];
  File _file; bool _fileOpen = false;
  bool openPath(const char* path, bool rw = false);
  void closeFile();
  bool readAt(uint32_t offset, uint8_t* dst, uint32_t len);
  void extractName(const char* path);
  void unmount();
  void startMachine(bool forceOption, bool forceStart);

  // cartucce
  uint8_t* _pool = nullptr; uint8_t _poolN = 0;
  uint16_t _poolBank[CART_POOL_MAX]; uint32_t _poolStamp[CART_POOL_MAX]; uint32_t _stamp = 0;
  uint8_t _slotPool[CART_SLOTS]; bool _slotOn[CART_SLOTS];
  uint16_t _bankReg[CART_SLOTS];
  bool _cartEnabled = false; uint32_t _cartHdr = 0;
  const uint8_t* _rd[64]; uint8_t* _wr[64]; uint8_t _sink[256];
  bool loadCart();
  bool loadBank(uint8_t slot, uint16_t bank);
  bool readBank(uint16_t bank, uint8_t* dst);
  void mapSlot(uint8_t slot);
  void clearPages();
  void freePool();

  // dischi ATR / XFD
  uint16_t _atrSecSize = 128; uint32_t _atrSectors = 0; uint32_t _atrHdr = 16; bool _atrWritable = false;
  uint8_t* _atrCache = nullptr; uint16_t _atrTag[ATARI_ATR_CACHE_SECTORS]; uint32_t _atrStamp[ATARI_ATR_CACHE_SECTORS]; uint32_t _atrClock = 0;
  bool mountAtr(const char* path, bool xfd);
  uint16_t sectorLen(uint32_t n) const { return (n <= 3) ? 128 : _atrSecSize; }
  uint32_t sectorOffset(uint32_t n) const { return (n <= 3) ? _atrHdr + (n - 1) * 128 : _atrHdr + 384 + (n - 4) * (uint32_t)_atrSecSize; }
  bool readSector(uint32_t n, uint8_t* out);
  bool writeSector(uint32_t n, const uint8_t* src);
  uint8_t diskCommand(uint8_t cmd, uint16_t buf, uint16_t len, uint16_t aux);

  // cassetta CAS
  uint32_t _casPos = 0;
  uint8_t tapeCommand(uint8_t cmd, uint16_t buf, uint16_t len);

  // SIO (HLE)
  void sioHandler();
  void sioReturn(uint8_t status);
  void putRam(uint16_t addr, const uint8_t* src, uint16_t n);

  // XEX
  bool _xexWaitInit = false;
  bool continueXex();
  bool finishXex();
  void callRoutine(uint16_t addr);

  // avvio
  enum Phase : uint8_t { PH_IDLE, PH_WAIT_BOOT } _phase = PH_IDLE;
  bool _seenFF = false; uint32_t _waitFrames = 0; bool _injectXex = false;
  int _browseIdx = -1;
};