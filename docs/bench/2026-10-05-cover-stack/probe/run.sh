#!/bin/sh
# Build the probe around the firmware's own cover_soft.c.obj and run it under QEMU.
#
#   run.sh <build>/esp-idf/cover/CMakeFiles/__idf_cover.dir/src/cover_soft.c.obj
#
# Run gen_images.py first (it writes images.c). Tools: the esp32p4 toolchain the firmware is
# built with, and Espressif's qemu-system-riscv32 (generic "virt" machine; it has no P4 model,
# and needs none: the code under test is integer-only, no FPU or PIE instructions).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
OBJ="$1"
T="${RISCV_BIN:-/c/Espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin}"
Q="${QEMU:-/c/Espressif/tools/qemu-riscv32/esp_develop_9.2.2_20260417/qemu/bin/qemu-system-riscv32.exe}"
CF="-march=rv32imafc_zicsr_zifencei_xesppie -mabi=ilp32f -O2 -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -mcmodel=medany -g"
cd "$HERE"
for f in crt0.S main.c mem.c images.c; do
    "$T/riscv32-esp-elf-gcc" $CF -c "$f" -o "${f%.*}.o"
done
"$T/riscv32-esp-elf-gcc" $CF -nostdlib -nostartfiles -Wl,--no-relax -Wl,--no-warn-rwx-segments -T link.ld \
    crt0.o main.o mem.o images.o "$OBJ" -lgcc -o probe.elf
"$Q" -machine virt -cpu rv32 -smp 1 -m 512M -nographic -bios none -kernel probe.elf \
    -monitor none -serial stdio
