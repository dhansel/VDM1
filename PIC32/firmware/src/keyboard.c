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


#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "system_config.h"
#include "system_definitions.h"
#include "peripheral/int/plib_int.h"

#include "keyboard.h"


// keyboard receiver state
#define KBD_START  0
#define KBD_BIT0   1
#define KBD_BIT7   8
#define KBD_PARITY 9
#define KBD_STOP   10
#define KBD_ERROR  255
static int kbd_state = KBD_START;


// keyboard states
static bool shiftL = false, shiftR = false, altL = false, altR = false, ctrlL = false, ctrlR = false;
static bool breakcode = false, extchar = false;
static bool capsLK = false, numLK = false, scrlLK = false;
static int altcode = 0;


// keycodes that are only used internally
#define K_SHFTL  0xFE00
#define K_SHFTR  0xFE01
#define K_CTRLL  0xFE02
#define K_CTRLR  0xFE03
#define K_ALTL   0xFE04
#define K_ALTR   0xFE05
#define K_CPSLK  0xFE06
#define K_NUMLK  0xFE07
#define K_SCRLK  0xFE08

#define K_KP0    0xFF00
#define K_KP1    0xFF01
#define K_KP2    0xFF02
#define K_KP3    0xFF03
#define K_KP4    0xFF04
#define K_KP5    0xFF05
#define K_KP6    0xFF06
#define K_KP7    0xFF07
#define K_KP8    0xFF08
#define K_KP9    0xFF09
#define K_KPD    0xFF0A


const uint16_t scancodes[136] = 
{ K_NONE,  K_F9,    K_NONE,  K_F5,    K_F3,    K_F1,    K_F2,    K_F12,   // 0x00-0x07
  K_NONE,  K_F10,   K_F8,    K_F6,    K_F4,    0x09,    '`',     K_NONE,  // 0x08-0x0F
  K_NONE,  K_ALTL,  K_SHFTL, K_NONE,  K_CTRLL, 'q',     '1',     K_NONE,  // 0x10-0x17
  K_NONE,  K_NONE,  'z',     's',     'a',     'w',     '2',     K_NONE,  // 0x18-0x1F
  K_NONE,  'c',     'x',     'd',     'e',     '4',     '3',     K_NONE,  // 0x20-0x27
  K_NONE,  ' ',     'v',     'f',     't',     'r',     '5',     K_NONE,  // 0x28-0x2F
  K_NONE,  'n',     'b',     'h',     'g',     'y',     '6',     K_NONE,  // 0x30-0x37
  K_NONE,  K_NONE,  'm',     'j',     'u',     '7',     '8',     K_NONE,  // 0x38-0x3F
  K_NONE,  ',',     'k',     'i',     'o',     '0',     '9',     K_NONE,  // 0x40-0x47
  K_NONE,  '.',     '/',     'l',     ';',     'p',     '-',     K_NONE,  // 0x48-0x4F
  K_NONE,  K_NONE,  '\'',    K_NONE,  '[',     '=',     K_NONE,  K_NONE,  // 0x50-0x57
  K_CPSLK, K_SHFTR, 0x0d,    ']',     K_NONE,  '\\',    K_NONE,  K_NONE,  // 0x58-0x5F
  K_NONE,  K_NONE,  K_NONE,  K_NONE,  K_NONE,  K_NONE,  0x08,    K_NONE,  // 0x60-0x67
  K_NONE,  K_KP1,   K_NONE,  K_KP4,   K_KP7,   K_NONE,  K_NONE,  K_NONE,  // 0x68-0x6F
  K_KP0,   K_KPD,   K_KP2,   K_KP5,   K_KP6,   K_KP8,   0x1B,    K_NUMLK, // 0x70-0x77
  K_F11,   '+',     K_KP3,   '-',     '*',     K_KP9,   K_SCRLK, K_NONE,  // 0x78-0x7F
  K_NONE,  K_NONE,  K_NONE,  K_F7,    K_NONE,  K_NONE,  K_NONE,  K_NONE}; // 0x80-0x87

