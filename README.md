# VDM-1 Simulator

This is a [Processor Technology VDM-1](http://www.s100computers.com/Hardware%20Folder/Processor%20Technology/VDM-1/VDM-1.htm) 
simulator to be used in conjunction with my [Altair 8800 Simulator project](https://www.hackster.io/david-hansel/arduino-altair-8800-simulator-3594a6).

After finishing the [Dazzler display](https://www.hackster.io/david-hansel/dazzler-display-for-altair-simulator-3febc6)
for the Altair Simulator I circled back to the Processor Technology VDM-1 display. It occurred to me that this could
be done by just creating a new firmware for [Geoff Graham's ASCII Video Terminal](http://geoffg.net/terminal.html)
since Geoff's hardware contains everything needed for the VDM-1, including a keyboard connection. I also realized
that I could re-use a number of the lessons learned in the Dazzler display for this.

The VDM-1 provided a 16 line by 64 character display and was a popular card to be installed in an Altair
back in the day. The capability of changing the picture by directly accessing the video memory allowed
programs/games to make very fluid updates to the screen (compared to regular ASCII terminals). The popular 
Trek-80 version of Steve Dompier's Star Trek game showed some of the improvements that could be achieved using the VDM-1:

![Trek-80](/doc/images/trek80.gif)

Other examples include Steve Dompier's [Target](/doc/images/target.gif) and [Raiders](/doc/images/raiders.gif) (Space Invaders clone)

## Updating the Altair Simulator firmware

To prepare the Altair Simulator to communicate with either the software or hardware VDM-1 emulator
you need to prepare it by uploading the latest firmware:
1. Download the latest firmware from the Altair Simulator's [GIT repository](https://github.com/dhansel/Altair8800).
2. In file config.h of the firmware, change "#define USE_VDM1 0" to "#define USE_VDM1 1"
3. Upload the new firmware to the Altair Simulator (using the Arduino IDE)

## Software VDM-1 emulator

As with the Dazzler project, I started this one with a pure (Windows) software implementation to test out
whether the general idea works - especially the communication with the Altair Simulator. You can use the resulting
application to try out the emulation without any additional hardware:
1. Download the [Windows VDM-1 application](/Windows/VDM1.exe)
2. Start the VDM-1 application
3. On the Altair Simulator, enter the configuration menu, go into the VDM-1 sub-menu 
4. Change the "Map to interface" setting to "Native USB"
5. Change the "Map keyboard to" setting to "SIO"
4. Connect your computer to the Altair Simulator's **native** USB port

After step 4, the VDM-1 application's title bar should show "(connected)" - if so then you're all set.
You can proceed to the **Using the VDM-1** section below.

Note that the source code for the software emulator is supplied in the [Windows](/Windows) directory, 
including a Visual Studio project set up to compile it.

Of course running the VDM-1 display as a Windows application is not very satisfying.
You may as well run a whole software emulator (such as [Z80pack](https://www.autometer.de/unix4fun/z80pack/))
on the PC. Having a hardware solution would be very much preferable for use with the (hardware) Altair Simulator.

## Hardware VDM-1 simulator

If you don't already have one of [Geoff Graham's ASCII terminals](http://geoffg.net/terminal.html) I highly
recommend getting one. It is a very useful piece of hardware for anyone dabbling in retro-computing. It
transforms any VGA or Composite monitor and a PS/2 keyboard into an instant-on Video terminal.

### Updating the terminal firmware

Once you have such a terminal, transforming it into a VDM-1 simulator is trivial:
1. Download and extract the [Firmware Upgrage to V1.3](http://geoffg.net/Downloads/Terminal/Terminal_V1.3_UPGRADE.zip) from Geoff's website
2. Follow the instructions in the Instructions.pdf file within that archive, **BUT** instead of using the Terminal_V1.3_UPGRADE.hex upgrade file, use the [VDM1-bootload.hex](/PIC32/firmware/VDM1-bootload.hex) file. Note that you can always revert the firmware to its original by following Geoff's instructions and uploading the Terminal_V1.3_UPGRADE.hex file.
3. Connect either the VGA output or the Composite output of the terminal hardware to a corresponding monitor and supply power to the terminal. 

If you are using Composite output, please note:
* The VDM-1 emulator firmware uses the "Baud Rate" jumper (JP1) "A" to determine whether to output NTSC or PAL. If the jumper is open then the output is NTSC, if it is closed, output is PAL.
* The VDM-1 outputs a picture of 576 horizontal pixels. You need to use a monitor that has the proper bandwidth/horizontal resolution to resolve that many pixels. This should not be a problem for modern TVs but older TVs may show a washed-out picture.

Note that the source code project for the new firmware is available in the [PIC32/firmware](/PIC32/firmware) directory.
Use the free Microchip MPLAB X IDE to compile and upload to the PIC32.

### Initial hardware test

After following the above instructions you should see a picture similar to the following 
(the color may be different depending on the color jumper settings on the terminal):
![Splash Screen](/doc/images/splash.png)

If you also connect a keyboard you should be able to move the cursor around the screen and
write onto the screen. The function keys modify the VDM-1's control register and DIP switches:

* F1/F2: Decrease/increase the upper 4 bits of the control register
* F3/F4: Decrease/increase the lower 4 bits of the control register
* F5-F10: Toggle DIP switches 1-6

For the expected behavior of these switches, please refer to the [VDM-1 manual](/doc/vdm1.pdf).
Note that the initial screen recreates the test pattern from page II-24 of the manual.

### Connecting to the Altair Simulator

Once you have verified that the VDM-1 produces a proper picture you can connect it to the
Altair Simulator. There are two possible ways to connect:

#### Serial connection

Connect the VDM-1's serial connection to a serial port on the Altair Simulator. The VDM-1 needs 
fast communication with the Altair to avoid slowing down the simulation, therefore the baud rate 
on the VDM-1 is fixed to 750000 baud. This means that you need to use the pin 18/19 serial port.
If you have modified your Arduino Due to fix the Pin 0/1 serial bug, in which case you can
alternatively use pin 0/1 (programming port).

After you have established the physical connection, enter the configuration menu on the Altair
Simulator, go into the VDM-1 sub-menu and change the following:
* Set the "Map to interface" setting to the serial port that the VDM-1 is connected to
* Set the "Map keyboard to" setting to "SIO"

Next, in the configuration menu, set 750000 (8N1) as the baud rate for the serial port that the VDM-1 is 
connected to.

Now proceed to the **Using the VDM-1** section below.

#### USB connection

If your pin 18/19 serial port is already used by a bluetooth module (or if you don't want
to occupy a prescious serial port for the VDM-1) then you can connect the VDM-1 to the
Arduino's **native** USB port. To make USB communication with the Altair Simulator
possible, the USB connector on the VDM-1 behaves as a USB **host**. To physically connect 
the VDM-1 you will need an adapter to convert the USB-B connector on the VDM-1 to USB-A
(such as [this](https://www.computercablestore.com/usb-adapter-usb-a-female-to-usb-b-male) 
but there are many [other](https://www.ebay.com/itm/New-USB-2-0-Type-A-Female-to-USB-B-Male-Adapter-Converter/291644209870?hash=item43e7597ace) [sellers](https://www.ebay.com/itm/2PCS-New-USB-2-0-Type-A-Female-to-USB-B-Male-Adapter-Converter-US-SHIPPING-M455/401544944647?hash=item5d7df19c07)).

Once the physical connection is established, enter the configuration menu on the Altair
Simulator, go into the VDM-1 sub-menu and and change the following:
* Set the "Map to interface" setting to "USB Native Port"
* Set the "Map keyboard to" setting to "SIO"

## Using the VDM-1

As an initial test for the communication between Altair and VDM-1 you can start the
CUTER for VDM-1 monitor software that is included in the Altair Simulator:

* Make sure the VDM-1 is connected and the Altair Simulator is configured as described above
* Set the SW7-0 switches to 00010000 (if you also have the Dazzler emulation enabled in the Simulator software then use 00010001).
* Push AUX1 down
* You should now see an empty screen with a ">" prompt at the top. For the software emulator, typing into the VDM-1 window should show the typed characters. Similarly, for the hardware emulator, typing on a keyboard connected to the VDM-1 should should show the typed characters.

If that works, see [here](/programs) for information on how to load and run some VDM-1 software on the Altair.

The Altair software included in this repository was taken from **Udo Munk**'s fantastic collection of Altair
(and other) software at [https://www.autometer.de/unix4fun/z80pack/ftp/altair/proctec-tapes](https://www.autometer.de/unix4fun/z80pack/ftp/altair/proctec-tapes).
