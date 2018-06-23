// -----------------------------------------------------------------------------
// Processor Technology VDM-1 emulation for PIC32MX device
// Copyright (C) 2018 David Hansel
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
// -----------------------------------------------------------------------------


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "system_config.h"
#include "system_definitions.h"

#include "vdm1.h"
#include "charset.h"
#include "peripheral/oc/plib_oc.h"
#include "peripheral/tmr/plib_tmr.h"
#include "peripheral/int/plib_int.h"
#include "peripheral/spi/plib_spi.h"
#include "peripheral/dma/plib_dma.h"
#include "peripheral/bmx/plib_bmx.h"


// display is 64 columns and 16 rows
// each character is 9 pixels wide and 13 pixels high
#define DISPLAY_PIXELS (64*9)
#define DISPLAY_LINES  (16*13)



// Our pixel clock runs at 24MHz (maximum speed for SPI given an 8MHz crystal), 
// so each pixel takes 0.0416us. Spec is 25.175MHz, so we're slightly lower.
// We can still manage to get 763 (instead of 800 per spec) pixels into one line.
#define VGA_NUM_PIXELS	   763			   // =31.791us/line (spec: 31.777us)
#define VGA_HFP_LENGTH     (15+17)         // =0.625us (spec: 0.635us) plus 0.708us margin
#define VGA_HBP_LENGTH     (46+17)         // =1.916us (spec: 1.906us) plus 0.708us margin
#define VGA_HSYNC_LENGTH   92			   // =3.833us (spec: 3.813us)

// sanity check
#if (VGA_HFP_LENGTH+VGA_HBP_LENGTH+VGA_HSYNC_LENGTH+DISPLAY_PIXELS) != VGA_NUM_PIXELS
#error Inconsistent horizontal timing!
#endif

// All numbers for vertical timing are numbers of horizontal lines,
// each horizontal line is 0.031777ms.
// The 640x480 resolution gives us 480 visible lines but we only need 416
// So there are 64 lines of margin, which we split between the front and back porches.
#define VGA_NUM_LINES     525               // =16.67ms/frame (spec: 16.683ms)
#define VGA_VFP_LENGTH    (10+32)           // front porch plus margin
#define VGA_VBP_LENGTH    (33+32) 	        // back porch plus margin
#define VGA_VSYNC_LENGTH  2                 // =0.06358ms (spec: 0.06355ms)
 
// sanity check
#if (VGA_VFP_LENGTH+VGA_VBP_LENGTH+VGA_VSYNC_LENGTH+DISPLAY_LINES*2) != VGA_NUM_LINES
#error Inconsistent vertical timing!
#endif


// timing based on: https://www.maximintegrated.com/en/app-notes/index.mvp/id/734
// for composite output, SPI and timer2 run at 12MHz, i.e. ~0.083us per pixel
#define NTSC_NUM_PIXELS	    762			    // =63.5us/line (spec: 63.5us)
#define NTSC_HFP_LENGTH     (18+27)         // =1.5us  (spec: 1.5us) plus 2.25us margin
#define NTSC_HBP_LENGTH     (57+27)         // =4.75us (spec: 4.7us) plus 2.25us margin
#define NTSC_HSYNC_LENGTH   57			    // =4.75us (spec: 4.7us)

// sanity check
#if (NTSC_HFP_LENGTH+NTSC_HBP_LENGTH+NTSC_HSYNC_LENGTH+DISPLAY_PIXELS) != NTSC_NUM_PIXELS
#error Inconsistent horizontal timing!
#endif

// All numbers for vertical timing are numbers of horizontal lines,
// each horizontal line is 0.0635ms. We use "fake progressive" scanning where
// we only send the first field at a 60Hz frequency. This is achieved by starting
// the VSYNC pulse always at the beginning of a scan line (starting in the middle
// of a scan line would indicate the start of the second field).
// See: https://sagargv.blogspot.com/2014/07/ntsc-demystified-color-demo-with.html
#define NTSC_NUM_LINES     262             // =16.637ms/field = 60.1 fields/sec
#define NTSC_VFP_LENGTH    26
#define NTSC_VBP_LENGTH    27
#define NTSC_VSYNC_LENGTH  1
 
// sanity check
#if (NTSC_VFP_LENGTH+NTSC_VBP_LENGTH+NTSC_VSYNC_LENGTH+DISPLAY_LINES) != NTSC_NUM_LINES
#error Inconsistent vertical timing!
#endif


