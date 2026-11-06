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
#include "AtariLoader.h"
#include <strings.h>
#include <stdlib.h>

static const uint16_t TRAP_XEXRET = 0xE45A;      // RTS verso (E45A-1)+1: trampolino di ritorno dalle INIT

// ─────────────────────────────────────────────────────────────────────────────
AtariLoader::AtariLoader() {
  memset(_sink, 0xFF, sizeof(_sink));
  memset(_atrTag, 0xFF, sizeof(_atrTag)); memset(_atrStamp, 0, sizeof(_atrStamp));
  memset(_poolBank, 0xFF, sizeof(_poolBank)); memset(_poolStamp, 0, sizeof(_poolStamp));
  memset(_bankReg, 0, sizeof(_bankReg)); memset(_slotPool, 0, sizeof(_slotPool)); memset(_slotOn, 0, sizeof(_slotOn));
  _romName[0] = '\0'; _romPath[0] = '\0';
  clearPages();
}

void AtariLoader::clearPages() {
  for (int i = 0; i < 64; i++) { _rd[i] = nullptr; _wr[i] = nullptr; }
}

// ── Filesystem ───────────────────────────────────────────────────────────────
bool AtariLoader::initFilesystem(FSType preferred, int sdCsPin) {
  if (_fsReady) return true;
  _sdCsPin = sdCsPin;
  auto trySD = [&]() -> bool {
    Serial.println("[Loader] SD...");
    if (initSD()) { _currentFS = FSType::FS_SD; _fsReady = true; Serial.println("[Loader] SD OK"); return true; }
    Serial.println("[Loader] SD not available"); return false;
  };
  auto tryLFS = [&]() -> bool {
    Serial.println("[Loader] LittleFS...");
    if (LittleFS.begin()) { _currentFS = FSType::FS_LITTLEFS; _fsReady = true; Serial.println("[Loader] LittleFS OK"); return true; }
    Serial.println("[Loader] LittleFS not available"); return false;
  };
  if (preferred == FSType::FS_SD) { if (trySD()) return true; if (tryLFS()) return true; }
  else                            { if (tryLFS()) return true; if (trySD()) return true; }
  Serial.println("[Loader] ERROR: no filesystem found");
  return false;
}

bool AtariLoader::initSD() {
  SPI1.setRX(SD_MISO); SPI1.setTX(SD_MOSI); SPI1.setSCK(SD_SCLK); SPI1.setCS(SD_CS);
  SPI1.begin();
  delay(100);
  return SD.begin(_sdCsPin, SD_SPI_HZ, SPI1);
}

void AtariLoader::closeFilesystem() {
  closeFile();
  if (_currentFS == FSType::FS_SD) { SD.end(); Serial.println("[Loader] SD closed"); }
  _fsReady = false; _currentFS = FSType::FS_NONE;
}

bool AtariLoader::openPath(const char* path, bool rw) {
  if (_fileOpen) closeFile();
  if (!_fsReady) return false;
  if (_currentFS == FSType::FS_SD) _file = rw ? SD.open(path, "r+") : SD.open(path, FILE_READ);
  else                             _file = rw ? LittleFS.open(path, "r+") : LittleFS.open(path, "r");
  _fileOpen = (bool)_file;
  return _fileOpen;
}
void AtariLoader::closeFile() { if (_fileOpen) { _file.close(); _fileOpen = false; } }

bool AtariLoader::readAt(uint32_t offset, uint8_t* dst, uint32_t len) {
  if (!_fileOpen || !_file.seek(offset)) return false;
  return _file.read(dst, len) == len;
}

File AtariLoader::openFile(const char* path, const char* mode) {
  if (_currentFS == FSType::FS_SD) return SD.open(path, mode);
  if (_currentFS == FSType::FS_LITTLEFS) return LittleFS.open(path, mode);
  return File();
}
bool AtariLoader::fileExists(const char* path) {
  if (_currentFS == FSType::FS_SD) return SD.exists(path);
  if (_currentFS == FSType::FS_LITTLEFS) return LittleFS.exists(path);
  return false;
}
void AtariLoader::setSelectedROM(const char* path) {
  if (!path) return;
  strncpy(_romPath, path, sizeof(_romPath) - 1); _romPath[sizeof(_romPath) - 1] = '\0';
  Serial.printf("AtariLoader: ROM selected > %s\n", _romPath);
}
void AtariLoader::extractName(const char* path) {
  const char* slash = strrchr(path, '/');
  const char* base = slash ? slash + 1 : path;
  strncpy(_romName, base, sizeof(_romName) - 1); _romName[sizeof(_romName) - 1] = '\0';
  char* dot = strrchr(_romName, '.'); if (dot) *dot = '\0';
}

