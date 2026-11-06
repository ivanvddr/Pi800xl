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
#include "Cpu6502.h"

enum { M_IMP, M_ACC, M_IMM, M_ZP, M_ZPX, M_ZPY, M_ABS, M_ABX, M_ABY, M_INX, M_INY, M_REL, M_IND };
enum { O_NOP, O_LDA, O_LDX, O_LDY, O_STA, O_STX, O_STY, O_ADC, O_SBC, O_AND, O_ORA, O_EOR, O_CMP, O_CPX, O_CPY, O_BIT,
       O_ASL, O_LSR, O_ROL, O_ROR, O_INC, O_DEC, O_INX, O_INY, O_DEX, O_DEY, O_TAX, O_TAY, O_TXA, O_TYA, O_TSX, O_TXS,
       O_PHA, O_PLA, O_PHP, O_PLP, O_JMP, O_JSR, O_RTS, O_RTI, O_BRK, O_CLC, O_SEC, O_CLI, O_SEI, O_CLD, O_SED, O_CLV,
       O_BR, O_JAM, O_SLO, O_RLA, O_SRE, O_RRA, O_SAX, O_LAX, O_DCP, O_ISC };

// tabelle in RAM (non const) per evitare accessi alla flash nel ciclo caldo
static uint8_t T_mode[256], T_op[256], T_cyc[256], T_px[256], T_nz[256];
static bool s_init = false;

static int kindOf(int o) {  // 0 = lettura, 1 = scrittura, 2 = read-modify-write
  switch (o) {
    case O_STA: case O_STX: case O_STY: case O_SAX: return 1;
    case O_ASL: case O_LSR: case O_ROL: case O_ROR: case O_INC: case O_DEC:
    case O_SLO: case O_RLA: case O_SRE: case O_RRA: case O_DCP: case O_ISC: return 2;
    default: return 0;
  }
}
static void def(int op, int o, int m) {
  T_op[op] = o; T_mode[op] = m;
  int k = kindOf(o), c = 2, px = 0;
  switch (m) {
    case M_ZP:  c = (k == 2) ? 5 : 3; break;
    case M_ZPX: case M_ZPY: c = (k == 2) ? 6 : 4; break;
    case M_ABS: c = (k == 2) ? 6 : 4; break;
    case M_ABX: case M_ABY: c = (k == 2) ? 7 : (k == 1 ? 5 : 4); px = (k == 0); break;
    case M_INX: c = (k == 2) ? 8 : 6; break;
    case M_INY: c = (k == 2) ? 8 : (k == 1 ? 6 : 5); px = (k == 0); break;
    default: c = 2;
  }
  T_cyc[op] = c; T_px[op] = px;
}
static void defc(int op, int o, int m, int cyc) { def(op, o, m); T_cyc[op] = cyc; }

