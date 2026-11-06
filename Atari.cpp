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
#include "Atari.h"
#include "roms.h"
#include "Video.h"
#include "Input.h"
#include "Audio.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
//  Tabelle statiche
// ============================================================================
struct ModeInfo { uint8_t height, bpl, text; };
static ModeInfo MI[16] = {
  {0,0,0},{0,0,0},
  {8,40,1},{10,40,1},{8,40,1},{16,40,1},{8,20,1},{16,20,1},
  {8,10,0},{4,10,0},{4,20,0},{2,20,0},{1,20,0},{2,40,0},{1,40,0},{1,40,0}};
static uint8_t WF[4]  = {0, 32, 40, 48};          // byte per 40 colonne
static int16_t X0[4]  = {0, 32, 0, -32};          // inizio playfield in pixel (0 = bordo sx finestra)
static int16_t WPX[4] = {0, 256, 320, 384};

static uint8_t s_sink[256];
#if ATARI_ROM_IN_RAM
static uint8_t s_os[16384], s_basic[8192];     // copie in SRAM (+24KB): ROM, charset incluso, senza passare dalla cache XIP
#define OSROM    s_os
#define BASICROM s_basic
#else
#define OSROM    boot0
#define BASICROM boot1
#endif
static uint8_t P4[15], P5[31], P9[511], P17[16384];
static uint8_t s_scratch[FB_W] __attribute__((aligned(4)));
static uint8_t s_rowBits[FB_W], s_pm[FB_W];
static uint8_t s_pfc[448], s_pfb[448];

#define PUT(n, c, b) do { uint8_t _c = (c), _b = (b); for (int _i = 0; _i < (n); _i++) { pc[_i] = _c; pb[_i] = _b; } pc += (n); pb += (n); } while (0)

// ============================================================================
//  Init / reset / memoria
// ============================================================================
void Atari::init() {
  Cpu6502::initTables();
#if ATARI_ROM_IN_RAM
  memcpy(s_os, boot0, sizeof(s_os)); memcpy(s_basic, boot1, sizeof(s_basic));
#endif
  uint32_t r = 0xF; for (int i = 0; i < 15; i++)  { P4[i] = r & 1;  r = (r >> 1) | ((((r) ^ (r >> 1)) & 1) << 3); }
  r = 0x1F;         for (int i = 0; i < 31; i++)  { P5[i] = r & 1;  r = (r >> 1) | ((((r) ^ (r >> 2)) & 1) << 4); }
  r = 0x1FF;        for (int i = 0; i < 511; i++) { P9[i] = r & 1;  r = (r >> 1) | ((((r) ^ (r >> 4)) & 1) << 8); }
  r = 0x1FFFF; memset(P17, 0, sizeof(P17));
  for (int i = 0; i < 131071; i++) { if (r & 1) P17[i >> 3] |= (1 << (i & 7)); r = (r >> 1) | ((((r) ^ (r >> 5)) & 1) << 16); }
  cpu.rdp = rdp; cpu.wrp = wrp; cpu.ioRead = ioReadCb; cpu.ioWrite = ioWriteCb; cpu.ctx = this;
}

void Atari::remap() {
  uint8_t pb = portb | ~ddrb;
  for (int p = 0; p < 256; p++) { rdp[p] = ram + (p << 8); wrp[p] = ram + (p << 8); }
  if (!(pb & 2)) for (int p = 0xA0; p <= 0xBF; p++) { rdp[p] = BASICROM + ((p - 0xA0) << 8); wrp[p] = s_sink; }
  if (pb & 1) {
    for (int p = 0xC0; p <= 0xCF; p++) { rdp[p] = OSROM + ((p - 0xC0) << 8); wrp[p] = s_sink; }
    for (int p = 0xD8; p <= 0xFF; p++) { rdp[p] = OSROM + ((p - 0xC0) << 8); wrp[p] = s_sink; }
    if (!(pb & 0x80)) for (int p = 0x50; p <= 0x57; p++) { rdp[p] = OSROM + 0x1000 + ((p - 0x50) << 8); wrp[p] = s_sink; }
  }
  if (cartRd) for (int i = 0; i < 64; i++) if (cartRd[i]) { rdp[0x80 + i] = cartRd[i]; wrp[0x80 + i] = cartWr[i]; }   // la cartuccia vince sul BASIC
  for (int p = 0xD0; p <= 0xD7; p++) { rdp[p] = nullptr; wrp[p] = nullptr; }   // I/O
}