// ── Estensioni ───────────────────────────────────────────────────────────────
static const char* extOf(const char* name) { const char* d = strrchr(name, '.'); return d ? d : ""; }
static AtariMedia mediaFromExt(const char* name) {
  const char* e = extOf(name);
  if (!strcasecmp(e, ".xex") || !strcasecmp(e, ".com") || !strcasecmp(e, ".exe")) return AtariMedia::XEX;
  if (!strcasecmp(e, ".atr") || !strcasecmp(e, ".xfd")) return AtariMedia::ATR;
  if (!strcasecmp(e, ".cas")) return AtariMedia::CAS;
  if (!strcasecmp(e, ".rom") || !strcasecmp(e, ".car") || !strcasecmp(e, ".cart") || !strcasecmp(e, ".bin")) return AtariMedia::CART;
  return AtariMedia::NONE;
}

// ── loadROM / unloadROM ──────────────────────────────────────────────────────
bool AtariLoader::loadROM(const char* path) {
  if (!path || !path[0] || !_atari) return false;
  if (!_fsReady && !initFilesystem()) { Serial.println("[Loader] ERROR: filesystem not ready"); return false; }
  AtariMedia m = mediaFromExt(path);
  if (m == AtariMedia::NONE) { Serial.printf("[Loader] ERROR: unknown extension: %s\n", path); return false; }

  unmount();
  strncpy(_romPath, path, sizeof(_romPath) - 1); _romPath[sizeof(_romPath) - 1] = '\0';
  extractName(path);
  Serial.printf("\n[Loader] Loading: %s\n", path);

  bool ok = false;
  switch (m) {
    case AtariMedia::XEX: {
      if (!openPath(path)) break;
      uint8_t h[2];
      if (!readAt(0, h, 2) || h[0] != 0xFF || h[1] != 0xFF) { Serial.println("[Loader] ERROR: not a DOS binary (missing FFFF)"); break; }
      _romSize = _file.size(); _file.seek(0);
      ok = true; break;
    }
    case AtariMedia::ATR: ok = mountAtr(path, !strcasecmp(extOf(path), ".xfd")); break;
    case AtariMedia::CAS: {
      if (!openPath(path)) break;
      uint8_t h[4];
      if (!readAt(0, h, 4) || memcmp(h, "FUJI", 4) != 0) { Serial.println("[Loader] ERROR: CAS without FUJI header"); break; }
      _romSize = _file.size(); _casPos = 0; ok = true; break;
    }
    case AtariMedia::CART: ok = openPath(path) && loadCart(); break;
    default: break;
  }
  if (!ok) { unmount(); return false; }

  _media = m; _loaded = true;
  Serial.printf("[Loader] %s: %s (%lu KB)\n", m == AtariMedia::XEX ? "XEX" : m == AtariMedia::ATR ? "ATR" :
                m == AtariMedia::CAS ? "CAS" : "CART", _romName, (unsigned long)(_romSize / 1024));
  // XEX/ATR/CAS: BASIC escluso (OPTION premuto al boot); CAS: START premuto = boot da cassetta
  startMachine(m != AtariMedia::CART, m == AtariMedia::CAS);
  _injectXex = (m == AtariMedia::XEX);
  return true;
}

void AtariLoader::unmount() {
  closeFile();
  _media = AtariMedia::NONE; _cartType = CartType::NONE; _loaded = false; _romSize = 0; _numBanks = 0;
  _xexWaitInit = false; _phase = PH_IDLE; _injectXex = false;
  freePool();
  if (_atrCache) { free(_atrCache); _atrCache = nullptr; }
  clearPages();
  if (_atari) _atari->setCart(nullptr, nullptr);
}