void Cpu6502::initTables() {
  if (s_init) return;
  s_init = true;
  for (int i = 0; i < 256; i++) T_nz[i] = (i & 0x80) | (i == 0 ? 2 : 0);
  static const uint8_t o1[8] = {O_ORA, O_AND, O_EOR, O_ADC, O_STA, O_LDA, O_CMP, O_SBC};
  static const uint8_t m1[8] = {M_INX, M_ZP, M_IMM, M_ABS, M_INY, M_ZPX, M_ABY, M_ABX};
  static const uint8_t o2[8] = {O_ASL, O_ROL, O_LSR, O_ROR, O_STX, O_LDX, O_DEC, O_INC};
  static const uint8_t m2[8] = {M_IMM, M_ZP, M_ACC, M_ABS, M_IMP, M_ZPX, M_IMP, M_ABX};
  static const uint8_t o3[8] = {O_SLO, O_RLA, O_SRE, O_RRA, O_SAX, O_LAX, O_DCP, O_ISC};
  for (int op = 0; op < 256; op++) {
    int aaa = op >> 5, bbb = (op >> 2) & 7, cc = op & 3;
    def(op, O_NOP, M_IMP);
    if ((op & 0x1F) == 0x10) { defc(op, O_BR, M_REL, 2); continue; }
    if (cc == 1) {
      if (op == 0x89) def(op, O_NOP, M_IMM); else def(op, o1[aaa], m1[bbb]);
    } else if (cc == 2) {
      if (bbb == 4 || bbb == 6) def(op, O_NOP, M_IMP);
      else if (bbb == 0) def(op, aaa == 5 ? O_LDX : O_NOP, M_IMM);
      else {
        int m = m2[bbb];
        if (aaa == 4 || aaa == 5) { if (bbb == 5) m = M_ZPY; if (bbb == 7) m = M_ABY; }
        if (aaa == 4 && bbb == 7) def(op, O_NOP, M_ABY); else def(op, o2[aaa], m);
      }
    } else if (cc == 0) {
      switch (bbb) {
        case 0: def(op, aaa == 5 ? O_LDY : aaa == 6 ? O_CPY : aaa == 7 ? O_CPX : O_NOP, M_IMM); break;
        case 1: def(op, aaa == 1 ? O_BIT : aaa == 4 ? O_STY : aaa == 5 ? O_LDY : aaa == 6 ? O_CPY : aaa == 7 ? O_CPX : O_NOP, M_ZP); break;
        case 3: def(op, aaa == 1 ? O_BIT : aaa == 2 ? O_JMP : aaa == 3 ? O_JMP : aaa == 4 ? O_STY : aaa == 5 ? O_LDY : aaa == 6 ? O_CPY : aaa == 7 ? O_CPX : O_NOP,
                    aaa == 3 ? M_IND : M_ABS); break;
        case 5: def(op, aaa == 4 ? O_STY : aaa == 5 ? O_LDY : O_NOP, M_ZPX); break;
        case 7: def(op, aaa == 5 ? O_LDY : O_NOP, M_ABX); break;
        default: break;
      }
    } else { // cc == 3 : illegali
      int m = m1[bbb];
      if (aaa == 4 || aaa == 5) { if (bbb == 5) m = M_ZPY; if (bbb == 7) m = M_ABY; }
      if (bbb == 2) def(op, op == 0xEB ? O_SBC : O_NOP, M_IMM);
      else if (aaa == 4 && (bbb == 4 || bbb == 6 || bbb == 7)) def(op, O_NOP, bbb == 4 ? M_INY : M_ABY);
      else def(op, o3[aaa], m);
    }
  }
  defc(0x00, O_BRK, M_IMP, 7); defc(0x20, O_JSR, M_ABS, 6); defc(0x40, O_RTI, M_IMP, 6); defc(0x60, O_RTS, M_IMP, 6);
  defc(0x4C, O_JMP, M_ABS, 3); defc(0x6C, O_JMP, M_IND, 5);
  defc(0x08, O_PHP, M_IMP, 3); defc(0x28, O_PLP, M_IMP, 4); defc(0x48, O_PHA, M_IMP, 3); defc(0x68, O_PLA, M_IMP, 4);
  defc(0x88, O_DEY, M_IMP, 2); defc(0xA8, O_TAY, M_IMP, 2); defc(0xC8, O_INY, M_IMP, 2); defc(0xE8, O_INX, M_IMP, 2);
  defc(0x18, O_CLC, M_IMP, 2); defc(0x38, O_SEC, M_IMP, 2); defc(0x58, O_CLI, M_IMP, 2); defc(0x78, O_SEI, M_IMP, 2);
  defc(0x98, O_TYA, M_IMP, 2); defc(0xB8, O_CLV, M_IMP, 2); defc(0xD8, O_CLD, M_IMP, 2); defc(0xF8, O_SED, M_IMP, 2);
  defc(0x8A, O_TXA, M_IMP, 2); defc(0x9A, O_TXS, M_IMP, 2); defc(0xAA, O_TAX, M_IMP, 2); defc(0xBA, O_TSX, M_IMP, 2);
  defc(0xCA, O_DEX, M_IMP, 2); defc(0xEA, O_NOP, M_IMP, 2);
  // JAM (KIL): il 6502 reale si blocca e solo il reset lo sblocca (nemmeno NMI/IRQ)
  static const uint8_t jam[12] = {0x02,0x12,0x22,0x32,0x42,0x52,0x62,0x72,0x92,0xB2,0xD2,0xF2};
  for (uint8_t op : jam) defc(op, O_JAM, M_IMP, 2);
}

void Cpu6502::resetCPU() {
  initTables();
  sp = 0xFD; p = 0x34; a = x = y = 0; nmiPending = false; stop = false; jammed = false;
  pc = rd(0xFFFC) | (rd(0xFFFD) << 8);
}

#define SETNZ(v) (p = (p & 0x7D) | T_nz[(uint8_t)(v)])