const uint16_t scancodes_shift[136] = 
{ K_NONE,  K_F9,    K_NONE,  K_F5,    K_F3,    K_F1,    K_F2,    K_F12,   // 0x00-0x07
  K_NONE,  K_F10,   K_F8,    K_F6,    K_F4,    0x09,    '~',     K_NONE,  // 0x08-0x0F
  K_NONE,  K_ALTL,  K_SHFTL, K_NONE,  K_CTRLL, 'Q',     '!',     K_NONE,  // 0x10-0x17
  K_NONE,  K_NONE,  'Z',     'S',     'A',     'W',     '@',     K_NONE,  // 0x18-0x1F
  K_NONE,  'C',     'X',     'D',     'E',     '$',     '#',     K_NONE,  // 0x20-0x27
  K_NONE,  ' ',     'V',     'F',     'T',     'R',     '%',     K_NONE,  // 0x28-0x2F
  K_NONE,  'N',     'B',     'H',     'G',     'Y',     '^',     K_NONE,  // 0x30-0x37
  K_NONE,  K_NONE,  'M',     'J',     'U',     '&',     '*',     K_NONE,  // 0x38-0x3F
  K_NONE,  '<',     'K',     'I',     'O',     ')',     '(',     K_NONE,  // 0x40-0x47
  K_NONE,  '>',     '?',     'L',     ':',     'P',     '_',     K_NONE,  // 0x48-0x4F
  K_NONE,  K_NONE,  '"',     K_NONE,  '{',     '+',     K_NONE,  K_NONE,  // 0x50-0x57
  K_CPSLK, K_SHFTR, 0x0d,    '}',     K_NONE,  '|',     K_NONE,  K_NONE,  // 0x58-0x5F
  K_NONE,  K_NONE,  K_NONE,  K_NONE,  K_NONE,  K_NONE,  0x08,    K_NONE,  // 0x60-0x67
  K_NONE,  K_KP1,   K_NONE,  K_KP4,   K_KP7,   K_NONE,  K_NONE,  K_NONE,  // 0x68-0x6F
  K_KP0,   K_KPD,   K_KP2,   K_KP5,   K_KP6,   K_KP8,   0x1B,    K_NUMLK, // 0x70-0x77
  K_F11,   '+',     K_KP3,   '-',     '*',     K_KP9,   K_SCRLK, K_NONE,  // 0x78-0x7F
  K_NONE,  K_NONE,  K_NONE,  K_F7,    K_NONE,  K_NONE,  K_NONE,  K_NONE}; // 0x80-0x87


volatile uint32_t keyboard_buffer_start = 0, keyboard_buffer_end = 0;
volatile uint8_t  keyboard_buffer[0x40];

#define keyboard_buffer_full()     (((keyboard_buffer_end+1)&0x0fff) == keyboard_buffer_start)
#define keyboard_buffer_empty()      (keyboard_buffer_start==keyboard_buffer_end)
#define keyboard_bufferr_available() ((keyboard_buffer_start-keyboard_buffer_end-1)&0x03f)


inline void keyboard_buffer_clear()
{
  keyboard_buffer_start = 0;
  keyboard_buffer_end = 0;
}


inline void keyboard_buffer_enqueue(uint8_t b)
{
  // There's really not much we can do if we receive a byte of data
  // when the buffer is full. Overwriting the beginning of the buffer
  // is about as bad as dropping the newly received byte. So we save
  // the time to check whether the buffer is full and just overwrite.
  keyboard_buffer[keyboard_buffer_end] = b;
  keyboard_buffer_end = (keyboard_buffer_end+1) & 0x3f;
}

inline uint8_t keyboard_buffer_dequeue()
{
  uint8_t res = 0;   
 
  if( !keyboard_buffer_empty() )
    {
      res = keyboard_buffer[keyboard_buffer_start];
      keyboard_buffer_start = (keyboard_buffer_start+1) & 0x3f;
    }
  
  return res;
}

void keyboard_reset()
{
  shiftL = false; shiftR = false; altL = false; altR = false; ctrlL = false; ctrlR = false;
  breakcode = false; extchar = false;
  capsLK = false; numLK = true; scrlLK = false;
  kbd_state = KBD_START;
  keyboard_buffer_clear();
}