// timing based on: https://en.wikipedia.org/wiki/PAL
// for composite output, SPI and timer2 run at 12MHz, i.e. ~0.083us per pixel
#define PAL_NUM_PIXELS	   768	 		    // =64us/line (spec: 64us)
#define PAL_HFP_LENGTH     (20+24)          // =1.67us (spec: 1.65us) plus 2.5us margin
#define PAL_HBP_LENGTH     (68+23)          // =5.67us (spec: 5.7us) plus 2.5us margin
#define PAL_HSYNC_LENGTH   57			    // =4.75us (spec: 4.7us)

// sanity check
#if (PAL_HFP_LENGTH+PAL_HBP_LENGTH+PAL_HSYNC_LENGTH+DISPLAY_PIXELS) != PAL_NUM_PIXELS
#error Inconsistent horizontal timing!
#endif

// All numbers for vertical timing are numbers of horizontal lines,
// each horizontal line is 0.064ms. We use "fake progressive" scanning where
// we only send the first field at a 50Hz frequency. This is achieved by starting
// the VSYNC pulse always at the beginning of a scan line (starting in the middle
// of a scan line would indicate the start of the second field).
// See: https://en.wikipedia.org/wiki/PAL
#define PAL_NUM_LINES     312              // =19.968ms/field = 50.08 fields/sec
#define PAL_VFP_LENGTH    51
#define PAL_VBP_LENGTH    52
#define PAL_VSYNC_LENGTH  1
 
// sanity check
#if (PAL_VFP_LENGTH+PAL_VBP_LENGTH+PAL_VSYNC_LENGTH+DISPLAY_LINES) != PAL_NUM_LINES
#error Inconsistent vertical timing!
#endif


// VDM1 video memory:    
// can be read and written, contains the characters visible on screen
uint8_t vdm1_memory[16*64];


// VDM1 control register:
// 4 upper bits define the first line shown, all lines above this are blanked
// 4 lower bits define the top row on the screen. e.g. if 3 then the order
// of rows displayed on the screen is 3-15, 0, 1, 2
uint8_t vdm1_ctrl = 0;


// DIP switches (SW1-6 = bit 0-5):
// bit 0-1: off/off: all blank
//          off/on : normal video
//          on /off: inverse video
//          on /on : illegal
// bit 2-3: off/off: cursor characters (bit 7 on) shown regular
//          off/on : cursor characters blink at 2 Hz
//          on /off: cursor characters shown inverted
//          on /on : illegal
// bit 4-5: off/off: all characters blanked (cursor characters shown as blocks)
//          off/on : control characters (0-31) blanked, CR/VT blanking enabled
//          on /off: all characters shown, CR/VT blanking enabled
//          on /on : all characters shown, CR/VT blanking disabled
uint8_t vdm1_dip = 2+4+16+32;


// timing parameters (set in vdm1_set_timing)
int g_num_pixels, g_hsync_start, g_hfp_length, g_hbp_length, g_hsync_length;
int g_num_lines, g_vfp_length, g_vbp_length, g_vsync_length;


// character sets (one with cursors enabled, one with cursors disabled)
// "charset" points to the one currently in use
uint16_t charset_cursor[13*256];
uint16_t charset_nocursor[13*256];
uint16_t *charset = charset_cursor;

// g_composite=false => VGA output
bool g_composite = false;

volatile int  g_current_line      = 0;
volatile int  g_blank_before_row  = 0;
volatile int  g_scroll_rows       = 0;
volatile bool g_blank_all         = false;
volatile bool g_invert_all        = false;
volatile int  g_cursor            = 1; // 0=off, 1=on, 2=blink


// two line buffers: one being shown while rendering into the other
static uint32_t linebuffer1[DISPLAY_PIXELS/32], linebuffer2[DISPLAY_PIXELS/32];
uint32_t zeroWord = 0;

static void render_half_line_with_VTCR_blanking(int l, bool first, uint32_t *lbp);
static void render_half_line_no_VTCR_blanking(int l, bool first, uint32_t *lbp);
static void (*render_half_line)(int, bool, uint32_t *) = render_half_line_no_VTCR_blanking;


// initialize charset_cursor and charset_nocursor 
// characters with codes lower than blankTo will be blanked
void vdm1_set_charset(int blankTo)
{
    int r, ch;
    uint16_t d;
    for(r=0; r<13; r++)
      for(ch=0; ch<128; ch++) 
        {
          // top row and two rightmost columns of all characters are blank
          d = (ch<blankTo || r==0) ? 0 : (charset_7bit[ch][r-1]*4);
          charset_nocursor[r*256+ch]     = d;
          charset_nocursor[r*256+ch+128] = d;
          charset_cursor[r*256+ch]       = d;
          charset_cursor[r*256+ch+128]   = d^0x1ff;
         }
}