void Atari::reset(bool cold) {
  if (cold) memset(ram, 0, sizeof(ram));
  portb = 0xFF; ddrb = 0; porta = 0xFF; ddra = 0; pactl = pbctl = 0;
  dmactl = chactl = hscrol = vscrol = pmbase = chbase = nmien = 0; nmist = 0x1F; dlist = 0;
  memset(hposp, 0, 4); memset(hposm, 0, 4); memset(sizep, 0, 4); sizem = 0; memset(grafp, 0, 4); grafm = 0;
  memset(colpm, 0, 4); memset(colpf, 0, 4); colbk = prior = vdelay = gractl = 0;
  memset(mpf, 0, 4); memset(ppf, 0, 4); memset(mpl, 0, 4); memset(ppl, 0, 4);
  memset(audf, 0, 4); memset(audc, 0, 4); audctl = 0; skctl = 0; irqen = 0; irqst = 0xFF; kbcode = 0xFF;
  serNeed = serDone = 0;
  keyHeld = false; outv[0] = outv[1] = outv[2] = outv[3] = false; pokeyRecalc();
  for (int i = 0; i < 4; i++) cnt[i] = per[i];
  b4 = b5 = b9 = b17 = 0; carry = 0; nextWsync = wsyncReq = false; dlJvb = false; mlActive = false; prevVs = false;
  remap();
  cpu.irqLine = false; cpu.resetCPU();
}

// ============================================================================
//  I/O
// ============================================================================
ATARI_RAMFUNC uint8_t Atari::ioRead(uint16_t a) {
  switch (a >> 8) {
    case 0xD0: return gtiaRead(a & 0x1F);
    case 0xD2: return pokeyRead(a & 0x0F);
    case 0xD3: return piaRead(a & 3);
    case 0xD4: return anticRegRead(a & 0x0F);
    case 0xD5: return host ? host->cartIo(a, 0, false) : 0xFF;
    default:   return 0xFF;
  }
}
ATARI_RAMFUNC void Atari::ioWrite(uint16_t a, uint8_t v) {
  switch (a >> 8) {
    case 0xD0: gtiaWrite(a & 0x1F, v); break;
    case 0xD2: pokeyWrite(a & 0x0F, v); break;
    case 0xD3: piaWrite(a & 3, v); break;
    case 0xD4: anticRegWrite(a & 0x0F, v); break;
    case 0xD5: if (host) host->cartIo(a, v, true); break;
    default: break;
  }
}

ATARI_RAMFUNC uint8_t Atari::piaRead(int r) {
  switch (r) {
    case 0: return (pactl & 4) ? (uint8_t)((g_joyA & ~ddra) | (porta & ddra)) : ddra;
    case 1: return (pbctl & 4) ? (uint8_t)(portb | ~ddrb) : ddrb;
    case 2: return pactl;
    default: return pbctl;
  }
}
ATARI_RAMFUNC void Atari::piaWrite(int r, uint8_t v) {
  switch (r) {
    case 0: if (pactl & 4) porta = v; else ddra = v; break;
    case 1: if (pbctl & 4) portb = v; else ddrb = v; remap(); break;
    case 2: pactl = v; break;
    default: pbctl = v; break;
  }
}

ATARI_RAMFUNC uint8_t Atari::gtiaRead(int r) {
  if (r < 4)  return mpf[r] & 0x0F;
  if (r < 8)  return ppf[r - 4] & 0x0F;
  if (r < 12) return mpl[r - 8] & 0x0F;
  if (r < 16) return ppl[r - 12] & 0x0F;
  if (r < 20) return (r == 16) ? (g_trig0 & 1) : 1;
  if (r == 0x14) return ATARI_PAL ? 0x01 : 0x0F;
  if (r == 0x1F) return ((g_consol & ~consolForce) & 7) | 8;
  return 0x0F;
}
ATARI_RAMFUNC void Atari::gtiaWrite(int r, uint8_t v) {
  if (r < 4)       hposp[r] = v;
  else if (r < 8)  hposm[r - 4] = v;
  else if (r < 12) sizep[r - 8] = v & 3;
  else if (r == 12) sizem = v;
  else if (r < 17) grafp[r - 13] = v;
  else if (r == 17) grafm = v;
  else if (r < 22) colpm[r - 18] = v;
  else if (r < 26) colpf[r - 22] = v;
  else switch (r) {
    case 26: colbk = v; break;
    case 27: prior = v; break;
    case 28: vdelay = v; break;
    case 29: gractl = v; break;
    case 30: memset(mpf, 0, 4); memset(ppf, 0, 4); memset(mpl, 0, 4); memset(ppl, 0, 4); break;
    case 31: spk = v & 8; break;
  }
}

