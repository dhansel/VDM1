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


#include "app.h"
#include "vdm1.h"
#include "keyboard.h"
#include "peripheral/tmr/plib_tmr.h"
#include "peripheral/osc/plib_osc.h"
#include "peripheral/ports/plib_ports.h"
#include "peripheral/usart/plib_usart.h"
#include "peripheral/devcon/plib_devcon.h"


// The following Microchip USB host stack source files have been modified from their original:
//
// File: firmware\src\system_config\default\framework\usb\src\dynamic\usb_host_cdc.c
// line 790: changed USB_CDC_PROTOCOL_AT_V250 to USB_CDC_PROTOCOL_NO_CLASS_SPECIFIC
// (Arduino Due reports CDC protocol as NO_CLASS_SPECIFIC)
//
// File: firmware\src\system_config\default\framework\usb\src\dynamic\usb_host.c
// added line 5391: if( transferType==USB_TRANSFER_TYPE_BULK && maxPacketSize>64 ) maxPacketSize=64;
//(Arduino Due always sends the high speed descriptor when configuration is requested, should
// send full speed descriptor if the host is full speed. Will send full speed descriptor when OTHER 
// configuration is requested. maxPacketSize for full speed bulk transfer is 64 bytes, high speed is 512)
// 
// File: firmware\src\system_config\default\framework\driver\usb\usbfs\src\dynamic\drv_usbfs_host.c
// added starting in line 1713:
// if( ++pipe->nakCounter>=10 )
//   { pIRP->status = USB_HOST_IRP_STATUS_COMPLETED_SHORT; pipe->nakCounter = 0; endIRP = true; }
//  else
//  (to end a read request if no data is received after waiting for 10ms, necessary
//   so we can still send joystick data when the Arduino is not sending)



// vdm1 commands received from the Altair simulator
#define VDM_MEMBYTE   0x10
#define VDM_FULLFRAME 0x20
#define VDM_CTRL      0x30
#define VDM_DIP       0x40

// vdm1 commands sent to the Altair simulator
#define VDM_CONNECT   0x10
#define VDM_KEY       0x30

// receiver states
#define ST_IDLE      0
#define ST_MEMBYTE1  1
#define ST_MEMBYTE2  2
#define ST_CTRL      3
#define ST_DIP       4
#define ST_FULLFRAME 5


uint32_t micros()
{
  return PLIB_TMR_Counter32BitGet(TMR_ID_4)/3;
}


void delayMicros(uint32_t t)
{
  uint32_t start = micros();
  while( micros()-start < t );
}


void blink(bool activity)
{
   static bool on = false;
   static uint32_t prev = 0;
   
   // LED is OFF when signaling activity
   if( on && (micros()-prev)>25000 )
   {
       // turn LED on
       PLIB_PORTS_PinDirectionOutputSet(PORTS_ID_0, PORT_CHANNEL_B, LED_PIN);
       on = false;
       prev = micros();
   }       
   else if( !on && activity && (micros()-prev)>25000 ) 
    {
       // turn LED off
       PLIB_PORTS_PinDirectionInputSet(PORTS_ID_0, PORT_CHANNEL_B, LED_PIN);       
       on = true;
       prev = micros();
   }
}


// -----------------------------------------------------------------------------
// --------------  Higher-level VDM1 communication handler  --------------------
// -----------------------------------------------------------------------------


void vdm1_receive(uint8_t data)
{
  static int state = ST_IDLE, addr, cnt;

  blink(true);
  switch( state )
    {
    case ST_IDLE:
      {
        switch( data & 0xf0 )
          {
          case VDM_MEMBYTE: 
            state = ST_MEMBYTE1;
            addr  = (data & 0x07) * 256;
            break;

          case VDM_CTRL:
            state = ST_CTRL;
            break;

          case VDM_DIP:
            state = ST_DIP;
            break;

          case VDM_FULLFRAME:
            state = ST_FULLFRAME;
            addr  = 0;
            cnt   = 1024;
            break;
          }
        break;
      }
      
    case ST_MEMBYTE1:
      addr += data;
      state = ST_MEMBYTE2;
      break;

    case ST_MEMBYTE2:
      vdm1_memory[addr] = data;
      state = ST_IDLE;
      break;

    case ST_CTRL:
      vdm1_ctrl = data;
      state = ST_IDLE;
      break;

    case ST_DIP:
      vdm1_set_dip(data);
      state = ST_IDLE;
      break;

    case ST_FULLFRAME:
      vdm1_memory[addr++] = data;
      if( --cnt==0 ) state = ST_IDLE; 
      break;
    }
}