inline __attribute__((always_inline)) void Cpu6502::adc(uint8_t v) {
  unsigned c = p & 1;
  if (p & 8) {
    unsigned al = (a & 0x0F) + (v & 0x0F) + c;
    if (al >= 0x0A) al = ((al + 6) & 0x0F) + 0x10;
    unsigned s = (a & 0xF0) + (v & 0xF0) + al;
    unsigned bin = a + v + c;
    p &= ~0xC3;
    if ((bin & 0xFF) == 0) p |= 2;
    p |= (s & 0x80);
    if ((a ^ s) & (v ^ s) & 0x80) p |= 0x40;
    if (s >= 0xA0) s += 0x60;
    if (s >= 0x100) p |= 1;
    a = s & 0xFF;
  } else {
    unsigned s = a + v + c;
    p &= ~0xC3;
    p |= (s > 0xFF) | (((~(a ^ v)) & (a ^ s) & 0x80) ? 0x40 : 0) | T_nz[s & 0xFF];
    a = s & 0xFF;
  }
}
inline __attribute__((always_inline)) void Cpu6502::sbc(uint8_t v) {
  unsigned c = p & 1;
  unsigned t = a + (uint8_t)~v + c;           // risultato binario (flag identici in decimale NMOS)
  uint8_t res = t & 0xFF;
  uint8_t nf = (p & ~0xC3) | (t > 0xFF) | (((a ^ v) & (a ^ t) & 0x80) ? 0x40 : 0) | T_nz[res];
  if (p & 8) {
    int al = (a & 0x0F) - (v & 0x0F) + (int)c - 1;
    if (al < 0) al = ((al - 6) & 0x0F) - 0x10;
    int s = (a & 0xF0) - (v & 0xF0) + al;
    if (s < 0) s -= 0x60;
    res = s & 0xFF;
  }
  p = nf; a = res;
}
ATARI_RAMFUNC void Cpu6502::interrupt(uint16_t vec, bool brk) {
  push(pc >> 8); push(pc & 0xFF);
  push(brk ? (p | 0x30) : ((p | 0x20) & ~0x10));
  p |= 4;
  pc = rd(vec) | (rd(vec + 1) << 8);
}