void AtariLoader::unloadROM() {
  bool was = _loaded;
  unmount();
  _romName[0] = '\0'; _romPath[0] = '\0';
  if (_atari && was) { _atari->forceConsol(0); _atari->reset(true); }
}

void AtariLoader::startMachine(bool forceOption, bool forceStart) {
  if (!_atari) return;
  if (_media == AtariMedia::CART) _atari->setCart(_rd, _wr); else _atari->setCart(nullptr, nullptr);
  _atari->forceConsol((forceOption ? 0x04 : 0) | (forceStart ? 0x01 : 0));
  _atari->reset(true);
  _seenFF = false; _waitFrames = 0;
  _phase = (forceOption || forceStart) ? PH_WAIT_BOOT : PH_IDLE;
}

// Attende la fine del boot dell'OS: COLDST ($0244) va a $FF durante l'init e torna a 0 alla fine
void AtariLoader::tick() {
  if (_phase != PH_WAIT_BOOT || !_atari) return;
  uint8_t cold = _atari->ram[0x244];
  if (cold == 0xFF) _seenFF = true;
  bool done = (_seenFF && cold == 0) || (++_waitFrames > ATARI_BOOT_TIMEOUT_FRAMES);
  if (!done) return;
  _phase = PH_IDLE;
  _atari->forceConsol(0);                                   // da ora i tasti console tornano reali
  if (_injectXex) {
    _injectXex = false;
    Serial.println("[Loader] OS pronto: avvio XEX");
    _atari->cpu.sp = 0xFF;
    if (!continueXex()) Serial.println("[Loader] XEX: fine");
  }
}

// ── Cartucce ─────────────────────────────────────────────────────────────────
void AtariLoader::freePool() {
  if (_pool) { free(_pool); _pool = nullptr; }
  _poolN = 0;
  memset(_poolBank, 0xFF, sizeof(_poolBank));
}

bool AtariLoader::readBank(uint16_t bank, uint8_t* dst) {
  uint32_t off = (uint32_t)bank * CART_BANK_SIZE, len = CART_BANK_SIZE;
  if (off + len > _romSize) len = (off < _romSize) ? _romSize - off : 0;
  if (len && !readAt(_cartHdr + off, dst, len)) { memset(dst, 0xFF, CART_BANK_SIZE); return false; }
  if (len < CART_BANK_SIZE) memset(dst + len, 0xFF, CART_BANK_SIZE - len);
  return true;
}

void AtariLoader::mapSlot(uint8_t s) {
  bool on = _cartEnabled && _slotOn[s];
  const uint8_t* base = on ? _pool + (uint32_t)_slotPool[s] * CART_BANK_SIZE : nullptr;
  for (int i = 0; i < 32; i++) {
    _rd[s * 32 + i] = on ? base + i * 256 : nullptr;
    _wr[s * 32 + i] = on ? _sink : nullptr;
  }
}