ATARI_RAMFUNC uint8_t Atari::anticRegRead(int r) {
  switch (r) {
    case 0x0B: return scan >> 1;
    case 0x0C: case 0x0D: return 0;
    case 0x0F: return nmist | 0x1F;
    default: return 0xFF;
  }
}
ATARI_RAMFUNC void Atari::anticRegWrite(int r, uint8_t v) {
  switch (r) {
    case 0: dmactl = v; break;
    case 1: chactl = v; break;
    case 2: dlist = (dlist & 0xFF00) | v; break;
    case 3: dlist = (dlist & 0x00FF) | (v << 8); break;
    case 4: hscrol = v & 15; break;
    case 5: vscrol = v & 15; break;
    case 7: pmbase = v; break;
    case 9: chbase = v; break;
    case 10: wsyncReq = true; cpu.stop = true; break;
    case 14: nmien = v; break;
    case 15: nmist = 0; break;
  }
}

// ============================================================================
//  POKEY
// ============================================================================
void Atari::keyEvent(uint8_t code, bool down) {
  if (down) { kbcode = code; keyHeld = true; if (irqen & 0x40) { irqst &= ~0x40; updIrq(); } }
  else keyHeld = false;
}
void Atari::breakKey() { if (irqen & 0x80) { irqst &= ~0x80; updIrq(); } }

ATARI_RAMFUNC uint8_t Atari::pokeyRead(int r) {
  if (r < 8) return 228;                         // POT: nessun paddle
  switch (r) {
    case 8:  return 0x00;                        // ALLPOT
    case 9:  return kbcode;
    case 10: {                                    // RANDOM
      int rv = 0;
      if (audctl & 0x80) { int i = (b9 + curT) % 511;       for (int k = 0; k < 8; k++) rv |= P9[(i + k) % 511] << k; }
      else               { int i = (b17 + curT) % 131071;   for (int k = 0; k < 8; k++) { int j = (i + k) % 131071; rv |= ((P17[j >> 3] >> (j & 7)) & 1) << k; } }
      return ~rv;
    }
    case 13: return 0xFF;                         // SERIN
    case 14: return irqst;
    case 15: return 0xFF & ~(keyHeld ? 4 : 0) & ~(shift ? 8 : 0);
  }
  return 0xFF;
}
ATARI_RAMFUNC void Atari::pokeyWrite(int r, uint8_t v) {
  switch (r) {
    case 0: case 2: case 4: case 6: audf[r >> 1] = v; pokeyRecalc(); break;
    case 1: case 3: case 5: case 7: audc[r >> 1] = v; break;
    case 8: audctl = v; pokeyRecalc(); break;
    case 9: for (int i = 0; i < 4; i++) cnt[i] = per[i]; break;      // STIMER
    case 13:                                       // SEROUT: eventi, non livelli
      irqst |= 0x10;                               // scrivere cancella la richiesta "dato necessario"
      serNeed = 1;                                 // IRQ "dato necessario": tra pochi cicli
      serDone = (serByteCycles() + CYC_LINE - 1) / CYC_LINE;   // IRQ "trasmissione completa": dopo 1 byte
      break;
    case 14: irqen = v; irqst |= ~v; updIrq(); break;
    case 15: skctl = v; break;
    default: break;
  }
}
int Atari::serByteCycles() {
  int bit = 2 * per[3];                            // clock seriale: canali 3+4 in coppia (AUDF3/4)
  if (bit < 200) bit = 200;
  return 10 * bit;                                 // start + 8 dati + stop
}
// Il drive non c'e': simulo solo la trasmissione, nessuna risposta su SERIN (l'OS andra' in timeout)
ATARI_RAMFUNC void Atari::serialTick() {
  if (serNeed && --serNeed == 0 && (irqen & 0x10)) { irqst &= ~0x10; updIrq(); }
  if (serDone && --serDone == 0 && (irqen & 0x08)) { irqst &= ~0x08; updIrq(); }
}

void Atari::pokeyRecalc() {
  int base = (audctl & 1) ? 114 : 28;
  int c0 = (audctl & 0x40) ? 1 : base, c2 = (audctl & 0x20) ? 1 : base;
  auto p8 = [](int f, int clk) { return clk == 1 ? f + 4 : (f + 1) * clk; };
  if (audctl & 0x10) { int v = (audf[1] << 8) | audf[0]; per[1] = (c0 == 1) ? v + 7 : (v + 1) * c0; per[0] = 1; }
  else { per[0] = p8(audf[0], c0); per[1] = p8(audf[1], base); }
  if (audctl & 0x08) { int v = (audf[3] << 8) | audf[2]; per[3] = (c2 == 1) ? v + 7 : (v + 1) * c2; per[2] = 1; }
  else { per[2] = p8(audf[2], c2); per[3] = p8(audf[3], base); }
  for (int i = 0; i < 4; i++) if (cnt[i] > per[i]) cnt[i] = per[i];
}

