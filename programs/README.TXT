All programs here are supposed to run within the CUTER for VDM-1 monitor software.
The programs have been taken from Udo Munk's fantastic collection of Altair
(and other) software: https://www.autometer.de/unix4fun/z80pack/ftp/altair/proctec-tapes

Note that all these programs (and some more) are included in the DISK11.DSK disk image
supplied with the Altair Simulator firmware. See the "VDM-1 Support" section in the 
Altair Simulator documentation for more information on how to use this disk.

Each program is provided in four different formats
1) .ENT files: These are the "originals" from Udo's archive.
   They are text files in a format that can be pasted directly into
   the CUTER monitor. 
2) .HEX files: Intel HEX files that can be uploaded into the Simulator
   by setting SW0-7 to 0100000 and pressing AUX1 down.
3) .TAP files: These are direct captures from the serial port to which
   the ACR device is mapped, saving the program via "SAVE" command in CUTER.
4) .WAV files: Same as TAP but this time capturing the audio output of a 
   cassette interface (such as Mike Douglas' cassette interface for the 
   Altair Clone) connected to one of the Simulator's serial ports.

First step is to connect the VDM-1 display (either software or hardware) to the
Simulator. Configure the VDM-1 emulation in the Simulator for the correct port
that the display is connected to and set the "Map (k)eyboard to" setting to "SIO".

Next you need to get CUTER running on the Simulator. There are several possibilities:

1) When the Altair Simulator firmware is compiled with USE_VDM1 set to 1
   (as it needs to be to support the VDM-1 emulation) then the "AUX1 down" list 
   of programs will have a "CUTER for VDM1" entry. Set SW7-0 to the code listed
   for that program and press AUX1 down to run CUTER. 

2) Use the HEX upload mechanism in the Simulator: 
   * receive HEX file ('H' command or SW6 up and AUX1 down)
   * copy-and-paste vdmcuter.hex into terminal
   * set address switches to C000
   * EXAMINE
   * RUN

3) Load the vdmcuter.tap file through a serial port via a boot loader:
   The vdmcuter.tap contains CUTER wrapped into the same second-stage
   boot loader used by Altair 8k BASIC 4.0.
   * Connect the serial port that ACR is mapped to to your PC
   * Enter the configuration monitor, configure the ACR device:
     - Map to host interface: [select appropriate interface]
     - Use 7 bits: off
     - Serial input uppercase: off
     - Translate Backspace to: off
     - Enable CLOAD/CSAVE traps: off
     Configure the host interface selected above:
     - 300 baud, 8 bits, no parity, 1 stop bit
   * Enter the first-stage boot loader via the front panel 
     (octal, starting at address 0):
       041 302 037 061 022 000 333 006 
       017 330 333 007 275 310 055 167 
       300 351 003 000
   * Start TeraTerm, connect to the proper serial port, configure
     the serial port for 300 baud 8N1.
   * Set SW15-10 down, SW9-8 up
   * Press RESET on the front panel
   * Press RUN on the front panel
   * In TeraTerm select File->Send File...
   * Check "Binary" in the send dialog
   * select the vdmcuter.tap file and click OK
   * Unfortunately TeraTerm's progress dialog goes away very quickly
     and there is no indication that it is still sending - BUT IT IS!
   * After about 15-20 seconds the LED pattern on the front panel should
     change once. After it changes, you should see A6 faintly flicker,
     indicating that data is received.
   * The loading process should take about 90-120 seconds.
   
4) Load the vdmcuter.wav file through a cassette interface:
   The vdmcuter.wav file contains the same as vdmcuter.tap, but is
   an audio file that needs to be played back into a cassette interface
   connected to one of the Altair Simulator's serial ports.
   Mike Douglas' cassette interface (made for the Altair Clone) should
   work fine on the Altair Simulator: http://altairclone.com/cassette.htm
   * Connect the +3.3V, GND, TX and RX lines of the cassette interface to 
     appropriate pins of a serial port on the Altair Simulator.
   * Connect the audio output of your PC to the audio input of the
     cassette interface
   * Follow the instructions for TAP files above, except:
     - Map ACR to the serial port that is connected to the cassette interface
     - Instead of using TeraTerm, play the .wav file through your PC's audio output

No matter which way you use to load CUTER, after loading finishes
the VDM-1 display should show an empty screen and ">" as a prompt. 
It should react to serial input received via the SIO device. If you
have set up the VDM-1 in the configuration menu to forward keyboard
input to the SIO device then characters typed on the keyboard should
be recognized too.

[Note that the CUTER version supplied here contains a small change such
 that it starts up using SIO as its input device and VDM-1 as output device
 (in the original the SENSE switches would determine the input and output
 devices): Addresses C1E2-C1E3 were changed from DB FF to 3E 04]

Once CUTER is running, you can now load one of the other programs,
you can choose your preferred method:

1) Load .ENT programs:
   * If using the Windows client for the VDM-1 display, you can send
     the .ENT file directly from there:
     - In the VDM-1 configuration in the Altair Simulator's configuration 
       menu make sure that the keyboard is mapped to SIO
     - Set Settings->Character Delay to 1ms
     - Set Settings->Line Delay to 10ms
     - Select File->Send File... and select the .ENT file to send
   * Otherwise you can use TeraTerm to send the .ENT file:
     - Make sure the serial port that TeraTerm is connected to is 
       mapped to SIO
     - In TeraTerm's Setup->Serial Port menu, set a transmit delay
       of 1msec/char and 10msec/line
     - In TeraTerm, select File->Send File... and select the .ENT file to send
   * If you notice missing characters on the screen while the file is
     sent, increase the character and/or line delay

2) Load .HEX programs:
   * Start a terminal connection to the Altair Simulator
   * Press STOP on the simulator's front panel
   * receive HEX file ('H' command or SW6 up and AUX1 down)
   * copy-and-paste the .hex file into your terminal
   * Press RUN on the simulator's front panel

3) Load .TAP programs:
   * Connect the serial port that ACR is mapped to to your PC
   * Enter the configuration monitor, configure the ACR device:
     - Map to host interface: [select appropriate interface]
     - Use 7 bits: off
     - Serial input uppercase: off
     - Translate Backspace to: off
     - Enable CLOAD/CSAVE traps: off
     Configure the host interface selected above:
     - 300 baud, 8 bits, no parity, 1 stop bit
   * Start TeraTerm, connect to the proper serial port, configure
     the serial port for 300 baud 8N1.
   * In CUTER, type GET and press ENTER
   * In TeraTerm select File->Send File...
   * Check "Binary" in the send dialog
   * Select the .tap file and click OK
   * Unfortunately TeraTerm's progress dialog goes away very quickly
     and there is no indication that it is still sending - BUT IT IS!
   * Note that there is no indication in CUTER of whether a file is 
     being received or not. It just hangs until it has successfully
     received a file.

4) Load .WAV programs:
   * Connect the +3.3V, GND, TX and RX lines of the cassette interface to 
     appropriate pins of a serial port on the Altair Simulator.
   * Connect the audio output of your PC to the audio input of the
     cassette interface
   * Follow the instructions for .TAP files above, except:
     - Map ACR to the serial port that is connected to the cassette interface
     - Instead of using TeraTerm, play the .wav file through your PC's audio output

After loading the program you can now run it. To do so, type "EX [a]" 
in CUTER, where [a] is the start address of the program. Each loading
method above will show you the start and end addresses. Most programs
start at 0.
