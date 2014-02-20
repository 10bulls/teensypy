teensypy

This program demonstrates a very basic use of the micropython python library for use with a Teensy 3.1 ARM board.

Big thanks to Dave Hylands for getting mircopython working for the Teensy, Damien George for his great micropython project
and Paul Stoffregen for packing so much into a Teensy package.

The python support is added using a static library build of the core micropython files (libmpython.a).
This static was built using this project...
https://github.com/10bulls/libmpython

This project uses Visual Micro (http://www.visualmicro.com/) but can also be built using the Arduino IDE.

To build this project, the build environment first needs to be set up.
The files used for this are located in the 'libraries' folder.

Copy 'mpython' folder to the arduino\libaries folder.

Add the contents of 'boards.txt' to the existing 'arduino/hardware/teensy/boards.txt'
(DO NOT REPLACE THE EXISTING ONE!)
This file contains hard coded paths that will need to be modified.

Copy 'mk20dx256py.ld' to 'arduino/hardware/teensy/cores/teensy3'

Needs Teensyduino 1.18 RC # 2 or later for programs > 128K
http://forum.pjrc.com/threads/24796-Teensyduino-1-18-Release-Candidate-2-Available

Python support can then be added to a project by simply selecting 'Teensy 3.1+Python' as the target board.

///////////////////////////////////////////////////////////////////////////////

Resources...

Main micropython site:
http://www.micropython.org/

micropython git repository:
https://github.com/micropython/micropython

Dave Hylands branch of micropython:
https://github.com/dhylands/micropython.git

Micropython on the teensy forum:
http://forum.pjrc.com/threads/24794-MicroPython-for-Teensy-3-1