// Avanza POKEY di n cicli CPU. Ritorna somma(volume*cicli) dei 4 canali (integrale del segnale).
ATARI_RAMFUNC int Atari::pokeyAdvance(int n) {
  int total = 0;
  for (int i = 0; i < 4; i++) {
    if ((i == 0 && (audctl & 0x10)) || (i == 2 && (audctl & 0x08))) continue;   // canale basso di una coppia a 16 bit
    const int vol = audc[i] & 15; const bool vo = audc[i] & 0x10;
    // Canali piu' veloci della banda audio (es. clock seriale SIO ~19kHz): inudibili e causa di
    // aliasing/click (gli scalini di volume diventano scalini DC), quindi contributo nullo.
    const bool ultra = !vo && per[i] < POKEY_ULTRA_PER;
    int rem = n, c = cnt[i], acc = 0;
    int lvl = vo ? vol * 2 : ultra ? 0 : (outv[i] ? vol * 2 : 0);
    while (rem >= c) {
      acc += lvl * c; rem -= c; c = per[i];
      const int off = n - rem;
      if (!vo && !ultra) {
        const uint8_t d = audc[i] & 0xE0;
        bool run = true;
        if (!(d & 0x80)) run = P5[(b5 + off) % 31];                     // il poly5 fa da gate
        if (run) {
          if (d == 0x00 || d == 0x80) {
            if (audctl & 0x80) outv[i] = P9[(b9 + off) % 511];
            else { int j = (b17 + off) % 131071; outv[i] = (P17[j >> 3] >> (j & 7)) & 1; }
          } else if (d == 0x40 || d == 0xC0) outv[i] = P4[(b4 + off) % 15];
          else outv[i] = !outv[i];                                       // tono puro
        }
        lvl = outv[i] ? vol * 2 : 0;
      }
      const int im = (i == 0) ? 1 : (i == 1) ? 2 : (i == 3) ? 4 : 0;   // timer 1,2,4
      if (im && (irqen & im)) { irqst &= ~im; updIrq(); }
    }
    cnt[i] = c - rem; acc += lvl * rem;
    total += acc;
  }
  b4 = (b4 + n) % 15; b5 = (b5 + n) % 31; b9 = (b9 + n) % 511; b17 = (b17 + n) % 131071;
  return total;
}

// Genera i campioni della riga (114 cicli) e li spinge su I2S (bloccante => sync a tempo reale)
ATARI_RAMFUNC void Atari::audioLine() {
  audAcc += (uint32_t)AUDIO_SAMPLE_RATE * CYC_LINE;
  int ns = 0; while (audAcc >= (uint32_t)CPU_HZ) { audAcc -= CPU_HZ; ns++; }
  if (ns == 0) { pokeyAdvance(CYC_LINE); return; }
  int left = CYC_LINE;
  for (int k = 0; k < ns; k++) {
    int len = (k == ns - 1) ? left : CYC_LINE / ns; left -= len;
    int sum = pokeyAdvance(len);
    int x = (sum * 128 / len) * AUDIO_GAIN;                 // sum e' in unita' doppie: 0 .. ~30720
    if (spk) x += 3000;
    int y = x - dcX + ((dcY * 251) >> 8); dcX = x; dcY = y;  // filtro DC
    if (y > 32767) y = 32767;
    if (y < -32768) y = -32768;
    audioPush((int16_t)y);
  }
}

// ============================================================================
//  ANTIC
// ============================================================================
ATARI_RAMFUNC uint8_t Atari::dlRead() {
  uint8_t v = *aptr(dlPtr);
  dlPtr = (dlPtr & 0xFC00) | ((dlPtr + 1) & 0x03FF);
  return v;
}