uint8_t vdm1_get_dip()
{
   return vdm1_dip;
}


void vdm1_set_dip(uint8_t v)
{
  // DIP switches 5+6: character blanking
  switch( v & 0x30 )
  {
      case 0x00: vdm1_set_charset(128); break; // all characters blanked
      case 0x10: vdm1_set_charset(0);   break; // no characters blanked
      case 0x20: vdm1_set_charset(32);  break; // control characters blanked
      case 0x30: vdm1_set_charset(0);   break; // no characters blanked
   }
  
  vdm1_dip = v;
}


inline void render_half_line_no_VTCR_blanking(int l, bool first, uint32_t *lbp)
{
    uint8_t *cc, r = l/13;
    const uint16_t *cp;
    uint32_t invert = g_invert_all ? 0xffffffff : 0, w;

    cp = &(charset[(l%13)*256]);
    cc = &(vdm1_memory[((r+g_scroll_rows)&15)*64]);

    if( !first ) { cc += 32; lbp += 9; }

    if( r < g_blank_before_row || g_blank_all ) { memset(lbp, invert, 9*4); return; }

    // this could be done in a loop but it's faster like this    
    w  = cp[  *cc] << 23;
    w |= cp[*++cc] << 14;
    w |= cp[*++cc] <<  5;
    w |= cp[*++cc] >>  4;
    *lbp = w ^ invert;
    w  = cp[  *cc] << 28;
    w |= cp[*++cc] << 19;
    w |= cp[*++cc] << 10;
    w |= cp[*++cc] <<  1;
    w |= cp[*++cc] >>  8;
    *++lbp = w ^ invert;
    w  = cp[  *cc] << 24;
    w |= cp[*++cc] << 15;
    w |= cp[*++cc] <<  6;
    w |= cp[*++cc] >>  3;
    *++lbp = w ^ invert;
    w  = cp[  *cc] << 29;
    w |= cp[*++cc] << 20;
    w |= cp[*++cc] << 11;
    w |= cp[*++cc] <<  2;
    w |= cp[*++cc] >>  7;
    *++lbp = w ^ invert;
    w  = cp[  *cc] << 25;
    w |= cp[*++cc] << 16;
    w |= cp[*++cc] <<  7;
    w |= cp[*++cc] >>  2;
    *++lbp = w ^ invert;
    w  = cp[  *cc] << 30;
    w |= cp[*++cc] << 21;
    w |= cp[*++cc] << 12;
    w |= cp[*++cc] <<  3;
    w |= cp[*++cc] >>  6;
    *++lbp = w ^ invert;
    w  = cp[  *cc] << 26;
    w |= cp[*++cc] << 17;
    w |= cp[*++cc] <<  8;
    w |= cp[*++cc] >>  1;
    *++lbp = w ^ invert;
    w  = cp[  *cc] << 31;
    w |= cp[*++cc] << 22;
    w |= cp[*++cc] << 13;
    w |= cp[*++cc] <<  4;
    w |= cp[*++cc] >>  5;
    *++lbp = w ^ invert;
    w  = cp[  *cc] << 27;
    w |= cp[*++cc] << 18;
    w |= cp[*++cc] <<  9;
    w |= cp[*++cc];
    *++lbp = w ^ invert;
}


