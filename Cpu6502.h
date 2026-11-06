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
 *
 * Cpu6502 - 6502 NMOS con bus a tabelle di pagina (RAM/ROM = puntatore, I/O = callback)
 * API pubblica allineata alla classe cpu di Pi64: execCPU(), resetCPU(), nmi(), irq
 * 
 */
#pragma once
#include <stdint.h>
#ifndef ATARI_RAMFUNC
#define ATARI_RAMFUNC __attribute__((section(".time_critical.atari"), noinline))
#endif

class Cpu6502 {
public:
  typedef uint8_t (*IoRead)(void*, uint16_t);
  typedef void    (*IoWrite)(void*, uint16_t, uint8_t);

  const uint8_t* const* rdp = nullptr;   // 256 puntatori pagina in lettura (nullptr = I/O)
  uint8_t* const*       wrp = nullptr;   // 256 puntatori pagina in scrittura (nullptr = I/O)
  IoRead  ioRead  = nullptr;
  IoWrite ioWrite = nullptr;
  void*   ctx     = nullptr;

  uint16_t pc = 0;
  uint8_t  a = 0, x = 0, y = 0, sp = 0xFD, p = 0x34;
  bool irqLine = false;      // livello IRQ (gestito dalla macchina)
  bool nmiPending = false;   // fronte NMI
  bool stop = false;         // la macchina lo alza (es. WSYNC) per interrompere execCPU
  // Trap: se pc cade in [trapBase, trapBase+trapRange) execCPU esce con trapHit=true PRIMA di eseguire (usato da SIOV/XEX).
  // Il gestore deve cambiare pc, oppure impostare trapSkip per eseguire normalmente l'istruzione.
  uint16_t trapBase = 0, trapRange = 0;
  bool trapHit = false, trapSkip = false;
  bool jammed = false;       // opcode JAM eseguito: la CPU resta bloccata fino a resetCPU()

  static void initTables();
  void resetCPU();
  // Esegue istruzioni finche' i cicli consumati >= budget (o stop). Ritorna i cicli consumati.
  int  execCPU(int budget);
  void nmi() { nmiPending = true; }
  uint16_t getpc() const { return pc; }

  inline uint8_t rd(uint16_t ad) { const uint8_t* q = rdp[ad >> 8]; return q ? q[ad & 0xFF] : ioRead(ctx, ad); }
  inline void    wr(uint16_t ad, uint8_t v) { uint8_t* q = wrp[ad >> 8]; if (q) q[ad & 0xFF] = v; else ioWrite(ctx, ad, v); }

private:
  inline void push(uint8_t v) { wr(0x100 + sp, v); sp--; }
  inline uint8_t pull() { sp++; return rd(0x100 + sp); }
  void adc(uint8_t v);
  void sbc(uint8_t v);
  void interrupt(uint16_t vec, bool brk);
};