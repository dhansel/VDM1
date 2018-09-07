There are two versions of the firmware available:

* VDM1-bootload.hex
  A version of the firmware that can be uploaded via the boot loader
  that gets installed on the PIC32 with Geoff Graham's firmware.

* VDM1-programmer.hex
  A version of the firmware that can be uploaded via a PICkit 3 programmer
  using the free MPLAB X IPE software (comes with the MPLAB X IDE).

To compile the firmware yourself:
1. Start the MPLAB X IDE software and load the VDM1.X project.
2. Select the configuration (bootload/programmer) you want to build
3. build the project

Note that when using the "bootload" configuration you will not be able to run
or debug the code from within the MPLAB X IDE.