bool keyboard_wait_clk_timeout(bool state, uint32_t timeout)
{
  // time out after 200us
  uint32_t start = micros();
  while( micros()-start<timeout )
   if( PLIB_PORTS_PinGet(PORTS_ID_0, PORT_CHANNEL_B, KB_CLOCK_PIN)==state )
     return true;
  
  return false;
}


bool keyboard_wait_clk(bool state)
{
  return keyboard_wait_clk_timeout(state, 200);
}


bool keyboard_send_bits(uint8_t data)
{
  int i;
  bool parity = true;

  for(i=0; i<8; i++)
  {
    // wait for CLK low
    if( !keyboard_wait_clk_timeout(false, 100000) ) return false;

    if( data&1 )
      { PLIB_PORTS_PinDirectionInputSet(PORTS_ID_0, PORT_CHANNEL_B, KB_DATA_PIN); parity = !parity; } // data high
    else
      PLIB_PORTS_PinDirectionOutputSet(PORTS_ID_0, PORT_CHANNEL_B, KB_DATA_PIN); // data low
  
    // shift data
    data >>= 1;
    
    // wait for CLK high
    if( !keyboard_wait_clk(true) ) return false;
  }

  // wait for CLK low
  if( !keyboard_wait_clk(false) ) return false;

  // send parity bit
  if( parity )
    PLIB_PORTS_PinDirectionInputSet(PORTS_ID_0, PORT_CHANNEL_B, KB_DATA_PIN); 
  else
    PLIB_PORTS_PinDirectionOutputSet(PORTS_ID_0, PORT_CHANNEL_B, KB_DATA_PIN);
          
  // wait for CLK high
  if( !keyboard_wait_clk(true) ) return false;
  
  // wait for CLK low
  if( !keyboard_wait_clk(false) ) return false;

  // send stop bit (1))
  PLIB_PORTS_PinDirectionInputSet(PORTS_ID_0, PORT_CHANNEL_B, KB_DATA_PIN); 
          
  // wait for CLK high
  if( !keyboard_wait_clk(true) ) return false;
  
  // release DATA
  PLIB_PORTS_PinDirectionInputSet(PORTS_ID_0, PORT_CHANNEL_B, KB_DATA_PIN);
  
  // wait for CLK low transition
  if( !keyboard_wait_clk(false) ) return false;
  
  // read ACK bit (must be 0)
  if( PLIB_PORTS_PinGet(PORTS_ID_0, PORT_CHANNEL_B, KB_DATA_PIN)!=0 )
    return false;

    // wait for CLK high transition
  if( !keyboard_wait_clk(true) ) return false;

  return true;  
}


bool keyboard_send_byte(uint8_t data)
{
  bool ok = false;

  // disable CLK interrupts
  PLIB_PORTS_ChannelChangeNoticeDisable(PORTS_ID_0, PORT_CHANNEL_B, KB_CLOCK_PIN_MASK);

  // pull CLK line low 
  PLIB_PORTS_PinDirectionOutputSet(PORTS_ID_0, PORT_CHANNEL_B, KB_CLOCK_PIN);
  delayMicros(110);
  
  // pull DATA line low and release CLK
  PLIB_PORTS_PinDirectionOutputSet(PORTS_ID_0, PORT_CHANNEL_B, KB_DATA_PIN);
  PLIB_PORTS_PinDirectionInputSet(PORTS_ID_0, PORT_CHANNEL_B, KB_CLOCK_PIN);
  // [wait ?us for CLK to settle]

  ok = keyboard_send_bits(data);
  
  // re-enable CLK interrupts
  PLIB_INT_SourceFlagClear(INT_ID_0, INT_SOURCE_CHANGE_NOTICE_B);         
  PLIB_PORTS_ChannelChangeNoticeEnable(PORTS_ID_0, PORT_CHANNEL_B, KB_CLOCK_PIN_MASK);
  
  return ok;
}


uint8_t keyboard_send_byte_wait_response(uint8_t b)
{
    uint8_t response = 0x00, retries = 10;
    while( retries-- > 0 )
    {
      keyboard_buffer_clear();
      if( keyboard_send_byte(b) ) 
      {
          uint32_t start = micros();
          while( keyboard_buffer_empty() && (micros()-start)<100000 );
          if( !keyboard_buffer_empty() )
          {
              response = keyboard_buffer_dequeue();
              if( response!=0xFE ) retries = 0;
          }
      }
      
      if( retries>0 ) delayMicros(10000);
    }
    
  return response;
}