inline void render_half_line_with_VTCR_blanking(int l, bool first, uint32_t *lbp)
{
    static bool hblanked = false, vblanked = false;
    uint8_t *cc, *cce, r = l/13;
    const uint16_t *cp;
    uint32_t w = 0, invert = g_invert_all ? 0xffffffff : 0, *lbe;

    cp = &(charset[(l%13)*256]);
    cc = &(vdm1_memory[((r+g_scroll_rows)&15)*64]);

    if( first )
      {
        hblanked = false;
        if( l==0 ) vblanked = false;
      }
    else
      { cc  += 32; lbp += 9; }
      
    lbe = lbp + 9;
    cce = cc + 32;
    if( r < g_blank_before_row || g_blank_all || vblanked ) { memset(lbp, invert, 9*4); return; }
    if( hblanked ) goto stop;

    // this could be done in a loop but it's faster like this    
    --lbp;
    w  = cp[  *cc] << 23; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] << 14; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] <<  5; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] >>  4;
    *++lbp = w ^ invert;
    w  = cp[  *cc] << 28; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] << 19; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] << 10; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] <<  1; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] >>  8;
    *++lbp = w ^ invert;
    w  = cp[  *cc] << 24; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] << 15; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] <<  6; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] >>  3; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    *++lbp = w ^ invert;
    w  = cp[  *cc] << 29; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] << 20; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] << 11; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] <<  2; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] >>  7;
    *++lbp = w ^ invert;
    w  = cp[  *cc] << 25; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] << 16; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] <<  7; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] >>  2;
    *++lbp = w ^ invert;
    w  = cp[  *cc] << 30; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] << 21; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] << 12; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] <<  3; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] >>  6;
    *++lbp = w ^ invert;
    w  = cp[  *cc] << 26; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] << 17; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] <<  8; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] >>  1;
    *++lbp = w ^ invert;
    w  = cp[  *cc] << 31; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] << 22; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] << 13; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] <<  4; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] >>  5;
    *++lbp = w ^ invert;
    w  = cp[  *cc] << 27; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] << 18; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc] <<  9; if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    w |= cp[*++cc];       if( (*cc&0x7f)==11 || (*cc&0x7f)==13 ) goto stop;
    *++lbp = w ^ invert;
    return;
          
  stop:
    // found either a VT or CR character.
    // write the data we've collected so far
    *++lbp = w ^ invert;
    // blank the rest of the half-line
    while( ++lbp<lbe ) *lbp=invert;
    // remember hblank for second half of line
    hblanked = true;

    // if this is the second half of the last scan line of this character row then check VT blanking
    if( (l%13)==12 && !first )
    {
      // see whether there's a VT blank on the current line after a CR (still counts)
      while( (*cc&0x7f)!=11 && ++cc<cce );
      // if we found a VT then blank the rest of the screen
      if( (*cc&0x7f)==11 && cc<cce ) vblanked = true;
    }
}


static void schedule_dma_line(int line)
{
  // set DMA channel 1 to start transfer when triggered (by timer2 overrun)
  PLIB_DMA_ChannelXTriggerEnable(DMA_ID_0, DMA_CHANNEL_1, DMA_CHANNEL_TRIGGER_TRANSFER_START);
  PLIB_DMA_ChannelXEnable(DMA_ID_0, DMA_CHANNEL_1);

  // point DMA channel 0 to start address of line buffer
  if( g_composite )
    PLIB_DMA_ChannelXSourceStartAddressSet(DMA_ID_0, DMA_CHANNEL_0, (uint32_t) (line & 1 ? linebuffer1 : linebuffer2));
  else
    PLIB_DMA_ChannelXSourceStartAddressSet(DMA_ID_0, DMA_CHANNEL_0, (uint32_t) (line & 2 ? linebuffer1 : linebuffer2));
      
  // set DMA channel 0 to start transfer when triggered (by SPI transmit interrupt)
  // so the next 32-bit chunk of data will be transferred until the whole line has been sent
  PLIB_DMA_ChannelXTriggerEnable(DMA_ID_0, DMA_CHANNEL_0, DMA_CHANNEL_TRIGGER_TRANSFER_START);
  
  // set DMA channel 0 to trigger IntHandlerDMAChannel0 after the whole line has been sent
  PLIB_DMA_ChannelXINTSourceFlagClear(DMA_ID_0, DMA_CHANNEL_0, DMA_INT_BLOCK_TRANSFER_COMPLETE);
  
  // render NEXT line into the line buffer that is not currently being sent
  if( g_composite )
    { 
      if( line<DISPLAY_LINES ) 
        {  
          // for composite we have time to render the full line
          render_half_line(line, true,  line & 1 ? linebuffer2 : linebuffer1); 
          render_half_line(line, false, line & 1 ? linebuffer2 : linebuffer1); 
        }
    }
  else 
    {
      // for VGA we render 1/2 line per line shown (each line is shown twice)
      if( line<DISPLAY_LINES*2 ) 
        render_half_line(line>>1, line&1, line & 2 ? linebuffer2 : linebuffer1);
    }
}


