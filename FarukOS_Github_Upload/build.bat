@echo off
setlocal
echo ===========================================
echo       BUILDING CUSTOM OPERATING SYSTEM
echo ===========================================

:: Set up tool paths
set "TOOLS_DIR=C:\Users\faruk\Desktop\antigravity\tools\bin"
set "QEMU_DIR=C:\Program Files\qemu"
set "PATH=%TOOLS_DIR%;%QEMU_DIR%;%PATH%"

echo.
echo [1/3] Assembling assembly files ...
nasm -f elf32 boot.s -o boot.o
nasm -f elf32 gdt_s.s -o gdt_s.o
nasm -f elf32 idt_s.s -o idt_s.o
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Assembling failed!
    exit /b %ERRORLEVEL%
)

echo [2/3] Compiling C files ...
i686-elf-gcc -c kernel.c -o kernel.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c gdt.c -o gdt.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c idt.c -o idt.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c pic.c -o pic.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c keyboard.c -o keyboard.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c mouse.c -o mouse.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c bga.c -o bga.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c rtc.c -o rtc.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c ata.c -o ata.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c sb16.c -o sb16.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Compiling failed!
    exit /b %ERRORLEVEL%
)

echo [3/3] Linking ...
i686-elf-gcc -T linker.ld -o myos.bin -ffreestanding -O2 -nostdlib boot.o gdt_s.o idt_s.o kernel.o gdt.o idt.o pic.o keyboard.o mouse.o bga.o rtc.o ata.o sb16.o -lgcc
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Linking failed!
    exit /b %ERRORLEVEL%
)

echo.
echo ===========================================
echo       BUILD SUCCESSFUL! SETUP DRIVE
echo ===========================================
if not exist hdd.img (
    echo Creating 100MB Hard Drive Image ^(hdd.img^)
    powershell -Command "$file = [System.IO.File]::Create('hdd.img'); $file.SetLength(104857600); $file.Close()"
)

echo.
echo ===========================================
echo       RUNNING IN QEMU
echo ===========================================
echo.

qemu-system-i386 -kernel myos.bin -drive file=hdd.img,format=raw,if=ide -display sdl,gl=off -full-screen -audiodev dsound,id=snd0 -device sb16,audiodev=snd0

pause
