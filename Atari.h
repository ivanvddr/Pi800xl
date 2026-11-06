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
#include "config.h"
#include "Cpu6502.h"

class Atari;
// Interfaccia verso il loader (cartucce, dischi, XEX)
class AtariHost {
public:
  virtual ~AtariHost() {}
  virtual void    onTrap(Atari& a, uint16_t pc) = 0;                      // pc dentro [E459, E459+3)
  virtual uint8_t cartIo(uint16_t addr, uint8_t value, bool write) = 0;   // pagina $D5xx (bank switching)
};

class Atari {
public:
  Cpu6502 cpu;
  uint8_t ram[65536];

  void init();
  void reset(bool cold);
  void runFrame();                       // una frame completa (SCANLINES righe)
  void keyEvent(uint8_t code, bool down);
  void shiftEvent(bool down) { shift = down; }
  void breakKey();
  // ---- loader ----
  void setHost(AtariHost* h) { host = h; cpu.trapBase = 0xE459; cpu.trapRange = h ? 3 : 0; }
  void setCart(const uint8_t* const* rd, uint8_t* const* wr) { cartRd = rd; cartWr = wr; remap(); }   // 64 pagine da 256B: $8000-$BFFF (nullptr = niente)
  void forceConsol(uint8_t mask) { consolForce = mask; }       // bit a 1 = tasto console premuto (bit0 START, bit1 SELECT, bit2 OPTION)
  void remap();
  uint32_t frames() const { return statTotalFrames; }
  uint32_t statFrames = 0, statDrawn = 0, statBasic = 0;      // contatori diagnostici
  void debugState(char* buf, int n);           // stato leggibile (vedi ATARI_DEBUG)

private:
  // ---- memoria ----
  const uint8_t* rdp[256];
  uint8_t* wrp[256];
  AtariHost* host = nullptr;
  const uint8_t* const* cartRd = nullptr; uint8_t* const* cartWr = nullptr; uint8_t consolForce = 0;
  uint32_t statTotalFrames = 0;
  uint8_t portb = 0xFF, ddrb = 0, porta = 0xFF, ddra = 0, pactl = 0, pbctl = 0;
  inline const uint8_t* aptr(uint16_t ad) const {
    const uint8_t* q = rdp[ad >> 8]; if (!q) q = ram + (ad & 0xFF00); return q + (ad & 0xFF);
  }
  static uint8_t ioReadCb(void* c, uint16_t a)             { return ((Atari*)c)->ioRead(a); }
  static void    ioWriteCb(void* c, uint16_t a, uint8_t v) { ((Atari*)c)->ioWrite(a, v); }
  uint8_t ioRead(uint16_t a);
  void    ioWrite(uint16_t a, uint8_t v);
  uint8_t gtiaRead(int r);   void gtiaWrite(int r, uint8_t v);
  uint8_t anticRegRead(int r); void anticRegWrite(int r, uint8_t v);
  uint8_t pokeyRead(int r);  void pokeyWrite(int r, uint8_t v);
  uint8_t piaRead(int r);    void piaWrite(int r, uint8_t v);

  // ---- temporizzazione ----
  int scan = 0, carry = 0, dma = 0, curT = 0;
  bool nextWsync = false, wsyncReq = false, drawFrame = false;
  uint32_t frameNo = 0;
  void runLine(int s);

  // ---- ANTIC ----
  uint8_t dmactl = 0, chactl = 0, hscrol = 0, vscrol = 0, pmbase = 0, chbase = 0, nmien = 0, nmist = 0x1F;
  uint16_t dlist = 0, dlPtr = 0, scr = 0;
  bool dlJvb = false, prevVs = false;
  uint8_t mlMode = 0, mlLine = 0, mlLast = 0, mlStart = 0; bool mlDli = false, mlHs = false, mlActive = false;
  uint8_t rMode = 0, rLine = 0, rBytes = 0, rWf = 0, rWfe = 0; bool rHs = false, dliNow = false;
  uint8_t lineData[48];
  uint8_t dlRead();
  void anticLine(int s);
  void fetchInstr();
  void pmFetch(int s);

  // ---- GTIA ----
  uint8_t hposp[4] = {0}, hposm[4] = {0}, sizep[4] = {0}, sizem = 0, grafp[4] = {0}, grafm = 0;
  uint8_t colpm[4] = {0}, colpf[4] = {0}, colbk = 0, prior = 0, vdelay = 0, gractl = 0;
  uint8_t mpf[4] = {0}, ppf[4] = {0}, mpl[4] = {0}, ppl[4] = {0};
  bool spk = false;
  void renderLine(int s);
  void decode(uint8_t* pc, uint8_t* pb);
  void composePM(uint8_t* row);

  // ---- POKEY ----
  uint8_t audf[4] = {0}, audc[4] = {0}, audctl = 0, skctl = 0, kbcode = 0, irqen = 0, irqst = 0xFF;
  int per[4] = {1, 1, 1, 1}, cnt[4] = {1, 1, 1, 1};
  bool outv[4] = {false, false, false, false};
  int b4 = 0, b5 = 0, b9 = 0, b17 = 0;
  bool keyHeld = false, shift = false;
  uint32_t audAcc = 0; int dcX = 0, dcY = 0;
  int serNeed = 0, serDone = 0;                 // righe mancanti agli IRQ seriali (SIO)
  int  serByteCycles();
  void serialTick();
  void pokeyRecalc();
  int  pokeyAdvance(int n);
  void audioLine();
  void updIrq() { cpu.irqLine = (irqst != 0xFF); }
};