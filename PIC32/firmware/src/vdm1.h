/* 
 * File:   video.h
 * Author: hansel
 *
 * Created on June 4, 2018, 10:15 PM
 */

#ifndef VIDEO_H
#define	VIDEO_H

#ifdef	__cplusplus
extern "C" {
#endif


// VDM1 video memory, can be read and written:
// contains the characters visible on screen
extern uint8_t vdm1_memory[16*64];
    

// VDM1 control register, can be read and written:
// 4 upper bits define the first line shown, all lines above this are blanked
// 4 lower bits define the top row on the screen. e.g. if 3 then the order
// of rows displayed on the screen is 3-15, 0, 1, 2
extern uint8_t vdm1_ctrl;


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
void vdm1_set_dip(uint8_t v);
uint8_t vdm1_get_dip();


// initialize the video display
void vdm1_init();


#ifdef	__cplusplus
}
#endif

#endif	/* VIDEO_H */