bool AtariLoader::loadCart() {
  uint32_t fileSize = _file.size();
  uint8_t h[16] = {0};
  if (fileSize < 16 || !readAt(0, h, 16)) { Serial.println("[Loader] ERROR: file too small"); return false; }
  _cartHdr = 0;
  uint32_t carType = 0; bool hasHdr = (memcmp(h, "CART", 4) == 0);
  if (hasHdr) { carType = ((uint32_t)h[4] << 24) | ((uint32_t)h[5] << 16) | ((uint32_t)h[6] << 8) | h[7]; _cartHdr = 16; }
  _romSize = fileSize - _cartHdr;

  CartType t = CartType::NONE;
  if (hasHdr) {
    switch (carType) {
      case 1: case 21: t = CartType::STD8; break;                      // standard 8K / right slot 8K
      case 2: t = CartType::STD16; break;
      case 12: case 13: case 14: case 23: case 24: case 25: t = CartType::XEGS; break;   // XEGS 32K..1MB
      default: Serial.printf("[Loader] ERROR: cartridge type %lu not supported\n", (unsigned long)carType); return false;
    }
  } else {                                                              // .rom grezzo: il tipo si deduce dalla dimensione
    if (_romSize == 8192) t = CartType::STD8;
    else if (_romSize == 16384) t = CartType::STD16;
    else if (_romSize >= 32768 && _romSize <= 1048576 && (_romSize & (_romSize - 1)) == 0) t = CartType::XEGS;   // ipotesi
    else { Serial.printf("[Loader] ERROR: unknown raw ROM size (%lu)\n", (unsigned long)_romSize); return false; }
  }
  _cartType = t;
  _numBanks = (uint16_t)((_romSize + CART_BANK_SIZE - 1) / CART_BANK_SIZE);
  if (t == CartType::STD8) _numBanks = 1;
  if (t == CartType::STD16) _numBanks = 2;
  Serial.printf("[Loader] Cart: %s %lu KB %u bank%s\n", t == CartType::STD8 ? "STD8" : t == CartType::STD16 ? "STD16" : "XEGS",
                (unsigned long)(_romSize / 1024), (unsigned)_numBanks, hasHdr ? " (.car)" : " (raw)");

  // Pool in heap: solo quanto serve (min. 2 bank per XEGS: finestra + bank fisso)
  uint8_t need = (t == CartType::STD8) ? 1 : 2;
  uint8_t want = (_numBanks < ATARI_CART_POOL_BANKS) ? (uint8_t)_numBanks : ATARI_CART_POOL_BANKS;
  if (want < need) want = need;
  if (want > CART_POOL_MAX) want = CART_POOL_MAX;
  while (want >= need && !(_pool = (uint8_t*)malloc((size_t)want * CART_BANK_SIZE))) want--;
  if (!_pool) { Serial.println("[Loader] ERROR: not enough RAM for cart pool"); return false; }
  _poolN = want;
  Serial.printf("[Loader] Cart pool: %u x 8 KB%s\n", (unsigned)_poolN, (_numBanks <= _poolN) ? " (ROM all in RAM)" : " (LRU)");

  memset(_poolBank, 0xFF, sizeof(_poolBank)); memset(_poolStamp, 0, sizeof(_poolStamp)); _stamp = 0;
  _cartEnabled = true;
  _slotOn[0] = (t != CartType::STD8); _slotOn[1] = true;
  // bank iniziali: STD8 -> A000=0 ; STD16 -> 8000=0, A000=1 ; XEGS -> 8000=0, A000=ultimo
  _bankReg[0] = 0;
  _bankReg[1] = (t == CartType::STD8) ? 0 : (t == CartType::STD16) ? 1 : (uint16_t)(_numBanks - 1);
  if (_numBanks <= _poolN) {
    for (uint16_t b = 0; b < _numBanks; b++) {
      if (!readBank(b, _pool + (uint32_t)b * CART_BANK_SIZE)) { Serial.printf("[Loader] ERROR reading bank %u\n", (unsigned)b); return false; }
      _poolBank[b] = b; _poolStamp[b] = ++_stamp;
    }
    _slotPool[0] = (uint8_t)_bankReg[0]; _slotPool[1] = (uint8_t)_bankReg[1];
    closeFile();
    Serial.println("[Loader] file closed");
  } else {
    // slot 1 = bank fisso (non sostituibile dalla LRU perche' usato da uno slot)
    _slotPool[0] = 0; _slotPool[1] = 1;
    if (!loadBank(1, _bankReg[1]) || !loadBank(0, _bankReg[0])) return false;
  }
  mapSlot(0); mapSlot(1);
  return true;
}

LOADER_RAMFUNC bool AtariLoader::loadBank(uint8_t slot, uint16_t bank) {
  if (slot >= CART_SLOTS) return false;
  if (_numBanks > 0) bank %= _numBanks;
  if (_poolBank[_slotPool[slot]] == bank) { _bankReg[slot] = bank; mapSlot(slot); return true; }
  int idx = -1;
  for (int i = 0; i < _poolN; i++) if (_poolBank[i] == bank) { idx = i; break; }
  if (idx < 0) {
    uint32_t best = 0xFFFFFFFFUL;
    for (int i = 0; i < _poolN; i++) {
      bool used = false;
      for (uint8_t s2 = 0; s2 < CART_SLOTS; s2++) if (s2 != slot && _slotOn[s2] && _slotPool[s2] == i) used = true;
      if (used) continue;
      if (_poolBank[i] == CART_NOBANK) { idx = i; break; }
      if (_poolStamp[i] < best) { best = _poolStamp[i]; idx = i; }
    }
    if (idx < 0) return false;
    if (!readBank(bank, _pool + (uint32_t)idx * CART_BANK_SIZE)) { _poolBank[idx] = CART_NOBANK; return false; }
    _poolBank[idx] = bank;
  }
  _poolStamp[idx] = ++_stamp;
  _slotPool[slot] = (uint8_t)idx; _bankReg[slot] = bank;
  mapSlot(slot);
  return true;
}