ATARI_RAMFUNC void Atari::pmFetch(int s) {
  if (!(dmactl & 0x0C)) return;
  const bool single = dmactl & 0x10;
  if (!single && (s & 1)) return;
  const uint16_t idx = single ? s : (s >> 1);
  const uint16_t base = single ? ((pmbase & 0xF8) << 8) : ((pmbase & 0xFC) << 8);
  const uint16_t mo = single ? 0x300 : 0x180, po = single ? 0x400 : 0x200, st = single ? 0x100 : 0x80;
  if (dmactl & 4) { uint8_t v = *aptr(base + mo + idx); if (gractl & 1) grafm = v; dma += 1; }
  if (dmactl & 8) {
    for (int i = 0; i < 4; i++) { uint8_t v = *aptr(base + po + i * st + idx); if (gractl & 2) grafp[i] = v; }
    dma += 4;
  }
}

ATARI_RAMFUNC void Atari::fetchInstr() {
  uint8_t op = dlRead(); dma += 1;
  uint8_t m = op & 15;
  mlDli = op & 0x80;
  if (m == 0) {                                        // righe vuote
    mlMode = 0; mlLine = 0; mlLast = (op >> 4) & 7; mlHs = false; prevVs = false;
  } else if (m == 1) {                                 // JMP / JVB
    uint8_t lo = dlRead(); uint8_t hi = dlRead(); dma += 2;
    dlPtr = lo | (hi << 8);
    if (op & 0x40) dlJvb = true;
    mlMode = 0; mlLine = 0; mlLast = 0; mlHs = false; prevVs = false;
  } else {
    if (op & 0x40) { uint8_t lo = dlRead(); uint8_t hi = dlRead(); scr = lo | (hi << 8); dma += 2; }
    const bool vs = op & 0x20;
    mlMode = m; mlHs = op & 0x10;
    mlLine = 0; mlLast = MI[m].height - 1;
    if (vs && !prevVs) { mlLine = vscrol; if (mlLine > mlLast) mlLine = mlLast; }
    if (!vs && prevVs) { if (vscrol < mlLast) mlLast = vscrol; }
    prevVs = vs;
    uint8_t wf = dmactl & 3, wfe = wf;
    if (mlHs && wf && wf < 3) wfe = wf + 1;
    rWf = wf; rWfe = wfe;
    int n = wfe ? (MI[m].bpl * WF[wfe]) / 40 : 0;
    for (int i = 0; i < n; i++) lineData[i] = *aptr((scr & 0xF000) | ((scr + i) & 0x0FFF));
    scr = (scr & 0xF000) | ((scr + n) & 0x0FFF);
    rBytes = n; dma += n;
  }
  mlActive = true;
}

ATARI_RAMFUNC void Atari::anticLine(int s) {
  dma = 0; dliNow = false; rMode = 0;
  if (s == 0) { dlPtr = dlist; dlJvb = false; mlActive = false; prevVs = false; }
  if (s == 248) { nmist = (nmist & 0x3F) | 0x40; if (nmien & 0x40) cpu.nmiPending = true; }
  if (s >= 8 && s < 248) {
    pmFetch(s);
    if ((dmactl & 0x20) && !dlJvb) {
      if (!mlActive) fetchInstr();
      rMode = mlMode; rLine = mlLine; rHs = mlHs;
      dliNow = mlDli && (mlLine == mlLast);
      if (mlMode >= 2 && MI[mlMode].text) dma += rBytes;      // fetch glifi ad ogni riga
      mlLine++;
      if (mlLine > mlLast) mlActive = false;
    }
  }
  dma += 9;   // refresh DRAM
}