// -----------------------------------------------------------------------------
// ---------------------- Ring buffer for received data ------------------------
// -----------------------------------------------------------------------------


volatile uint32_t ringbuffer_start = 0, ringbuffer_end = 0;
volatile uint8_t  ringbuffer[0x1000];

#define ringbuffer_full()     (((ringbuffer_end+1)&0x0fff) == ringbuffer_start)
#define ringbuffer_empty()      (ringbuffer_start==ringbuffer_end)
#define ringbuffer_available() ((ringbuffer_start-ringbuffer_end-1)&0x0fff)


inline void ringbuffer_enqueue(uint8_t b)
{
  // There's really not much we can do if we receive a byte of data
  // when the ring buffer is full. Overwriting the beginning of the buffer
  // is about as bad as dropping the newly received byte. So we save
  // the time to check whether the buffer is full and just overwrite.
  ringbuffer[ringbuffer_end] = b;
  ringbuffer_end = (ringbuffer_end+1) & 0x0fff;
}

inline uint8_t ringbuffer_dequeue()
{
  if( !ringbuffer_empty() )
    {
      vdm1_receive(ringbuffer[ringbuffer_start]);
      ringbuffer_start = (ringbuffer_start+1) & 0x0fff;
    }
}


// -----------------------------------------------------------------------------
// ---------------------- Terminal routines (test mode) ------------------------
// -----------------------------------------------------------------------------


// initially the cursor is not visible, it only becomes visible
// after the first time a key has been pressed
static bool cursor_shown = false;
static int cursor_row = 0, cursor_col = 0;


static void toggle_dip(uint8_t n)
{
  uint8_t mask = 1<<(n-1), dip = vdm1_get_dip();
  vdm1_set_dip((dip & ~mask) | ((dip & mask) ^ mask));
}


static void set_char(int row, int col, uint8_t ch)
{
  vdm1_memory[row*64+col] = (cursor_shown && row==cursor_row && col==cursor_col) ? ch^0x80 : ch;
}


static uint8_t get_char(int row, int col)
{
  uint8_t ch = vdm1_memory[row*64+col];
  return (cursor_shown && row==cursor_row && col==cursor_col) ? ch^0x80 : ch;
}


static void move_cursor(int row, int col)
{
  if( cursor_shown ) vdm1_memory[cursor_row*64+cursor_col] ^= 0x80;
  cursor_row = row & 0x0f; 
  cursor_col = col & 0x3f;
  if( cursor_shown ) vdm1_memory[cursor_row*64+cursor_col] ^= 0x80;
}

static void show_cursor(bool show)
{
   if( show && !cursor_shown || !show && cursor_shown)
     vdm1_memory[cursor_row*64+cursor_col] ^= 0x80;
   cursor_shown = show;
}

static void advance_cursor(int i)
{
  int r = cursor_row, c = cursor_col + i;
  while( c>63 ) { c -= 64; r++; }
  while( c< 0 ) { c += 64; r--; }
  if( r<0  ) { r=0; c=0; }
  if( r>15 ) { r=15; c=63; }
  move_cursor(r, c);
}


static void print_dip()
{
  uint8_t i, dip = vdm1_get_dip();
  for(i=0; i<6; i++)
  {
    set_char(0, 29+i, dip & 1 ? '1' : '0');
    dip >>= 1;
  }
}