bool keyboard_send_led_status()
{
   uint8_t st = (capsLK ? 4 : 0) + (numLK ? 2 : 0) + (scrlLK ? 1 : 0);
   if( keyboard_send_byte_wait_response(0xED)==0xFA )
     if( keyboard_send_byte_wait_response(st)==0xFA )
       return true;
   
   return false;
}

               
bool keyboard_set_repeat_rate(uint8_t rate)
{
   if( keyboard_send_byte_wait_response(0xF3)==0xFA )
     if( keyboard_send_byte_wait_response(rate)==0xFA )
       return true;
   
   return false;
}


void keyboard_bit_received(bool data)
{
  static uint8_t kbd_data = 0, kbd_parity = 0;
  static uint32_t kbd_prev_pulse = 0;
  
  // clear error state if CLK line was idle for longer than 1ms
  if( kbd_state == KBD_ERROR && (micros()-kbd_prev_pulse)>1000 )
    kbd_state = KBD_START;
  
  if( kbd_state == KBD_START )
  {
    // first bit is START bit and must be 0
    if( data )
      kbd_state = KBD_ERROR;
    else
      { kbd_state = KBD_BIT0; kbd_data = 0; kbd_parity = 1; }
  }
  else if( kbd_state >= KBD_BIT0 && kbd_state <= KBD_BIT7 )
  {
      kbd_data >>= 1;
      if( data ) { kbd_data |= 128; kbd_parity = !kbd_parity; }
      kbd_state++;
  }
  else if( kbd_state == KBD_PARITY )
  {
      if( kbd_parity == data )
        kbd_state = KBD_STOP;
      else
        kbd_state = KBD_ERROR;
  }
  else if( kbd_state == KBD_STOP )
  {
      if( kbd_data == 0xAA )
        keyboard_reset();
      else
        keyboard_buffer_enqueue(kbd_data);
      
      kbd_state = KBD_START;
  }
}


void __ISR(_CHANGE_NOTICE_VECTOR, ipl6AUTO) _IntHandlerChangeNotification(void)
{
  // read data on falling edge of clock pin
  if( !PLIB_PORTS_PinGet(PORTS_ID_0, PORT_CHANNEL_B, KB_CLOCK_PIN) )
    keyboard_bit_received(PLIB_PORTS_PinGet(PORTS_ID_0, PORT_CHANNEL_B, KB_DATA_PIN));
  PLIB_INT_SourceFlagClear(INT_ID_0, INT_SOURCE_CHANGE_NOTICE_B);
}