// ============================================================================
//  Rendering playfield / GTIA
// ============================================================================
ATARI_RAMFUNC void Atari::decode(uint8_t* pc, uint8_t* pb) {
  const int n = rBytes, L = rLine;
  const uint8_t bv[4] = {0, 1, 2, 4};
  const uint8_t cv[4] = {colbk, colpf[0], colpf[1], colpf[2]};
  switch (rMode) {
    case 2: case 3: {
      const uint8_t* cb = aptr((uint16_t)((chbase & 0xFC) << 8));
      const uint8_t lit = (colpf[2] & 0xF0) | (colpf[1] & 0x0F), bg = colpf[2];
      for (int i = 0; i < n; i++) {
        uint8_t ch = lineData[i], code = ch & 0x7F, g; int r = L;
        if (rMode == 3 && (code & 0x60) == 0x60) r = L - 2;   // discendenti
        if (r < 0 || r > 7) g = 0;
        else { if (chactl & 4) r = 7 - r; g = cb[code * 8 + r]; }
        if (ch & 0x80) { if (chactl & 1) g = 0; if (chactl & 2) g ^= 0xFF; }
        for (int b = 7; b >= 0; b--) { *pc++ = ((g >> b) & 1) ? lit : bg; *pb++ = 4; }
      }
    } break;
    case 4: case 5: {
      const uint8_t* cb = aptr((uint16_t)((chbase & 0xFC) << 8));
      for (int i = 0; i < n; i++) {
        uint8_t ch = lineData[i]; int r = (rMode == 5) ? (L >> 1) : L; if (chactl & 4) r = 7 - r;
        uint8_t g = cb[(ch & 0x7F) * 8 + r];
        for (int k = 3; k >= 0; k--) {
          int v = (g >> (2 * k)) & 3;
          if (v == 3) { if (ch & 0x80) PUT(2, colpf[3], 8); else PUT(2, colpf[2], 4); }
          else PUT(2, cv[v], bv[v]);
        }
      }
    } break;
    case 6: case 7: {
      const uint8_t* cb = aptr((uint16_t)((chbase & 0xFE) << 8));
      for (int i = 0; i < n; i++) {
        uint8_t ch = lineData[i]; int r = (rMode == 7) ? (L >> 1) : L; if (chactl & 4) r = 7 - r;
        uint8_t g = cb[(ch & 0x3F) * 8 + r]; int ci = ch >> 6;
        for (int b = 7; b >= 0; b--) { if ((g >> b) & 1) PUT(2, colpf[ci], 1 << ci); else PUT(2, colbk, 0); }
      }
    } break;
    case 8:  for (int i = 0; i < n; i++) for (int k = 3; k >= 0; k--) { int v = (lineData[i] >> (2 * k)) & 3; PUT(8, cv[v], bv[v]); } break;
    case 9:  for (int i = 0; i < n; i++) for (int k = 7; k >= 0; k--) { int v = (lineData[i] >> k) & 1; PUT(4, v ? colpf[0] : colbk, v); } break;
    case 10: for (int i = 0; i < n; i++) for (int k = 3; k >= 0; k--) { int v = (lineData[i] >> (2 * k)) & 3; PUT(4, cv[v], bv[v]); } break;
    case 11: case 12: for (int i = 0; i < n; i++) for (int k = 7; k >= 0; k--) { int v = (lineData[i] >> k) & 1; PUT(2, v ? colpf[0] : colbk, v); } break;
    case 13: case 14: for (int i = 0; i < n; i++) for (int k = 3; k >= 0; k--) { int v = (lineData[i] >> (2 * k)) & 3; PUT(2, cv[v], bv[v]); } break;
    case 15: {
      const uint8_t lit = (colpf[2] & 0xF0) | (colpf[1] & 0x0F), bg = colpf[2];
      for (int i = 0; i < n; i++) for (int k = 7; k >= 0; k--) { *pc++ = ((lineData[i] >> k) & 1) ? lit : bg; *pb++ = 4; }
    } break;
  }
}

ATARI_RAMFUNC void Atari::composePM(uint8_t* row) {
  memset(s_pm, 0, FB_W);
  for (int i = 0; i < 4; i++) {
    uint8_t g = grafp[i]; if (!g) continue;
    int mult = (sizep[i] == 1) ? 2 : (sizep[i] == 3) ? 4 : 1, w = mult * 2;
    int xs = ((int)hposp[i] - 48) * 2;
    for (int j = 0; j < 8; j++) if (g & (0x80 >> j)) {
      int x0 = xs + j * w; for (int x = x0; x < x0 + w; x++) if (x >= 0 && x < FB_W) s_pm[x] |= 1 << i;
    }
  }
  for (int m = 0; m < 4; m++) {
    uint8_t g = (grafm >> (2 * m)) & 3; if (!g) continue;
    int sz = (sizem >> (2 * m)) & 3, mult = (sz == 1) ? 2 : (sz == 3) ? 4 : 1, w = mult * 2;
    int xs = ((int)hposm[m] - 48) * 2;
    for (int j = 0; j < 2; j++) if (g & (2 >> j)) {
      int x0 = xs + j * w; for (int x = x0; x < x0 + w; x++) if (x >= 0 && x < FB_W) s_pm[x] |= 0x10 << m;
    }
  }
  const bool fifth = prior & 0x10, multi = prior & 0x20;
  const uint8_t mode = (prior & 1) ? 1 : (prior & 2) ? 2 : (prior & 4) ? 4 : (prior & 8) ? 8 : 1;
  for (int x = 0; x < FB_W; x++) {
    uint8_t m = s_pm[x]; if (!m) continue;
    uint8_t pf = s_rowBits[x], pl = m & 0x0F, ms = m >> 4;
    for (int i = 0; i < 4; i++) {                             // collisioni
      if (pl & (1 << i)) { ppf[i] |= pf; ppl[i] |= pl; }
      if (ms & (1 << i)) { mpf[i] |= pf; mpl[i] |= pl; }
    }
    uint8_t p = pl; if (!fifth) p |= ms;
    uint8_t col = row[x], bits = pf;
    if (fifth && ms && !(pf & 7)) { col = colpf[3]; bits = 8; }
    if (p) {
      int lo = 0; while (!(p & (1 << lo))) lo++;
      uint8_t pcol = colpm[lo];
      if (multi) { if ((p & 3) == 3) pcol = colpm[0] | colpm[1]; else if ((p & 0xC) == 0xC) pcol = colpm[2] | colpm[3]; }
      bool win;
      if (!bits) win = true;
      else switch (mode) {
        case 1: win = true; break;
        case 2: win = (p & 3) != 0; break;
        case 4: win = false; break;
        default: win = !(bits & 3); break;
      }
      if (win) col = pcol;
    }
    row[x] = col;
  }
}