// Pagina $D5xx: bank switching. XEGS: la scrittura sceglie il bank in $8000; bit 7 = cartuccia disabilitata
LOADER_RAMFUNC uint8_t AtariLoader::cartIo(uint16_t, uint8_t value, bool write) {
  if (!write || _media != AtariMedia::CART || _cartType != CartType::XEGS) return 0xFF;
  bool en = !(value & 0x80);
  bool remap = false;
  if (en != _cartEnabled) { _cartEnabled = en; mapSlot(0); mapSlot(1); remap = true; }
  if (en) {
    uint16_t bank = (_numBanks ? (value & 0x7F) % _numBanks : 0);
    if (bank != _bankReg[0] || remap) { if (loadBank(0, bank)) remap = true; }
  }
  if (remap && _atari) _atari->remap();
  return 0xFF;
}

// ── Dischi ATR / XFD ─────────────────────────────────────────────────────────
bool AtariLoader::mountAtr(const char* path, bool xfd) {
  bool rw = false;
#if ATARI_ATR_WRITABLE
  rw = (_currentFS == FSType::FS_SD);
#endif
  if (rw) { if (!openPath(path, true)) rw = false; }
  if (!rw && !openPath(path)) return false;
  _atrWritable = rw;
  uint32_t size = _file.size();
  if (xfd) {
    _atrHdr = 0; _atrSecSize = 128; _atrSectors = size / 128;
  } else {
    uint8_t h[16];
    if (!readAt(0, h, 16) || h[0] != 0x96 || h[1] != 0x02) { Serial.println("[Loader] ERROR: not an ATR (bad magic)"); return false; }
    uint32_t paras = h[2] | (h[3] << 8) | ((uint32_t)h[6] << 16);
    _atrSecSize = h[4] | (h[5] << 8);
    if (_atrSecSize != 128 && _atrSecSize != 256 && _atrSecSize != 512) { Serial.printf("[Loader] ERROR: sector size %u not supported\n", _atrSecSize); return false; }
    if (_atrSecSize > 256) { Serial.println("[Loader] ERROR: 512-byte sectors not supported"); return false; }
    _atrHdr = 16;
    uint32_t img = paras * 16;
    _atrSectors = (_atrSecSize == 128) ? img / 128 : (img >= 384 ? 3 + (img - 384) / _atrSecSize : img / 128);
  }
  _romSize = size;
  _atrCache = (uint8_t*)malloc((size_t)ATARI_ATR_CACHE_SECTORS * 256);
  memset(_atrTag, 0xFF, sizeof(_atrTag)); _atrClock = 0;
  Serial.printf("[Loader] ATR: %lu sectors of %u bytes, %s, cache %s\n", (unsigned long)_atrSectors, _atrSecSize,
                _atrWritable ? "read/write" : "read-only", _atrCache ? "LRU" : "off (no RAM)");
  return _atrSectors > 0;
}

bool AtariLoader::readSector(uint32_t n, uint8_t* out) {
  if (n == 0 || n > _atrSectors) return false;
  uint16_t len = sectorLen(n);
  if (_atrCache) for (int i = 0; i < ATARI_ATR_CACHE_SECTORS; i++) if (_atrTag[i] == n) {
    memcpy(out, _atrCache + i * 256, len); _atrStamp[i] = ++_atrClock; return true;
  }
  if (!readAt(sectorOffset(n), out, len)) return false;
  if (_atrCache) {
    int v = 0; uint32_t best = 0xFFFFFFFFUL;
    for (int i = 0; i < ATARI_ATR_CACHE_SECTORS; i++) { if (_atrTag[i] == 0xFFFF) { v = i; break; } if (_atrStamp[i] < best) { best = _atrStamp[i]; v = i; } }
    memcpy(_atrCache + v * 256, out, len); _atrTag[v] = (uint16_t)n; _atrStamp[v] = ++_atrClock;
  }
  return true;
}