void __ISR(_OUTPUT_COMPARE_2_VECTOR, ipl7AUTO) IntHandlerOC2(void)
{
  // This interrupt is scheduled to occur 60 CPU cycles before timer2 runs over, 
  // which signifies the end of the horizontal back porch.
  // We use this interrupt to time the beginning of picture data
  // and to set up the output compare registers producing the VSYNC pulse

  // Keep the CPU in a defined state until after timer2 runs over, which starts
  // the SPI/DMA transfer. Without this, the SPI/DMA transfer may get delayed
  // by a few cycles due to the current CPU activity, causing a "wobbly" picture.
  asm volatile("\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n");
  asm volatile("\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n");
  asm volatile("\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n");
  
  if( g_composite )
  {
    // increment line counter and roll over when we reach the bottom of the screen
    if( ++g_current_line==g_num_lines ) g_current_line=0;

    if( g_current_line==g_vbp_length-1 )
      {
        // We are one line before the vertically visible region.
        // Render the first visible line so it's ready when needed.
        render_half_line(0, true,  linebuffer1);
        render_half_line(0, false, linebuffer1);
      }
    else if( g_current_line==g_vbp_length )
      {
        // We at the start of the vertically visible region.
        // Set up DMA to start sending the first line at the next timer2 interrupt
        schedule_dma_line(g_current_line-(g_vbp_length-1));
      }
    else if( g_current_line==g_num_lines-g_vsync_length )
      {
        // We are at the start of the sync signal (i.e. the end of the vertical 
        // front porch). Set up OC3 (output compare 3) to produce a long (vertical) sync
        PLIB_OC_Buffer16BitSet(OC_ID_3, g_hsync_start-g_hsync_length);
      }
    else if( g_current_line==0 )
      {
        // We are at the end of the sync signal (i.e. the beginning
        // of the vertical back porch). Set up OC3 (output compare 3) 
        // to produce short (horizontal) syncs
        PLIB_OC_Buffer16BitSet(OC_ID_3, g_hsync_start+g_hsync_length);
      }
    }
  else
    {
    // increment line counter and roll over when we reach the bottom of the screen
    if( ++g_current_line==VGA_NUM_LINES ) g_current_line=0;

    if( g_current_line==VGA_VBP_LENGTH-2 )
      {
        // We are two lines before the vertically visible region.
        // Render the first half of the first visible line.
        render_half_line(0, true, linebuffer1);
      }
    else if( g_current_line==VGA_VBP_LENGTH-1 )
      {
        // We are one line before the vertically visible region.
        // Render the second half of the first visible line.
        render_half_line(0, false, linebuffer1);
      }
    else if( g_current_line==VGA_VBP_LENGTH )
      {
        // We at the start of the vertically visible region.
        // Set up DMA to start sending the first line at the next timer2 interrupt
        schedule_dma_line(g_current_line-(VGA_VBP_LENGTH-2));
      }
    else if( g_current_line==VGA_NUM_LINES-VGA_VSYNC_LENGTH )
      {
        // We are at the start of the sync signal (i.e. the end of the vertical 
        // front porch). Set up OC4 (output compare 4) to set 
        // the vertical sync signal low at the next timer2 interrupt
        PLIB_OC_ModeSelect(OC_ID_4, OC_SET_LOW_SINGLE_PULSE_MODE);
        PLIB_OC_Enable(OC_ID_4);
      }
    else if( g_current_line==0 )
      {
        // We are at the end of the sync signal (i.e. the beginning
        // of the vertical back porch). Set up OC4 (output compare 4) 
        // to set the vertical sync signal high at the next timer2 interrupt
        PLIB_OC_ModeSelect(OC_ID_4, OC_SET_HIGH_SINGLE_PULSE_MODE);
        PLIB_OC_Enable(OC_ID_4);
      }
    }
  
  if( g_current_line==0 )
  {
      static int framecounter = 0;

      // set vertical blanking and scrolling for this frame
      g_blank_before_row = (vdm1_ctrl / 16);
      g_scroll_rows      = (vdm1_ctrl & 15);

      // DIP switches 1+2: full-screen blanking/inversion
      switch( vdm1_dip & 0x03 )
        {
           case 0x00: { g_blank_all = true;  g_invert_all = false; break; }
           case 0x01: { g_blank_all = false; g_invert_all = true;  break; }
           case 0x02: { g_blank_all = false; g_invert_all = false; break; }
           case 0x03: { g_blank_all = true;  g_invert_all = true;  break; }
        }

      // DIP switches 3+4: cursor handling
      switch( vdm1_dip & 0x0C )
        {
           case 0x00: { charset = charset_nocursor; break; } 
           case 0x04: { charset = charset_cursor; break; } 
           case 0x08:
           case 0xC0: 
           { 
               framecounter = (framecounter + 1) % 30;
               charset = framecounter<15 ? charset_cursor : charset_nocursor;
               break;
           } 
        }
  
      // DIP switches 5+6: CR/VT blanking
      switch( vdm1_dip & 0x30 )
        {
           case 0x00: render_half_line = render_half_line_no_VTCR_blanking;   break;
           case 0x10: render_half_line = render_half_line_with_VTCR_blanking; break;
           case 0x20: render_half_line = render_half_line_with_VTCR_blanking; break;
           case 0x30: render_half_line = render_half_line_no_VTCR_blanking;   break;
        }
    }

  // allow next interrupt
  PLIB_INT_SourceFlagClear(INT_ID_0,INT_SOURCE_OUTPUT_COMPARE_2);
}