ATARI_RAMFUNC void Atari::renderLine(int s) {
  if (s < 8 || s >= 248) return;                 // finestra visibile: righe 8..247 = 240 righe
  const bool pmOn = (grafp[0] | grafp[1] | grafp[2] | grafp[3] | grafm) != 0;
  if (!drawFrame && !(COLLISIONS_ON_SKIPPED && pmOn)) return;
  uint8_t* row = s_scratch;                       // si compone sempre qui; poi si confronta col framebuffer
  if (pmOn) memset(s_rowBits, 0, FB_W);
  if (rMode < 2 || rWf == 0 || rBytes == 0) {
    memset(row, colbk, FB_W);
  } else {
    int org = X0[rWfe] + (rHs ? 2 * hscrol : 0);
    decode(s_pfc + 32 + org, s_pfb + 32 + org);
    int cl0 = X0[rWf], cl1 = X0[rWf] + WPX[rWf]; if (cl0 < 0) cl0 = 0; if (cl1 > FB_W) cl1 = FB_W;
    if (cl0 > 0) memset(row, colbk, cl0);
    if (cl1 < FB_W) memset(row + cl1, colbk, FB_W - cl1);
    memcpy(row + cl0, s_pfc + 32 + cl0, cl1 - cl0);
    if (pmOn) memcpy(s_rowBits + cl0, s_pfb + 32 + cl0, cl1 - cl0);
  }
  if (pmOn) composePM(row);
  if (drawFrame) {                                 // scrive nel framebuffer solo le righe cambiate e segna il blocco
    uint32_t* d = (uint32_t*)(g_fb + (s - 8) * FB_W); const uint32_t* n = (const uint32_t*)s_scratch;
    int i = 0; while (i < FB_W / 4 && d[i] == n[i]) i++;
    if (i < FB_W / 4) { memcpy(d, n, FB_W); g_fbDirty |= 1u << ((s - 8) / TFT_CHUNK_LINES); }
  }
}

// ============================================================================
//  Ciclo principale (core0)
// ============================================================================
ATARI_RAMFUNC void Atari::runLine(int s) {
  scan = s; cpu.stop = false;
  anticLine(s);
  int limit = CYC_LINE - dma; if (limit < 4) limit = 4;
  const int rpoint = (limit > 12) ? limit - 10 : limit / 2;      // fine area visibile (cc 208)
  const int dliAt  = rpoint < 8 ? rpoint : 8;
  int t = carry; bool rendered = false;
  if (nextWsync) { nextWsync = false; if (t < rpoint) t = rpoint; }

  auto runTo = [&](int target) {
    while (t < target) {
      curT = t;
      t += cpu.execCPU(target - t);
      if (cpu.trapHit) { cpu.trapHit = false; if (host) host->onTrap(*this, cpu.pc); else cpu.trapSkip = true; }
      if (cpu.stop) {
        cpu.stop = false;
        if (wsyncReq) {
          wsyncReq = false;
          if (t < rpoint) t = rpoint;                      // WSYNC: si ferma fino a fine area visibile
          else { if (t < limit) t = limit; nextWsync = true; }   // oltre: aspetta la riga dopo
        }
      }
    }
  };

  if (dliNow) {
    runTo(dliAt);
    nmist |= 0x80; if (nmien & 0x80) cpu.nmiPending = true;      // DLI
  }
  runTo(rpoint);
  renderLine(s); rendered = true;
  runTo(limit);
  carry = t - limit; if (carry < 0) carry = 0;
  (void)rendered;
  if (cpu.pc >= 0xA000 && cpu.pc < 0xC000) statBasic++;
  serialTick();
  audioLine();
}

