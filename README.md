[README.md](https://github.com/user-attachments/files/26772845/README.md)
# FarukOS

FarukOS is a custom-built, 32-bit monolithic operating system developed in C and Assembly. It features a graphical user interface (GUI) with a window manager, basic event-driven applications, and low-level hardware drivers written from scratch.

## Features
- **32-Bit Protected Mode Kernel**: Custom bootloader transitioning from 16-bit to 32-bit flat memory model.
- **Hardware Drivers**: Direct I/O port interactions for PS/2 Keyboard/Mouse, RTC, SB16 Audio, and ATA Disk IDE.
- **Window Management**: An event-driven loop capable of rendering multiple overlapping windows securely.
- **Virtual File System (VFS)**: In-memory file hierarchy to organize apps and files.
- **Built-in Applications**:
  - `Minesweeper` (Fully playable classic logic game)
  - `Tic Tac Toe` (With smart win/draw detection)
  - `Settings` (Real-time Dark/Light theme switching)
  - `Calculator`, `Clock`, `Snake`

## Architecture Highlights
- **Interrupts (IDT)**: Custom interrupt service routines (ISRs) for handling PIC signals cleanly without lagging the main process.
- **Redraw Pacing (60 FPS)**: An internal tick mechanism to support smooth backgrounds and animations without demanding excessive memory (No 100% spinlock drain on CPU).

## How to Compile & Run (Windows)
1. You must have `i686-elf-gcc` cross-compiler and `nasm` installed.
2. Ensure QEMU is installed (`qemu-system-i386`).
3. Run `build.bat` using the terminal.
```batch
.\build.bat
```
*(The build script will execute the assembly files, compile C sources, link everything into `myos.bin`, auto-generate an empty `hdd.img` if missing, and launch QEMU.)*

## Screenshots
*(Insert your own screenshots from the emulator here!)*

*FarukOS Project developed for Operating System Design Course.*
