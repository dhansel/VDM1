/* 
 * File:   keyboard.h
 * Author: hansel
 *
 * Created on June 4, 2018, 9:38 PM
 */

#ifndef KEYBOARD_H
#define	KEYBOARD_H

#include <stdint.h>


#ifdef	__cplusplus
extern "C" {
#endif
    

// special key codes beyond ASCII
#define K_F1     0x0101
#define K_F2     0x0102
#define K_F3     0x0103
#define K_F4     0x0104
#define K_F5     0x0105
#define K_F6     0x0106
#define K_F7     0x0107
#define K_F8     0x0108
#define K_F9     0x0109
#define K_F10    0x010A
#define K_F11    0x010B
#define K_F12    0x010C
    
#define K_HOME   0x0110
#define K_END    0x0111
#define K_CSRU   0x0112
#define K_CSRD   0x0113
#define K_CSRL   0x0114
#define K_CSRR   0x0115
#define K_PGUP   0x0116
#define K_PGDN   0x0117
#define K_INS    0x0080 // INSERT key behaves as MODE SELECT on SOLOS keyboards
#define K_DEL    0x007f // DELETE key sends DEL character
#define K_PRSC   0x011A
    
#define K_NONE   0xFFFF

    
uint16_t keyboard_get_key();
void keyboard_init();


#ifdef	__cplusplus
}
#endif

#endif	/* KEYBOARD_H */

