#pragma once

#include "DoomDef.h"

#define ZONEID2 0x1d4a
#define PU_FREE -1
#define PU_NEWBLOCK -2

typedef struct vramblock_s vramblock_t;

struct vramblock_s
{
    int size;
    short id;
    short tag;
    int prevtic;
    void** gfx;
    byte* block;
    vramblock_t* prev;
    vramblock_t* next;
};

typedef struct
{
    int size;               // total bytes malloced, including header
    int free;
    vramblock_t blocklist;  // start / end cap for linked list
    vramblock_t* rover;
} vramzone_t;

extern vramzone_t* vramzone;

void Z_VFree(vramzone_t* vram, vramblock_t* block);
vramblock_t* Z_VAlloc(vramzone_t* vram, int size, int tag, void* gfx);
void Z_SetVAllocList(vramzone_t* vram);
void Z_VTouch(vramzone_t* vram, vramblock_t* block);
int Z_FreeVMemory(vramzone_t* vram);
void Z_VLoadCache();
