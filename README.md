[![N-Vision](https://raw.githubusercontent.com/ngagesdk/N-Vision/main/media/logo.svg)](https://raw.githubusercontent.com/ngagesdk/N-Vision/main/media/logo.svg?raw=true "N-Vision")

# N-Vision

**A modern replacement for the original Nokia N-Gage screen capture hardware.**

## Overview

N-Vision is an open-source hardware project that aims to recreate the functionality
of the extremely rare screen capture device that was once used by Nokia during
promotional events, game demonstrations, and for recording official N-Gage gameplay
footage.

As original capture units have become virtually impossible to find, preserving and
showcasing N-Gage software has become increasingly difficult. N-Vision provides a
modern alternative by tapping directly into the display interface without requiring
any modifications to the console itself.

The project uses a custom flexible PCB (FPC) that is installed between the N-Gage's
mainboard and LCD, allowing the display signals to be captured transparently.
A Raspberry Pi Pico 2 then decodes the display data in real time and outputs it over
DVI, making it suitable for recording, streaming, or development purposes.

## Features (Planned)

- Non-destructive installation using a custom flexible PCB
- Real-time LCD signal capture
- Display decoding on a Raspberry Pi Pico 2
- Digital DVI video output
- Open-source hardware and firmware
- Designed specifically for the Nokia N-Gage Classic

## Project Structure
 
| Directory              | Description                                                                                             |
| :--------------------- | :------------------------------------------------------------------------------------------------------ |
| [decoder/](decoder/)   | Proof-of-concept decoder to toy around with display signals.                                            |
| [firmware/](firmware/) | Firmware for the Raspberry Pi Pico 2 that captures and decodes the N-Gage display signals in real time. |
| [hardware/](hardware/) | Hardware design files for the custom flexible PCB that interfaces with the N-Gage display.              | 

## Project Status

> **This project is currently under active development and is not yet complete.**

The hardware, firmware, and documentation are all works in progress. Expect incomplete
features, design changes, and occasional breaking updates as development continues.

## Contributing

Contributions are warmly welcomed.

Whether you have experience with hardware design, embedded development, reverse
engineering, signal analysis, documentation, or simply own test hardware, your help
is greatly appreciated. Every contribution helps move the project closer to becoming
a practical and accessible replacement for the original capture hardware.

If you'd like to contribute, feel free to open an issue, start a discussion, or submit
a pull request.

## Goals

- Preserve the ability to capture authentic Nokia N-Gage gameplay.
- Provide an affordable and reproducible alternative to the original Nokia capture
  hardware.
- Document the N-Gage display interface for future preservation efforts.
- Build an open platform that the community can improve and extend.

## License

N-Vision is released under a dual-license model:

- **Software** (firmware, utilities, and related source code) is licensed under the
  [**MIT License**](firmware/LICENSE.md).
- **Hardware** (schematics, PCB layouts, mechanical designs, and other hardware design
  files) is licensed under the
  [**CERN Open Hardware Licence Version 2 - Permissive (CERN-OHL-P v2)**](hardware/cern_ohl_p_v2.txt).