static void print_char(uint16_t c)
{
  switch( c )
  {
    case 8:  
    case K_DEL:  
    {
      int i;
      if( c==8 ) advance_cursor(-1); 
      for(i=cursor_col; i<64; i++) set_char(cursor_row, i, get_char(cursor_row, i+1));
      set_char(cursor_row, 63, ' ');
      break;
    }

    case 9:
      if( cursor_col < 63 ) advance_cursor(8-(cursor_col&7));
      break;
     
    case 10:
      move_cursor(cursor_row+1, cursor_col);
      break;
     
    case 13:
      move_cursor(cursor_row+1, 0);
      break;

    case K_INS:
    {
      int i;
      for(i=63; i>=cursor_col; i--) set_char(cursor_row, i, get_char(cursor_row, i-1));
      set_char(cursor_row, cursor_col, ' ');
      break;
    }

    case K_HOME:
    {
        int i;
        for(i=0; i<64; i++) if( get_char(cursor_row, i) != ' ' ) break;
        if( i==64 ) i=0;
        move_cursor(cursor_row, i==cursor_col ? 0 : i);
        break;
    }
      
    case K_END:
    {
        int i;
        for(i=63; i>=0; i--) if( get_char(cursor_row, i) != ' ' ) break;
        if( i<0 ) i=63;
        if( i<63 ) i++;
        move_cursor(cursor_row, i==cursor_col ? 63 : i);
        break;
    }

    case K_CSRU:
      move_cursor(cursor_row-1, cursor_col);
      break;
      
    case K_CSRD:
      move_cursor(cursor_row+1, cursor_col);
      break;
      
    case K_CSRL:
      advance_cursor(-1);
      break;
      
    case K_CSRR:
      advance_cursor(1);
      break;

    case K_PGUP:
      move_cursor(0, cursor_col);
      break;
      
    case K_PGDN:
      move_cursor(15, cursor_col);
      break;
      
    case K_PRSC:
      show_cursor(false);
      memset(vdm1_memory, ' ', 16*64);
      move_cursor(0, 0);
      show_cursor(true);
      break;
      
    case K_F1:
      vdm1_ctrl = (vdm1_ctrl-0x10) | (vdm1_ctrl & 0x0F);
      break;
    
    case K_F2:
      vdm1_ctrl = (vdm1_ctrl+0x10) | (vdm1_ctrl & 0x0F);
      break;
    
    case K_F3: 
      vdm1_ctrl = (vdm1_ctrl & 0xF0) | ((vdm1_ctrl-1) & 0x0F);
      break;
      
    case K_F4:
      vdm1_ctrl = (vdm1_ctrl & 0xF0) | ((vdm1_ctrl+1) & 0x0F);
      break;

    case K_F5:
    case K_F6:
    case K_F7:
    case K_F8:
    case K_F9:
    case K_F10:
      toggle_dip(c-K_F5+1);
      //print_dip();
      break;

    case K_F11:
      set_char(cursor_row, cursor_col, 13);
      break;

    case K_F12:
      set_char(cursor_row, cursor_col, 11);
      break;
      
    default:
      if( c<0x100 )
      {
        set_char(cursor_row, cursor_col, c);
        advance_cursor(1);
      }
  }
}

void print_byte(uint8_t data)
{
  const char hex[17] = "0123456789ABCDEF";
  print_char(hex[data/16]);
  print_char(hex[data&15]);
}

void print_string(char *s)
{
  while(*s) print_char(*s++);
}

// -----------------------------------------------------------------------------
// -------------------------------- USB handlers -------------------------------
// -----------------------------------------------------------------------------


static USB_HOST_CDC_OBJ    usbCdcObject     = NULL;
static USB_HOST_CDC_HANDLE usbCdcHostHandle = USB_HOST_CDC_HANDLE_INVALID;
static uint8_t usbInData[64], usb_keydata[2];
volatile bool usbBusy = false, usbSendConnect = false;