ATARI_RAMFUNC int Cpu6502::execCPU(int budget) {
  if (jammed) return budget;               // bus bloccato: consuma il budget senza eseguire nulla
  int cyc = 0;
  while (cyc < budget && !stop) {
    if (nmiPending) { nmiPending = false; interrupt(0xFFFA, false); cyc += 7; continue; }
    if (irqLine && !(p & 4)) { interrupt(0xFFFE, false); cyc += 7; continue; }

    if ((uint16_t)(pc - trapBase) < trapRange) {                 // nessun costo se trapRange == 0
      if (trapSkip) trapSkip = false; else { trapHit = true; break; }
    }
    uint8_t op = rd(pc++);
    const uint8_t m = T_mode[op], o = T_op[op];
    int c = T_cyc[op];
    uint16_t ea = 0, base; uint8_t v; unsigned t;
    switch (m) {
      case M_IMM: ea = pc++; break;
      case M_ZP:  ea = rd(pc++); break;
      case M_ZPX: ea = (rd(pc++) + x) & 0xFF; break;
      case M_ZPY: ea = (rd(pc++) + y) & 0xFF; break;
      case M_ABS: ea = rd(pc) | (rd(pc + 1) << 8); pc += 2; break;
      case M_ABX: base = rd(pc) | (rd(pc + 1) << 8); pc += 2; ea = base + x; if (T_px[op] && ((base ^ ea) & 0xFF00)) c++; break;
      case M_ABY: base = rd(pc) | (rd(pc + 1) << 8); pc += 2; ea = base + y; if (T_px[op] && ((base ^ ea) & 0xFF00)) c++; break;
      case M_INX: { uint8_t z = (rd(pc++) + x) & 0xFF; ea = rd(z) | (rd((z + 1) & 0xFF) << 8); } break;
      case M_INY: { uint8_t z = rd(pc++); base = rd(z) | (rd((z + 1) & 0xFF) << 8); ea = base + y;
                    if (T_px[op] && ((base ^ ea) & 0xFF00)) c++; } break;
      case M_REL: ea = pc++; break;
      case M_IND: { uint16_t ptr = rd(pc) | (rd(pc + 1) << 8); pc += 2;
                    ea = rd(ptr) | (rd((ptr & 0xFF00) | ((ptr + 1) & 0xFF)) << 8); } break;   // bug pagina NMOS
      default: break;
    }
#define RMW(EXPR) do { if (m == M_ACC) { v = a; EXPR; a = v; } else { v = rd(ea); EXPR; wr(ea, v); } } while (0)
#define ASL_() do { p = (p & ~1) | (v >> 7); v <<= 1; SETNZ(v); } while (0)
#define LSR_() do { p = (p & ~1) | (v & 1); v >>= 1; SETNZ(v); } while (0)
#define ROL_() do { uint8_t cc_ = p & 1; p = (p & ~1) | (v >> 7); v = (v << 1) | cc_; SETNZ(v); } while (0)
#define ROR_() do { uint8_t cc_ = p & 1; p = (p & ~1) | (v & 1); v = (v >> 1) | (cc_ << 7); SETNZ(v); } while (0)
#define CMP_(R) do { v = rd(ea); t = (unsigned)(R) - v; p = (p & ~1) | ((R) >= v ? 1 : 0); SETNZ(t); } while (0)
    switch (o) {
      case O_LDA: a = rd(ea); SETNZ(a); break;
      case O_LDX: x = rd(ea); SETNZ(x); break;
      case O_LDY: y = rd(ea); SETNZ(y); break;
      case O_STA: wr(ea, a); break;
      case O_STX: wr(ea, x); break;
      case O_STY: wr(ea, y); break;
      case O_ADC: adc(rd(ea)); break;
      case O_SBC: sbc(rd(ea)); break;
      case O_AND: a &= rd(ea); SETNZ(a); break;
      case O_ORA: a |= rd(ea); SETNZ(a); break;
      case O_EOR: a ^= rd(ea); SETNZ(a); break;
      case O_CMP: CMP_(a); break;
      case O_CPX: CMP_(x); break;
      case O_CPY: CMP_(y); break;
      case O_BIT: v = rd(ea); p = (p & 0x3D) | (v & 0xC0) | ((a & v) ? 0 : 2); break;
      case O_ASL: RMW(ASL_()); break;
      case O_LSR: RMW(LSR_()); break;
      case O_ROL: RMW(ROL_()); break;
      case O_ROR: RMW(ROR_()); break;
      case O_INC: v = rd(ea) + 1; wr(ea, v); SETNZ(v); break;
      case O_DEC: v = rd(ea) - 1; wr(ea, v); SETNZ(v); break;
      case O_INX: x++; SETNZ(x); break;
      case O_INY: y++; SETNZ(y); break;
      case O_DEX: x--; SETNZ(x); break;
      case O_DEY: y--; SETNZ(y); break;
      case O_TAX: x = a; SETNZ(x); break;
      case O_TAY: y = a; SETNZ(y); break;
      case O_TXA: a = x; SETNZ(a); break;
      case O_TYA: a = y; SETNZ(a); break;
      case O_TSX: x = sp; SETNZ(x); break;
      case O_TXS: sp = x; break;
      case O_PHA: push(a); break;
      case O_PLA: a = pull(); SETNZ(a); break;
      case O_PHP: push(p | 0x30); break;
      case O_PLP: p = (pull() & ~0x10) | 0x20; break;
      case O_JMP: pc = ea; break;
      case O_JSR: { uint16_t r = pc - 1; push(r >> 8); push(r & 0xFF); pc = ea; } break;
      case O_RTS: { uint8_t lo = pull(); uint8_t hi = pull(); pc = (lo | (hi << 8)) + 1; } break;
      case O_RTI: p = (pull() & ~0x10) | 0x20; { uint8_t lo = pull(); uint8_t hi = pull(); pc = lo | (hi << 8); } break;
      case O_BRK: pc++; interrupt(0xFFFE, true); break;
      case O_CLC: p &= ~1; break;   case O_SEC: p |= 1; break;
      case O_CLI: p &= ~4; break;   case O_SEI: p |= 4; break;
      case O_CLD: p &= ~8; break;   case O_SED: p |= 8; break;
      case O_CLV: p &= ~0x40; break;
      case O_JAM: jammed = true; pc--; break;
      case O_BR: {
        int8_t off = (int8_t)rd(ea);
        static const uint8_t mask[4] = {0x80, 0x40, 0x01, 0x02};
        bool flag = (p & mask[op >> 6]) != 0;
        if (flag == (bool)((op >> 5) & 1)) {
          uint16_t np = pc + off; c++; if ((np ^ pc) & 0xFF00) c++; pc = np;
        }
      } break;
      // illegali comuni
      case O_SLO: v = rd(ea); p = (p & ~1) | (v >> 7); v <<= 1; wr(ea, v); a |= v; SETNZ(a); break;
      case O_RLA: v = rd(ea); { uint8_t cc_ = p & 1; p = (p & ~1) | (v >> 7); v = (v << 1) | cc_; } wr(ea, v); a &= v; SETNZ(a); break;
      case O_SRE: v = rd(ea); p = (p & ~1) | (v & 1); v >>= 1; wr(ea, v); a ^= v; SETNZ(a); break;
      case O_RRA: v = rd(ea); { uint8_t cc_ = p & 1; p = (p & ~1) | (v & 1); v = (v >> 1) | (cc_ << 7); } wr(ea, v); adc(v); break;
      case O_SAX: wr(ea, a & x); break;
      case O_LAX: a = x = rd(ea); SETNZ(a); break;
      case O_DCP: v = rd(ea) - 1; wr(ea, v); t = (unsigned)a - v; p = (p & ~1) | (a >= v ? 1 : 0); SETNZ(t); break;
      case O_ISC: v = rd(ea) + 1; wr(ea, v); sbc(v); break;
      default: break;   // NOP (anche varianti con operando)
    }
    cyc += c;
  }
  return cyc;
}