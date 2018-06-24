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

## Software emulator

As with the Dazzler project, I started this one with a pure (Windows) software implementation to test out
whether the general idea works - especially the communication with the Altair Simulator. You can use the resulting
application to try out the emulation without any additional hardware:
1. Update the Altair Simulator software to the latest version
2. Download the [VDM-1 application](/Windows/VDM1.exe)
3. Start the VDM-1 application
4. Connect your computer to the Altair Simulator's **native** USB port

After step 4, the VDM-1 application's title bar should show "(connected)" - if so then you're all set.

See [here](/programs) for information on how to load and run some VDM-1 software on the Altair.

Note that the source code for the software emulator is supplied in the "Windows" directory, 
including a Visual Studio project set up to compile it.

## Hardware emulator

Of course running the VDM-1 display as a Windows application is not very satisfying.
One could as well run a whole software emulator (such as [Z80pack](https://www.autometer.de/unix4fun/z80pack/))
on the PC. Having a hardware solution would be very much preferable.

If you don't already have one of [Geoff Graham's ASCII terminals](http://geoffg.net/terminal.html) I highly
recommend getting one. It is a very useful piece of hardware for anyone dabbling in retro-computing. It
transforms any VGA or Composite monitor and a PS/2 keyboard into an instant-on Video terminal.

Once you have such a terminal, transforming it into a VDM-1 emulator is trivial:
1. Download and extract the [Firmware Upgrage to V1.3](http://geoffg.net/Downloads/Terminal/Terminal_V1.3_UPGRADE.zip) from Geoff's website
2. Follow the instructions in the Instructions.pdf file within that archive, **BUT** instead of using the Terminal_V1.3_UPGRADE.hex upgrade file, use the [VDM1-bootload.hex](/PIC32/firmware/VDM1-bootload.hex) file. Note that you can always revert the firmware to its original by following Geoff's instructions and uploading the Terminal_V1.3_UPGRADE.hex file.
3. Connect either the VGA output or the Composite output of the terminal hardware to a corresponding monitor and supply power to the terminal. 

If you are using Composite output, please note:
* The VDM-1 emulator firmware uses the "Baud Rate" jumper (JP1) "A" to determine whether to output NTSC or PAL. If the jumper is open then the output is NTSC, if it is closed, output is PAL.
* The VDM-1 outputs a picture of 576 horizontal pixels. You need to use a monitor that has the proper bandwidth/horizontal resolution to resolve that many pixels. This should not be a problem for modern TVs but older TVs may show a washed-out picture.

After following the above instructions you should see a picture similar to the following 
(the color may be different depending on the color jumper settings on the terminal)
![Splash Screen](/doc/images/splash.png)

If you also connect a keyboard you should be able to move the cursor around the screen and
write onto the screen. The function keys modify the VDM-1's control register and DIP switches:

* F1/F2: Decrease/increase the lower 4 bits of the control register
* F3/F4: Decrease/increase the upper 4 bits of the control register
* F5-F10: Toggle DIP switches 1-6

For the expected behavior of these switches, please refer to the [VDM-1 manual](/doc/vdm1.pdf).
Note that the initial screen recreates the test pattern from page II-24 of the manual.

