# VDM-1 Emulator

This is a [Processor Technology VDM-1](http://www.s100computers.com/Hardware%20Folder/Processor%20Technology/VDM-1/VDM-1.htm) 
emulator to be used in conjunction with my [Altair 8800 Simulator project](https://www.hackster.io/david-hansel/arduino-altair-8800-simulator-3594a6).

After finishing the [Dazzler display](https://www.hackster.io/david-hansel/dazzler-display-for-altair-simulator-3febc6)
for the Altair Simulator I circled back to the Processor Technology VDM-1 display. It occurred to me that this could
be done by just creating a new firmware for [Geoff Graham's ASCII Video Terminal](http://geoffg.net/terminal.html)
since Geoff's hardware contains everything needed for the VDM-1, including a keyboard connection.

The VDM-1 provides a 16 lines by 64 character display and was a popular card to be installed in an Altair
back in the day. The capability of changing the picture by directly accessing the video memory allowed
programs/games to make very fluid updates to the screen (compared to regular ASCII terminals). The popular 
Trek-80 version of Steve Dompier's Star Trek game showed some of the improvements that could be achieved using the VDM-1:

![Trek-80](/doc/images/trek80.gif)

## Software emulator (Windows)

As with the Dazzler project, I started this one with a pure (Windows) software implementation to test out
whether the general idea works - especially the communication with the Altair Simulator. You can use the resulting
application to try out the emulation without any additional hardware:
1. Update the Altair Simulator software to the latest version
2. Download the [VDM-1 application](/Windows/VDM1.exe)
3. Start the VDM-1 application
4. Connect your computer to the Altair Simulator's **native** USB port
After step 4, the VDM-1 application's title bar should show "(connected)" - you're all set.
See [here](/programs/README.TXT) for information on how to load and run some VDM-1 software on the Altair.

## Hardware emulator (firmware for Video Terminal)

More documentation for the VDM-1 emulator to follow.
