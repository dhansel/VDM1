# VDM-1

[Processor Technology VDM-1](http://www.s100computers.com/Hardware%20Folder/Processor%20Technology/VDM-1/VDM-1.htm) 
emulator to be used with Altair 8800 Simulator project. For details about the simulator see:
https://www.hackster.io/david-hansel/arduino-altair-8800-simulator-3594a6

After finishing the [Dazzler display](https://www.hackster.io/david-hansel/dazzler-display-for-altair-simulator-3febc6)
for the Altair Simulator I circled back to the Processor Technology VDM-1 display. It occurred to me that this could
be done by just creating a new firmware for [Geoff Graham's ASCII Video Terminal](http://geoffg.net/terminal.html)
since Geoff's hardware contains everything needed for the VDM-1, including a keyboard connection.

The VDM-1 provides a 16 lines by 64 character display and was a popular card to be installed in an Altair
back in the day. The capability of changing the picture by directly accessing the video memory allowed
programs/games to make very fluid updates to the screen (compared to terminals). The Trek-80 version of
Steve Dompier's Star Trek game showed the improvements that could be achieved using the VDM-1:

![Trek-80](/doc/trek80.gif)

## Software emulator (Windows)

## Hardware emulator (firmware for Video Terminal)

More documentation for the VDM-1 emulator to follow.