void __ISR(_DMA_0_VECTOR, ipl6AUTO) IntHandlerDMAChannel0(void)
{
  // This interrupt happens when the DMA transfer for one scan line
  // has finished. We use it to set up the transfer for the next line
  // (unless we have reached the end of the visible region)
    if( g_composite )
    {
      if( g_current_line<g_vbp_length+DISPLAY_LINES )
      {
        // schedule next scan line (note that g_current_line has already been 
        // incremented in IntHandlerOC2 at the beginning of this scan line)
        schedule_dma_line(g_current_line-(g_vbp_length-1));
      }
    }
    else
    {
      if( g_current_line<VGA_VBP_LENGTH+DISPLAY_LINES*2 )
      {
        // schedule next scan line (note that g_current_line has already been 
        // incremented in IntHandlerOC2 at the beginning of this scan line)
        schedule_dma_line(g_current_line-(VGA_VBP_LENGTH-2));
      }
    }

  // allow next interrupt
  PLIB_INT_SourceFlagClear(INT_ID_0, INT_SOURCE_DMA_0);
}


static void vdm1_set_timing()
{
  // Check whether VGA monitor is connected (VGA output pin gets pulled low)
  PLIB_PORTS_PinDirectionInputSet(PORTS_ID_0, PORT_CHANNEL_A, VIDEO_VGA_PIN);
  PLIB_PORTS_ChangeNoticePullUpPerPortEnable(PORTS_ID_0, PORT_CHANNEL_A, VIDEO_VGA_PIN);
  delayMicros(200);
  g_composite = PLIB_PORTS_PinGet(PORTS_ID_0, PORT_CHANNEL_A, VIDEO_VGA_PIN); 
  PLIB_PORTS_ChangeNoticePullUpPerPortDisable(PORTS_ID_0, PORT_CHANNEL_A, VIDEO_VGA_PIN);
   
  if( g_composite )
  {
    bool ntsc;
    
    // composite output => check PAL/NTSC jumper
    PLIB_PORTS_PinDirectionInputSet(PORTS_ID_0, PORT_CHANNEL_B, PAL_jumper_PIN);
    PLIB_PORTS_ChangeNoticePullUpPerPortEnable(PORTS_ID_0, PORT_CHANNEL_B, PAL_jumper_PIN);
    delayMicros(200);
    ntsc = PLIB_PORTS_PinGet(PORTS_ID_0, PORT_CHANNEL_B, PAL_jumper_PIN); 
    PLIB_PORTS_ChangeNoticePullUpPerPortDisable(PORTS_ID_0, PORT_CHANNEL_B, PAL_jumper_PIN);
    if( ntsc )
    {
      g_num_pixels   = NTSC_NUM_PIXELS;
      g_hfp_length   = NTSC_HFP_LENGTH;
      g_hbp_length   = NTSC_HBP_LENGTH;
      g_hsync_length = NTSC_HSYNC_LENGTH;
      g_num_lines    = NTSC_NUM_LINES;
      g_vfp_length   = NTSC_VFP_LENGTH;
      g_vbp_length   = NTSC_VBP_LENGTH;
      g_vsync_length = NTSC_VSYNC_LENGTH;
    }
    else
    {
      g_num_pixels   = PAL_NUM_PIXELS;
      g_hfp_length   = PAL_HFP_LENGTH;
      g_hbp_length   = PAL_HBP_LENGTH;
      g_hsync_length = PAL_HSYNC_LENGTH;
      g_num_lines    = PAL_NUM_LINES;
      g_vfp_length   = PAL_VFP_LENGTH;
      g_vbp_length   = PAL_VBP_LENGTH;
      g_vsync_length = PAL_VSYNC_LENGTH;
    }

    // start HSYNC 37 pixels late to account for the 32 "0" bits
    // sent by DMA1 before DMA0 starts, plus some overhead (determined experimentally)
    g_hsync_start  = g_num_pixels-g_hbp_length-g_hsync_length+37;
  }
  else
  {
    g_num_pixels   = VGA_NUM_PIXELS;
    g_hfp_length   = VGA_HFP_LENGTH;
    g_hbp_length   = VGA_HBP_LENGTH;
    g_hsync_length = VGA_HSYNC_LENGTH;
    g_num_lines    = VGA_NUM_LINES;
    g_vfp_length   = VGA_VFP_LENGTH;
    g_vbp_length   = VGA_VBP_LENGTH;
    g_vsync_length = VGA_VSYNC_LENGTH;
    
    // start HSYNC 41 pixels late to account for the 32 "0" bits
    // sent by DMA1 before DMA0 starts, plus some overhead (determined experimentally)
    g_hsync_start  = g_num_pixels-g_hbp_length-g_hsync_length+41;
  }
}



