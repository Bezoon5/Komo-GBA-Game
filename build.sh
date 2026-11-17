#!/bin/sh
set -e
cd "$(dirname "$0")"
python3 tools/make_assets.py
mkdir -p out
CF="--target=armv4t-none-eabi -mcpu=arm7tdmi -mthumb -O2 -ffreestanding -nostdlib -fno-builtin -Wall -Wno-unused-function -Isrc"
clang $CF -c src/main.c -o out/main.o
clang $CF -c src/rt.c -o out/rt.o
clang $CF -O1 -c src/gen/assets.c -o out/assets.o
clang --target=armv4t-none-eabi -mcpu=arm7tdmi -c src/crt0.s -o out/crt0.o
ld.lld -T src/gba.ld out/crt0.o out/main.o out/rt.o out/assets.o -o out/komo.elf
llvm-objcopy -O binary out/komo.elf out/komo.gba 2>/dev/null || objcopy -O binary out/komo.elf out/komo.gba
python3 tools/fixheader.py out/komo.gba
gcc -O2 -g -fsanitize=address,undefined -Wall -Wno-unused-function -Isrc host/host.c src/gen/assets.c -o out/host_test
echo built