uint16_t keyboard_get_key()
{
    uint16_t key = K_NONE;
    
    while( !keyboard_buffer_empty() && key==K_NONE )
    {
        uint16_t c = K_NONE;
        uint8_t b = keyboard_buffer_dequeue();
        //print_byte(b); print_char('-');

        // translate key code to character code
        if( b == 0xE0 )
          extchar = true;
        else if( extchar && b!=0xF0 )
        {
            extchar = false;
            switch( b )
            {
                case 0x11: c = K_ALTR;  break;
                case 0x14: c = K_CTRLR; break;
                case 0x4A: c = '/';     break;
                case 0x5A: c = 0x0d;    break;
                case 0x69: c = K_END;   break;
                case 0x6B: c = K_CSRL;  break;
                case 0x6C: c = K_HOME;  break;
                case 0x70: c = K_INS;   break;
                case 0x71: c = K_DEL;   break;
                case 0x72: c = K_CSRD;  break;
                case 0x74: c = K_CSRR;  break;
                case 0x75: c = K_CSRU;  break;
                case 0x7A: c = K_PGDN;  break;
                case 0x7C: c = K_PRSC;  break;
                case 0x7D: c = K_PGUP;  break;
            }
        }
        else if( b < 136 )
        {
           if( shiftL || shiftR )
             c = scancodes_shift[b];
           else
             c = scancodes[b];

           // handle caps-lock           
           if( capsLK && c>96 && c<123 ) 
             c -= 32;
           else if( capsLK && c>64 && c<91 ) 
             c += 32;
           
           // handle num-lock
           switch( c ) 
           {
               case K_KP0: c = numLK ? '0' : K_INS;  break;
               case K_KP1: c = numLK ? '1' : K_END;  break;
               case K_KP2: c = numLK ? '2' : K_CSRD; break;
               case K_KP3: c = numLK ? '3' : K_PGDN; break;
               case K_KP4: c = numLK ? '4' : K_CSRL; break;
               case K_KP5: c = numLK ? '5' : K_NONE; break;
               case K_KP6: c = numLK ? '6' : K_CSRR; break;
               case K_KP7: c = numLK ? '7' : K_HOME; break;
               case K_KP8: c = numLK ? '8' : K_CSRU; break;
               case K_KP9: c = numLK ? '9' : K_PGUP; break;
               case K_KPD: c = numLK ? '.' : K_DEL;  break;
           }

           // handle CTRL
           if( ctrlL || ctrlR )
             {
               if( c>=64 && c<=95 )
                 c -= 64;
               else if( c>='a' && c<='z' )
                 c -= 'a'-1;
             }
        }
        
        // interpret character codes
        if( c==K_SHFTL )
          shiftL = !breakcode;
        else if( c==K_SHFTR )
          shiftR = !breakcode;
        if( c==K_CTRLL )
          ctrlL = !breakcode;
        else if( c==K_CTRLR )
          ctrlR = !breakcode;
        else if( c==K_ALTL )
        {
          if( breakcode ) 
            { key = altcode; altL = false; }
          else
            { altcode = 0; altL = true; }
        }
        else if( c==K_ALTR )
          altR = !breakcode;
        else if( !breakcode )
        {
           if( (c==K_CPSLK || c==K_NUMLK || c==K_SCRLK) )
             {
               if( c==K_CPSLK )
                 capsLK = !capsLK;
               else if( c==K_NUMLK )
                 numLK = !numLK;
               else if( c==K_SCRLK )
                 scrlLK = !scrlLK;

               keyboard_send_led_status();
             }
             else if( altL && c>='0' && c <='9' )
               altcode = altcode * 10 + c-'0';
             else
               key = c;
           }

        breakcode = (b==0xF0);
    }
    
    return key;
}


void keyboard_init()
{
  // reset keyboard states
  keyboard_reset();
  
  // Set up DATA/CLK pins (pins 16/17, B7/B8)
  // simulate open-collector by using pin direction:
  // - direction "output": outputs 0
  // - direction "input": high-z state, pull-up resistor makes 1
  PLIB_PORTS_PinDirectionInputSet(PORTS_ID_0, PORT_CHANNEL_B, KB_CLOCK_PIN);
  PLIB_PORTS_PinClear(PORTS_ID_0, PORT_CHANNEL_B, KB_CLOCK_PIN);
  PLIB_PORTS_PinDirectionInputSet(PORTS_ID_0, PORT_CHANNEL_B, KB_DATA_PIN);
  PLIB_PORTS_PinClear(PORTS_ID_0, PORT_CHANNEL_B, KB_DATA_PIN);
  
  // Set up change notification interrupt for  CLK pin (pin 17, B8)
  PLIB_PORTS_ChangeNoticePerPortTurnOn(PORTS_ID_0, PORT_CHANNEL_B);
  PLIB_PORTS_ChannelChangeNoticeEnable(PORTS_ID_0, PORT_CHANNEL_B, KB_CLOCK_PIN_MASK);
  PLIB_INT_VectorPrioritySet(INT_ID_0, INT_VECTOR_CN, INT_PRIORITY_LEVEL6);
  PLIB_INT_VectorSubPrioritySet(INT_ID_0, INT_VECTOR_CN, INT_SUBPRIORITY_LEVEL0);
  PLIB_INT_SourceFlagClear(INT_ID_0, INT_SOURCE_CHANGE_NOTICE_B);         
  PLIB_INT_SourceEnable(INT_ID_0, INT_SOURCE_CHANGE_NOTICE_B);  

  
  // set keyboard LEDs and repeat rate
  if( keyboard_send_led_status() )
    keyboard_set_repeat_rate(6);
}
