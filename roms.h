#pragma once
// ============================================================================
//  ROM (non incluse: copyright)
//  boot0 = OS 800XL, 16384 byte  -> $C000-$FFFF (contiene anche il self-test a offset $1000)
//  boot1 = BASIC,     8192 byte  -> $A000-$BFFF
//  Le const finiscono in flash (XIP): non occupano RAM.
// ============================================================================
const unsigned char boot0[16384] = { 
  0x00  //add here OS 800XL rom dump
 };   

const unsigned char boot1[8192]  = { 
  0x00	//add here BASIC rom dump
 };   