void usbScheduleTransfer()
{
  uint16_t key = K_NONE;

  if( usbBusy )
    {
      // a USB read or write request is currently in process so we can't
      // schedule a transfer right now
      return;
    }
  else if( usbSendConnect )
    {
      usb_keydata[0] = VDM_CONNECT;
      USB_HOST_CDC_Write(usbCdcHostHandle, NULL, usb_keydata, 1);
      usbBusy = true;
      usbSendConnect = false;
    }
  else if( (key=keyboard_get_key()) < 0x100 )
    {
      blink(true);
      usb_keydata[0] = VDM_KEY;
      usb_keydata[1] = key;
      USB_HOST_CDC_Write(usbCdcHostHandle, NULL, usb_keydata, 2);
      usbBusy = true;
    }
  else
    {
      // If there is space available in the ringbuffer then ask the client
      // to send more data. If there is no space then do not start another
      // request until we have processed some data and space is available.
      size_t avail = ringbuffer_available();
      if( avail>0 ) 
        {
          USB_HOST_CDC_Read(usbCdcHostHandle, NULL, usbInData, avail > 64 ? 64 : avail);
          usbBusy = true;
        }
    }
}


void USBHostCDCAttachEventListener(USB_HOST_CDC_OBJ cdcObj, uintptr_t context)
{
  // a client has been attached
  usbCdcObject = cdcObj;
  usbSendConnect = true;
}


USB_HOST_CDC_EVENT_RESPONSE USBHostCDCEventHandler(USB_HOST_CDC_HANDLE cdcHandle, USB_HOST_CDC_EVENT event, void * eventData, uintptr_t context)
{
  USB_HOST_CDC_EVENT_WRITE_COMPLETE_DATA * writeCompleteEventData;
  USB_HOST_CDC_EVENT_READ_COMPLETE_DATA * readCompleteEventData;
    
  switch(event)
    {
    case USB_HOST_CDC_EVENT_READ_COMPLETE:
      {
        readCompleteEventData = (USB_HOST_CDC_EVENT_READ_COMPLETE_DATA *)(eventData);
        if( readCompleteEventData->result == USB_HOST_CDC_RESULT_SUCCESS )
          {
            // received data from the client => put it in the ringbuffer so it can
            // be processed when we get to it
            size_t i;
            for(i=0; i<readCompleteEventData->length; i++)
              ringbuffer_enqueue(usbInData[i]);
          }

        // transfer is finished => schedule the next transfer
        usbBusy = false;
        usbScheduleTransfer();
        break;
      }
        
    case USB_HOST_CDC_EVENT_WRITE_COMPLETE:
      {   
        // transfer is finished => schedule the next transfer
        usbBusy = false;
        usbScheduleTransfer();
        break;
      }
            
    case USB_HOST_CDC_EVENT_DEVICE_DETACHED:
      {
        usbCdcObject = NULL;
        usbCdcHostHandle = USB_HOST_CDC_HANDLE_INVALID;
        break;
      }
    }
    
  return(USB_HOST_CDC_EVENT_RESPONE_NONE);
}


bool usbTasks()
{
  if( usbCdcHostHandle==USB_HOST_CDC_HANDLE_INVALID )
    {
      if( usbCdcObject!=NULL )
        {
          // a USB device was newly attached - try to open it
          usbCdcHostHandle = USB_HOST_CDC_Open(usbCdcObject);
          if(usbCdcHostHandle != USB_HOST_CDC_HANDLE_INVALID)
            {
              // succeeded opening the device => all further processing is in event handler
              USB_HOST_CDC_EventHandlerSet(usbCdcHostHandle, USBHostCDCEventHandler, (uintptr_t)0);
          
              // request data
              USB_HOST_CDC_Read(usbCdcHostHandle, NULL, usbInData, 64);
            }
        }
    }
  else
    {
      // schedule a new transfer if none is currently going
      // (can't allow USB interrupts while scheduling a new transfer)
      PLIB_INT_Disable(INT_ID_0);
      usbScheduleTransfer();
      PLIB_INT_Enable(INT_ID_0);
    }
  
  return usbCdcHostHandle!=USB_HOST_CDC_HANDLE_INVALID;
}


