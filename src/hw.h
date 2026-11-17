#pragma once
#include "types.h"

#ifdef HOST
extern u8 host_io[0x400];
extern u16 host_pal[512];
extern u8 host_vram[0x18000];
extern u16 host_oam[512];
#define IO_PTR(o) ((volatile u8 *)host_io + (o))
#define PAL_BG ((volatile u16 *)host_pal)
#define PAL_OBJ ((volatile u16 *)(host_pal + 256))
#define VRAM ((volatile u8 *)host_vram)
#define OAM_MEM ((volatile u16 *)host_oam)
#else
#define IO_PTR(o) ((volatile u8 *)0x04000000 + (o))
#define PAL_BG ((volatile u16 *)0x05000000)
#define PAL_OBJ ((volatile u16 *)0x05000200)
#define VRAM ((volatile u8 *)0x06000000)
#define OAM_MEM ((volatile u16 *)0x07000000)
#endif

#define REG16(o) (*(volatile u16 *)IO_PTR(o))
#define REG32(o) (*(volatile u32 *)IO_PTR(o))

#define REG_DISPCNT REG16(0x00)
#define REG_DISPSTAT REG16(0x04)
#define REG_VCOUNT REG16(0x06)
#define REG_BGCNT(n) REG16(0x08 + 2 * (n))
#define REG_BGHOFS(n) REG16(0x10 + 4 * (n))
#define REG_BGVOFS(n) REG16(0x12 + 4 * (n))
#define REG_BLDCNT REG16(0x50)
#define REG_BLDALPHA REG16(0x52)
#define REG_BLDY REG16(0x54)
#define REG_DMA_SAD(n) REG32(0xB0 + 12 * (n))
#define REG_DMA_DAD(n) REG32(0xB4 + 12 * (n))
#define REG_DMA_CNT(n) REG32(0xB8 + 12 * (n))
#define REG_KEYINPUT REG16(0x130)
#define REG_IE REG16(0x200)
#define REG_IF REG16(0x202)
#define REG_WAITCNT REG16(0x204)
#define REG_IME REG16(0x208)

#define KEY_A 0x001
#define KEY_B 0x002
#define KEY_SELECT 0x004
#define KEY_START 0x008
#define KEY_RIGHT 0x010
#define KEY_LEFT 0x020
#define KEY_UP 0x040
#define KEY_DOWN 0x080
#define KEY_R 0x100
#define KEY_L 0x200
#define REG_MOSAIC REG16(0x4C)
#define REG_WIN0H REG16(0x40)
#define REG_WIN1H REG16(0x42)
#define REG_WIN0V REG16(0x44)
#define REG_WIN1V REG16(0x46)
#define REG_WININ REG16(0x48)
#define REG_WINOUT REG16(0x4A)