bool AtariLoader::writeSector(uint32_t n, const uint8_t* src) {
  if (!_atrWritable || n == 0 || n > _atrSectors) return false;
  uint16_t len = sectorLen(n);
  if (!_file.seek(sectorOffset(n)) || _file.write(src, len) != len) return false;
  _file.flush();
  if (_atrCache) for (int i = 0; i < ATARI_ATR_CACHE_SECTORS; i++) if (_atrTag[i] == n) { memcpy(_atrCache + i * 256, src, len); _atrStamp[i] = ++_atrClock; }
  return true;
}

uint8_t AtariLoader::diskCommand(uint8_t cmd, uint16_t buf, uint16_t len, uint16_t aux) {
  uint8_t tmp[256];
  switch (cmd) {
    case 0x52: {                                          // 'R' leggi settore
      if (!readSector(aux, tmp)) return 0x90;
      uint16_t n = sectorLen(aux); if (len < n) n = len;
      putRam(buf, tmp, n);
      return 1;
    }
    case 0x57: case 0x50: {                               // 'W' / 'P' scrivi settore
      if (!_atrWritable) return 0x90;
      uint16_t n = sectorLen(aux); memset(tmp, 0, sizeof(tmp));
      for (uint16_t i = 0; i < n && i < len; i++) tmp[i] = _atari->ram[(buf + i) & 0xFFFF];
      return writeSector(aux, tmp) ? 1 : 0x90;
    }
    case 0x53: {                                          // 'S' stato: 4 byte
      uint8_t st[4] = { (uint8_t)(0x10 | (_atrSecSize == 256 ? 0x20 : 0) | (_atrWritable ? 0 : 0x08)), 0xFF, 0xE0, 0x00 };
      putRam(buf, st, len < 4 ? len : 4);
      return 1;
    }
    case 0x4E: {                                          // 'N' lettura configurazione PERCOM: 12 byte
      uint8_t pc[12] = { 40, 1, 0, (uint8_t)(_atrSecSize == 256 ? 26 : 18), 0, (uint8_t)(_atrSecSize == 256 ? 4 : 0),
                         (uint8_t)(_atrSecSize >> 8), (uint8_t)(_atrSecSize & 0xFF), 0xFF, 0, 0, 0 };
      putRam(buf, pc, len < 12 ? len : 12);
      return 1;
    }
    default: return 0x8B;                                 // NAK (formattazione e altro: non supportati)
  }
}

// ── Cassetta (SPERIMENTALE) ─────────────────────────────────────────────────
// Ogni chunk 'data' del FUJI contiene un record da 132 byte (55 55 controllo 128 dati checksum): si copia cosi' com'e'.
uint8_t AtariLoader::tapeCommand(uint8_t cmd, uint16_t buf, uint16_t len) {
  if (cmd != 0x52) return 0x8B;
  for (int guard = 0; guard < 4096; guard++) {
    uint8_t h[8];
    if (!readAt(_casPos, h, 8)) return 0x8A;               // fine nastro
    uint16_t size = h[4] | (h[5] << 8);
    uint32_t next = _casPos + 8 + size;
    if (!memcmp(h, "data", 4)) {
      uint8_t tmp[256]; uint16_t n = size < sizeof(tmp) ? size : (uint16_t)sizeof(tmp);
      if (!readAt(_casPos + 8, tmp, n)) return 0x8A;
      _casPos = next;
      if (n > len) n = len;
      putRam(buf, tmp, n);
      return 1;
    }
    _casPos = next;                                         // FUJI, baud, fsk, ...: si saltano
  }
  return 0x8A;
}

// ── SIOV (HLE) ───────────────────────────────────────────────────────────────
void AtariLoader::putRam(uint16_t addr, const uint8_t* src, uint16_t n) {
  for (uint16_t i = 0; i < n; i++) _atari->ram[(uint16_t)(addr + i)] = src[i];
}