// -----------------------------------------------------------------------------
// ------------------------------ Serial handlers ------------------------------
// -----------------------------------------------------------------------------

bool serialConnected = false;

void __ISR(_UART_2_VECTOR, ipl3AUTO) _IntHandlerUSARTReceive(void)
{
  serialConnected = true;
  ringbuffer_enqueue(PLIB_USART_ReceiverByteReceive(USART_ID_2));
  PLIB_INT_SourceFlagClear(INT_ID_0, INT_SOURCE_USART_2_RECEIVE);
}


void serialTasks()
{
  uint16_t key;

  if( !serialConnected )
  {
      static uint32_t prevSend = 0;
      if( micros()-prevSend > 500000 )
      {
        PLIB_USART_TransmitterByteSend(USART_ID_2, VDM_CONNECT);          
        prevSend = micros();
      }
  }
  else if( (key=keyboard_get_key())<0x100 ) 
  {
    PLIB_USART_TransmitterByteSend(USART_ID_2, VDM_KEY);
    PLIB_USART_TransmitterByteSend(USART_ID_2, key);
    blink(true);
  }
}


// -----------------------------------------------------------------------------
// ------------------------------- initialization ------------------------------
// -----------------------------------------------------------------------------



void set_pll_multiplier(int mult)
{
  if( mult != PLIB_OSC_SysPLLMultiplierGet(OSC_ID_0) )
  {
    // disable interrupts and unlock the system configuration    
    PLIB_INT_Disable(INT_ID_0);
    PLIB_DEVCON_SystemUnlock(DEVCON_ID_0);
  
    // switch clock source to FRC (can't change PLL while in use)
    PLIB_OSC_SysClockSelect(OSC_ID_0, OSC_FRC);
    while(!PLIB_OSC_ClockSwitchingIsComplete(OSC_ID_0));

    // change the PLL multiplier
    PLIB_OSC_SysPLLMultiplierSelect(OSC_ID_0, mult);

    // switch clock source back to primary oscillator with PLL
    PLIB_OSC_SysClockSelect(OSC_ID_0, OSC_PRIMARY_WITH_PLL);
    while(!PLIB_OSC_ClockSwitchingIsComplete(OSC_ID_0));
  
    // re-lock the system configuration and enable interrupts
    PLIB_DEVCON_SystemLock(DEVCON_ID_0);
    PLIB_INT_Enable(INT_ID_0);
  }
}