void vdm1_init()
{    
  // determine VGA/NTSC/PAL output and set timing constants
  vdm1_set_timing();
    
  // prioritize DMA for bus access
  PLIB_BMX_ArbitrationModeSet(BMX_ID_0, PLIB_BMX_ARB_MODE_DMA);
  
  // set up timer 2 (at 24MHz for VGA, 12MHz for Composite)
  PLIB_TMR_ClockSourceSelect(TMR_ID_2, TMR_CLOCK_SOURCE_PERIPHERAL_CLOCK );
  PLIB_TMR_PrescaleSelect(TMR_ID_2, g_composite ? TMR_PRESCALE_VALUE_4 : TMR_PRESCALE_VALUE_2);
  PLIB_TMR_Mode16BitEnable(TMR_ID_2);
  PLIB_TMR_Counter16BitClear(TMR_ID_2);
  PLIB_TMR_Period16BitSet(TMR_ID_2, g_num_pixels);

  // set up output compare for HSYNC signal
  PLIB_OC_ModeSelect(OC_ID_3, OC_DUAL_COMPARE_CONTINUOUS_PULSE_MODE);
  PLIB_OC_BufferSizeSelect(OC_ID_3, OC_BUFFER_SIZE_16BIT);
  PLIB_OC_TimerSelect(OC_ID_3, OC_TIMER_16BIT_TMR2);
  PLIB_OC_Buffer16BitSet(OC_ID_3, g_hsync_start+g_hsync_length);  // turn on at timer value
  PLIB_OC_PulseWidth16BitSet(OC_ID_3, g_hsync_start); // turn off at timer value
  PLIB_OC_Enable(OC_ID_3);
  
  // set up output compare + interrupt for start of next scan line
  // (see comment in ISR function IntHandlerOC2)
  PLIB_INT_VectorPrioritySet(INT_ID_0, INT_VECTOR_OC2, INT_PRIORITY_LEVEL7);
  PLIB_INT_VectorSubPrioritySet(INT_ID_0, INT_VECTOR_OC2, INT_SUBPRIORITY_LEVEL0);
  PLIB_INT_SourceFlagClear(INT_ID_0, INT_SOURCE_OUTPUT_COMPARE_2);
  PLIB_INT_SourceEnable(INT_ID_0, INT_SOURCE_OUTPUT_COMPARE_2);
  PLIB_OC_ModeSelect(OC_ID_2, OC_TOGGLE_CONTINUOUS_PULSE_MODE);
  PLIB_OC_BufferSizeSelect(OC_ID_2, OC_BUFFER_SIZE_16BIT);
  PLIB_OC_TimerSelect(OC_ID_2, OC_TIMER_16BIT_TMR2);
  PLIB_OC_Buffer16BitSet(OC_ID_2, g_composite ? g_num_pixels-15 : g_num_pixels-30);
  PLIB_OC_Enable(OC_ID_2);
    
  // set up output compare for VSYNC signal
  PLIB_OC_BufferSizeSelect(OC_ID_4, OC_BUFFER_SIZE_16BIT);
  PLIB_OC_TimerSelect(OC_ID_4, OC_TIMER_16BIT_TMR2);
  PLIB_OC_Buffer16BitSet(OC_ID_4, 0);

  // set up SPI (24MHz clock, 32-bit word size, input pin disabled)
  PLIB_SPI_Enable(SPI_ID_2);
  PLIB_SPI_MasterEnable(SPI_ID_2);
  PLIB_SPI_PinEnable(SPI_ID_2, SPI_PIN_DATA_OUT);
  PLIB_SPI_PinDisable(SPI_ID_2, SPI_PIN_DATA_IN);
  PLIB_SPI_CommunicationWidthSelect(SPI_ID_2, SPI_COMMUNICATION_WIDTH_32BITS);
  PLIB_SPI_BaudRateClockSelect(SPI_ID_2, SPI_BAUD_RATE_PBCLK_CLOCK);
  PLIB_SPI_BaudRateSet(SPI_ID_2, SYS_CLK_PeripheralFrequencyGet(CLK_BUS_PERIPHERAL_1), g_composite ? 12000000 : 24000000);
  PLIB_PORTS_RemapOutput(PORTS_ID_0, OUTPUT_FUNC_SDO2, g_composite ? OUTPUT_PIN_RPB2 : OUTPUT_PIN_RPA4);
  
  // set up DMA channel 1 (gets triggered by Timer2, sends one 32-bit "0" word to SPI2)
  PLIB_DMA_ChannelXSourceStartAddressSet(DMA_ID_0, DMA_CHANNEL_1, (uint32_t) &zeroWord);
  PLIB_DMA_ChannelXSourceSizeSet(DMA_ID_0, DMA_CHANNEL_1, 4);
  PLIB_DMA_ChannelXDestinationStartAddressSet(DMA_ID_0, DMA_CHANNEL_1, (uint32_t) &SPI2BUF);
  PLIB_DMA_ChannelXDestinationSizeSet(DMA_ID_0, DMA_CHANNEL_1, 4);
  PLIB_DMA_ChannelXCellSizeSet(DMA_ID_0, DMA_CHANNEL_1, 4);
  PLIB_DMA_ChannelXPrioritySelect(DMA_ID_0, DMA_CHANNEL_1, DMA_CHANNEL_PRIORITY_3);
  PLIB_DMA_ChannelXStartIRQSet(DMA_ID_0, DMA_CHANNEL_1, DMA_TRIGGER_TIMER_2);
  
  // set up DMA channel 0 (sends 32-bit chunks of line data to SPI2)
  // - chained to channel 1, i.e. will start after channel 1 finishes
  // - next 32-bit chunk triggered by SPI transmit finished interrupt
  // - triggers IntHandlerDMAChannel0 when transfer (i.e. scan line) complete
  PLIB_DMA_ChannelXSourceSizeSet(DMA_ID_0, DMA_CHANNEL_0, DISPLAY_PIXELS/8);
  PLIB_DMA_ChannelXDestinationStartAddressSet(DMA_ID_0, DMA_CHANNEL_0, (uint32_t) &SPI2BUF);
  PLIB_DMA_ChannelXDestinationSizeSet(DMA_ID_0, DMA_CHANNEL_0, 4);
  PLIB_DMA_ChannelXCellSizeSet(DMA_ID_0, DMA_CHANNEL_0, 4);
  PLIB_DMA_ChannelXPrioritySelect(DMA_ID_0, DMA_CHANNEL_0, DMA_CHANNEL_PRIORITY_3);
  PLIB_DMA_ChannelXStartIRQSet(DMA_ID_0, DMA_CHANNEL_0, DMA_TRIGGER_SPI_2_TRANSMIT);
  PLIB_DMA_ChannelXChainEnable(DMA_ID_0, DMA_CHANNEL_0);
  PLIB_DMA_ChannelXChainToLower(DMA_ID_0, DMA_CHANNEL_0);
  PLIB_DMA_ChannelXTriggerEnable(DMA_ID_0, DMA_CHANNEL_0, DMA_CHANNEL_TRIGGER_TRANSFER_START);
  PLIB_DMA_ChannelXINTSourceEnable(DMA_ID_0, DMA_CHANNEL_0, DMA_INT_BLOCK_TRANSFER_COMPLETE);
  PLIB_DMA_Enable(DMA_ID_0);

  // Set up DMA completion interrupt (IntHandlerDMAChannel0)
  PLIB_INT_VectorPrioritySet(INT_ID_0, INT_VECTOR_DMA0, INT_PRIORITY_LEVEL6);
  PLIB_INT_VectorSubPrioritySet(INT_ID_0, INT_VECTOR_DMA0, INT_SUBPRIORITY_LEVEL0);
  PLIB_INT_SourceFlagClear(INT_ID_0, INT_SOURCE_DMA_0);
  PLIB_INT_SourceEnable(INT_ID_0, INT_SOURCE_DMA_0);

  // apply initial DIP switch and control register settings
  vdm1_set_dip(vdm1_dip);
  
  // start showing the picture  
  PLIB_TMR_Start(TMR_ID_2);
}