void AtariLoader::sioReturn(uint8_t status) {
  Cpu6502& c = _atari->cpu;
  _atari->ram[0x0303] = status;                             // DSTATS
  c.y = status;
  c.p = (c.p & 0x7D) | (status & 0x80) | (status == 0 ? 2 : 0);
  c.sp++; uint8_t lo = c.rd(0x100 + c.sp);                  // RTS
  c.sp++; uint8_t hi = c.rd(0x100 + c.sp);
  c.pc = (uint16_t)((lo | (hi << 8)) + 1);
}

void AtariLoader::sioHandler() {
  const uint8_t* r = _atari->ram;
  uint8_t dev = r[0x300], unit = r[0x301], cmd = r[0x302];
  uint16_t buf = r[0x304] | (r[0x305] << 8), len = r[0x308] | (r[0x309] << 8), aux = r[0x30A] | (r[0x30B] << 8);
  uint8_t st = 0x8A;                                        // timeout: nessun dispositivo
  if (dev == 0x31 && unit == 1 && _media == AtariMedia::ATR) st = diskCommand(cmd, buf, len, aux);
  else if (dev == 0x60 && _media == AtariMedia::CAS) st = tapeCommand(cmd, buf, len);
  sioReturn(st);
}

void AtariLoader::onTrap(Atari& a, uint16_t pc) {
  (void)a;
  uint16_t off = pc - 0xE459;
  if (off == 0) {
#if ATARI_SIO_FAST_FAIL
    sioHandler();
#else
    _atari->cpu.trapSkip = true;                            // SIOV originale (solo con media montato)
    if (_media == AtariMedia::ATR || _media == AtariMedia::CAS) { _atari->cpu.trapSkip = false; sioHandler(); }
#endif
  } else if (off == 1 && _xexWaitInit) {                    // ritorno da una INIT
    _xexWaitInit = false;
    if (!continueXex()) Serial.println("[Loader] XEX: fine");
  } else {
    _atari->cpu.trapSkip = true;
  }
}

// ── XEX ──────────────────────────────────────────────────────────────────────
void AtariLoader::callRoutine(uint16_t addr) {
  Cpu6502& c = _atari->cpu;
  uint16_t ret = TRAP_XEXRET - 1;                           // RTS somma 1
  c.wr(0x100 + c.sp, ret >> 8);   c.sp--;
  c.wr(0x100 + c.sp, ret & 0xFF); c.sp--;
  c.pc = addr; c.p &= ~0x0C;                                // I e D azzerati
  _xexWaitInit = true;
}

bool AtariLoader::finishXex() {
  closeFile();
  uint8_t* r = _atari->ram;
  uint16_t run = r[0x2E0] | (r[0x2E1] << 8);
  if (!run) { Serial.println("[Loader] XEX without RUN address: loaded, no start"); return false; }
  Serial.printf("[Loader] XEX: RUN $%04X\n", run);
  _atari->cpu.pc = run; _atari->cpu.p &= ~0x0C;
  return true;
}

// Legge i segmenti finche' non ne trova uno che imposta INITAD ($02E2): lo chiama e attende il ritorno
bool AtariLoader::continueXex() {
  uint8_t* r = _atari->ram;
  for (;;) {
    uint8_t h[2];
    if (_file.read(h, 2) != 2) return finishXex();
    uint16_t s = h[0] | (h[1] << 8);
    if (s == 0xFFFF) { if (_file.read(h, 2) != 2) return finishXex(); s = h[0] | (h[1] << 8); }
    if (_file.read(h, 2) != 2) return finishXex();
    uint16_t e = h[0] | (h[1] << 8);
    if (e < s) { Serial.println("[Loader] XEX: invalid segment"); return finishXex(); }
    uint32_t len = (uint32_t)e - s + 1;
    r[0x2E2] = r[0x2E3] = 0;
    uint32_t got = _file.read(&r[s], len);
    if (got != len) { Serial.println("[Loader] XEX: truncated file"); return finishXex(); }
    uint16_t init = r[0x2E2] | (r[0x2E3] << 8);
    if (init) {
      r[0x2E2] = r[0x2E3] = 0;
      callRoutine(init);
      return true;
    }
  }
}