void APP_Initialize ( void )
{
  int i;

  // Geoff Graham's terminal runs at 40MHz with a 20MHz (SPI) pixel clock
  // which is not fast enough to display 576 pixels in the 25.4 microseconds
  // we have according to the VGA (640x480) spec. We need to change the PLL
  // multiplier to up the frequency to 48MHz with a 24MHz (SPI) pixel clock.
  // Luckily the PIC32 architecture allows such changes at runtime.
  // This is really only necessary if the VDM1 firmware was uploaded via the 
  // bootloader - a proper programmer will also set the configuration bits
  // for our 24MHz clock frequency.
  set_pll_multiplier(24);
  
  // set up timer 4+5 (32-bit timer at 3MHz, overflows after ~1431 seconds)
  // used as general-purpose timer
  PLIB_TMR_ClockSourceSelect(TMR_ID_4, TMR_CLOCK_SOURCE_PERIPHERAL_CLOCK );
  PLIB_TMR_PrescaleSelect(TMR_ID_4, TMR_PRESCALE_VALUE_16);
  PLIB_TMR_Mode32BitEnable(TMR_ID_4);
  PLIB_TMR_Counter32BitClear(TMR_ID_4);
  PLIB_TMR_Start(TMR_ID_4);

  // set up USB
  USB_HOST_CDC_AttachEventHandlerSet(USBHostCDCAttachEventListener, (uintptr_t) 0);
  PLIB_USB_StopInIdleDisable(USB_ID_1);
  USB_HOST_BusEnable(0);
  
  // set up USART 2 on pins 4/5 (B0/B1) at 750000 baud, 8N1
  PLIB_PORTS_PinModePerPortSelect(PORTS_ID_0, PORT_CHANNEL_B, 0, PORTS_PIN_MODE_DIGITAL);
  PLIB_PORTS_PinModePerPortSelect(PORTS_ID_0, PORT_CHANNEL_B, 1, PORTS_PIN_MODE_DIGITAL);
  PLIB_PORTS_RemapOutput(PORTS_ID_0, OUTPUT_FUNC_U2TX, OUTPUT_PIN_RPB0);
  PLIB_PORTS_RemapInput(PORTS_ID_0, INPUT_FUNC_U2RX, INPUT_PIN_RPB1);
  PLIB_PORTS_ChangeNoticePullUpPerPortEnable(PORTS_ID_0, PORT_CHANNEL_B, 1);
  PLIB_USART_InitializeModeGeneral(USART_ID_2, false, false, false, false, false);
  PLIB_USART_LineControlModeSelect(USART_ID_2, USART_8N1);
  PLIB_USART_InitializeOperation(USART_ID_2, USART_RECEIVE_FIFO_ONE_CHAR, USART_TRANSMIT_FIFO_IDLE, USART_ENABLE_TX_RX_USED);
  PLIB_USART_BaudRateHighEnable(USART_ID_2);
  PLIB_USART_BaudRateHighSet(USART_ID_2, SYS_CLK_PeripheralFrequencyGet(CLK_BUS_PERIPHERAL_1), 750000);
  PLIB_USART_TransmitterEnable(USART_ID_2);
  PLIB_USART_ReceiverEnable(USART_ID_2);
  PLIB_USART_Enable(USART_ID_2);
  serialConnected = false;
  
  // set up USART 2 receive interrupt 
  PLIB_INT_VectorPrioritySet(INT_ID_0, INT_VECTOR_UART2, INT_PRIORITY_LEVEL3);
  PLIB_INT_VectorSubPrioritySet(INT_ID_0, INT_VECTOR_UART2, INT_SUBPRIORITY_LEVEL0);
  PLIB_INT_SourceFlagClear(INT_ID_0, INT_SOURCE_USART_2_RECEIVE);
  PLIB_INT_SourceEnable(INT_ID_0, INT_SOURCE_USART_2_RECEIVE);

  // set up activity LED
  PLIB_PORTS_PinDirectionOutputSet(PORTS_ID_0, PORT_CHANNEL_B, LED_PIN);
  PLIB_PORTS_PinClear(PORTS_ID_0, PORT_CHANNEL_B, LED_PIN);
  
  // initialize the video output
  vdm1_init();

  // initialize the screen
  for(i=0; i<16*64; i++) vdm1_memory[i] = ~i & 255;
  vdm1_memory[0x00] = vdm1_memory[0x74] = vdm1_memory[0xF2] = 32;
  move_cursor(6,12); print_string("                                        ");
  move_cursor(7,12); print_string("  Processor Technology VDM-1 Simulator  ");
  move_cursor(8,12); print_string("         (C) 2018 David Hansel          ");
  move_cursor(9,12); print_string("                                        ");
  move_cursor(0,0);

  // initialize the keyboard input
  keyboard_init();

}


void APP_Tasks ( void )
{
  blink(false);

  // process received data (most commands have 3 bytes so process 3 at once)
  ringbuffer_dequeue();
  ringbuffer_dequeue();
  ringbuffer_dequeue();

  // check for new USB connection and manage existing USB connection
  if( !usbTasks() ) 
  {
    // USB is not connected => communicate via serial
    serialTasks();

    // if serial isn't connected either then just display characters
    // from keyboard (test mode)
    if( !serialConnected ) 
    {
       uint16_t ch = keyboard_get_key();
       if(ch != K_NONE) { blink(true); show_cursor(true); print_char(ch); } 
    }    
  }
}