void Atari::runFrame() {
  drawFrame = ((frameNo % (VIDEO_FRAMESKIP + 1)) == 0) && (g_fbState == 0);
  frameNo++; statFrames++; statTotalFrames++; if (drawFrame) statDrawn++;
  for (int s = 0; s < SCANLINES; s++) runLine(s);
  if (drawFrame) { __sync_synchronize(); g_fbState = 1; }
}

// ============================================================================
//  Diagnostica
// ============================================================================
void Atari::debugState(char* b, int n) {
  int k = 0;
  k += snprintf(b + k, n - k, "PC=%04X A=%02X X=%02X Y=%02X P=%02X SP=%02X PORTB=%02X jam=%d\n",
                cpu.pc, cpu.a, cpu.x, cpu.y, cpu.p, cpu.sp, (uint8_t)(portb | ~ddrb), cpu.jammed);
  k += snprintf(b + k, n - k, "DMACTL=%02X CHACTL=%02X CHBASE=%02X DLIST=%04X NMIEN=%02X IRQEN=%02X IRQST=%02X\n",
                dmactl, chactl, chbase, dlist, nmien, irqen, irqst);
  k += snprintf(b + k, n - k, "COLPF0-3=%02X %02X %02X %02X COLBK=%02X PRIOR=%02X GRACTL=%02X\n",
                colpf[0], colpf[1], colpf[2], colpf[3], colbk, prior, gractl);
  k += snprintf(b + k, n - k, "AUDF=%02X %02X %02X %02X AUDC=%02X %02X %02X %02X AUDCTL=%02X SKCTL=%02X\n",
                audf[0], audf[1], audf[2], audf[3], audc[0], audc[1], audc[2], audc[3], audctl, skctl);
  k += snprintf(b + k, n - k, "boot1[0..3]=%02X %02X %02X %02X  boot1[1FFA..1FFF]=%02X %02X %02X %02X %02X %02X  righe-con-PC-in-A000..BFFF=%lu\n",
                boot1[0], boot1[1], boot1[2], boot1[3], boot1[0x1FFA], boot1[0x1FFB], boot1[0x1FFC], boot1[0x1FFD], boot1[0x1FFE], boot1[0x1FFF], (unsigned long)statBasic);
  k += snprintf(b + k, n - k, "OS: boot0[3FFA..3FFF]=%02X %02X %02X %02X %02X %02X  RAM: 01=%02X 08=%02X 09=%02X 0A/B(DOSVEC)=%02X%02X 0C/D(DOSINI)=%02X%02X 6A(RAMTOP)=%02X 244(COLDST)=%02X 2E0/1(RUNAD)=%02X%02X\n",
                boot0[0x3FFA], boot0[0x3FFB], boot0[0x3FFC], boot0[0x3FFD], boot0[0x3FFE], boot0[0x3FFF],
                ram[1], ram[8], ram[9], ram[0x0B], ram[0x0A], ram[0x0D], ram[0x0C], ram[0x6A], ram[0x244], ram[0x2E1], ram[0x2E0]);
  k += snprintf(b + k, n - k, "DL:");
  uint16_t p = dlist, lms = 0; bool found = false;
  for (int i = 0; i < 14 && k < n - 40; i++) {
    uint8_t op = *aptr(p); k += snprintf(b + k, n - k, " %02X", op);
    uint8_t m = op & 15;
    if (m == 1) { k += snprintf(b + k, n - k, " %02X %02X", *aptr(p + 1), *aptr(p + 2)); break; }
    if (m >= 2 && (op & 0x40)) {
      uint16_t a = *aptr(p + 1) | (*aptr(p + 2) << 8);
      k += snprintf(b + k, n - k, "(@%04X)", a);
      if (!found) { lms = a; found = true; }
      p += 3;
    } else p++;
  }
  if (found) {
    k += snprintf(b + k, n - k, "\nSCR@%04X:", lms);
    for (int i = 0; i < 40 && k < n - 8; i++) k += snprintf(b + k, n - k, " %02X", *aptr(lms + i));
    k += snprintf(b + k, n - k, "\nTXT: ");
    for (int i = 0; i < 40 && k < n - 4; i++) {
      uint8_t c = *aptr(lms + i) & 0x7F; char ch = (c < 0x40) ? c + 0x20 : (c < 0x60) ? c - 0x40 : (c < 0x7F ? c : '.');
      b[k++] = (ch >= 32 && ch < 127) ? ch : '.';
    }
    b[k] = 0;
  }
  if (k < n - 2) { b[k++] = '\n'; b[k] = 0; }
}