// ── Info / debug / sfoglia ────────────────────────────────────────────────────
void AtariLoader::debugStatus() const {
  if (!_loaded) { Serial.println("[Loader] No media loaded"); return; }
  Serial.println("\n=== AtariLoader Status ===");
  Serial.printf("Media  : %s  %s (%lu KB)\n", _media == AtariMedia::XEX ? "XEX" : _media == AtariMedia::ATR ? "ATR" :
                _media == AtariMedia::CAS ? "CAS" : "CART", _romName, (unsigned long)(_romSize / 1024));
  if (_media == AtariMedia::CART) {
    Serial.printf("Cart   : %s banks=%u enabled=%d pool=%u file=%s\n", _cartType == CartType::STD8 ? "STD8" :
                  _cartType == CartType::STD16 ? "STD16" : "XEGS", (unsigned)_numBanks, _cartEnabled, (unsigned)_poolN,
                  _fileOpen ? "open (LRU)" : "closed");
    for (int s = 0; s < CART_SLOTS; s++)
      Serial.printf(" Slot %d ($%04X-$%04X) -> bank %u (pool %u) %s\n", s, 0x8000 + s * 0x2000, 0x9FFF + s * 0x2000,
                    (unsigned)_bankReg[s], (unsigned)_slotPool[s], (_cartEnabled && _slotOn[s]) ? "" : "(off)");
  }
  Serial.println("==========================");
}

static bool isMedia(const char* name) { return mediaFromExt(name) != AtariMedia::NONE; }
bool AtariLoader::isMediaFile(const char* name) { return isMedia(name); }

void AtariLoader::listROMs(const char* folder) {
  if (!_fsReady) { Serial.println("[Loader] Filesystem not ready"); return; }
  Serial.printf("Media in %s:\n==========================================\n", folder);
  if (_currentFS == FSType::FS_SD) {
    File root = SD.open(folder);
    if (!root) { Serial.println(" ERROR: folder not found"); return; }
    File e = root.openNextFile();
    while (e) {
      if (!e.isDirectory() && isMedia(e.name())) Serial.printf(" %-40s %6lu KB\n", e.name(), (unsigned long)(e.size() / 1024));
      e.close(); e = root.openNextFile();
    }
    root.close();
  } else {
    Dir dir = LittleFS.openDir(folder);
    while (dir.next()) if (isMedia(dir.fileName().c_str())) {
      File f = dir.openFile("r");
      if (f) { Serial.printf(" %-40s %6lu KB\n", dir.fileName().c_str(), (unsigned long)(f.size() / 1024)); f.close(); }
    }
  }
  Serial.println("==========================================");
}

// Itera i file supportati; se index >= 0 copia il percorso completo del file numero 'index' in out
static int scanFolder(AtariLoader* L, FSType fs, const char* folder, int index, char* out, size_t outLen) {
  int n = 0;
  auto visit = [&](const char* name) {
    if (!isMedia(name)) return;
    if (n == index && out) snprintf(out, outLen, "%s/%s", folder, (name[0] == '/') ? name + 1 : name);
    n++;
  };
  if (fs == FSType::FS_SD) {
    File root = SD.open(folder);
    if (!root) return 0;
    File e = root.openNextFile();
    while (e) { if (!e.isDirectory()) visit(e.name()); e.close(); e = root.openNextFile(); }
    root.close();
  } else if (fs == FSType::FS_LITTLEFS) {
    Dir dir = LittleFS.openDir(folder);
    while (dir.next()) visit(dir.fileName().c_str());
  }
  (void)L;
  return n;
}

int AtariLoader::countROMs(const char* folder) {
  if (!_fsReady) return 0;
  return scanFolder(this, _currentFS, folder, -1, nullptr, 0);
}
bool AtariLoader::loadIndex(int index, const char* folder) {
  if (!_fsReady) return false;
  char path[128] = {0};
  int n = scanFolder(this, _currentFS, folder, index, path, sizeof(path));
  if (index < 0 || index >= n || !path[0]) return false;
  _browseIdx = index;
  return loadROM(path);
}
bool AtariLoader::loadNext(int delta, const char* folder) {
  int n = countROMs(folder);
  if (n <= 0) { Serial.printf("[Loader] No media in %s\n", folder); return false; }
  int i = (_browseIdx < 0) ? (delta > 0 ? 0 : n - 1) : (_browseIdx + delta + n) % n;
  return loadIndex(i, folder);
}