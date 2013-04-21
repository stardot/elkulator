/*Elkulator v1.0 by Tom Walker
  Memory handling*/
#include <allegro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elk.h"

int FASTLOW=0;
int FASTHIGH2=0;
//#define FASTLOW (turbo || (mrb && mrbmode && mrbmapped))
#define FASTHIGH (FASTHIGH2 && ((pc&0xE000)!=0xC000))

int output,timetolive;
int mrbmapped=0;
int plus1=0;
uint8_t readkeys(uint16_t addr);
uint8_t ram[32768],ram2[32768];
uint8_t os[16384],mrbos[16384];
uint8_t basic[16384],adfs[16384],dfs[16384];
uint8_t rom12[16384],rom13[16384],ram6[16384];
uint8_t sndrom[16384];
uint8_t plus1rom[16384];
uint8_t sndlatch;
int snden=0;
int usedrom6=0;

void loadroms()
{
        FILE *f;
        char path[512],p2[512];
        getcwd(p2,511);
        sprintf(path,"%sroms",exedir);
        printf("path now %s\n",path);
        chdir(path);
        f=fopen("os","rb");
        fread(os,16384,1,f);
        fclose(f);
        f=fopen("os300.rom","rb");
        fread(mrbos,16384,1,f);
        fclose(f);
        f=fopen("basic.rom","rb");
        fread(basic,16384,1,f);
        fclose(f);
        f=fopen("adfs.rom","rb");
        fread(adfs,16384,1,f);
        fclose(f);
        f=fopen("dfs.rom","rb");
        fread(dfs,16384,1,f);
        fclose(f);
        f=fopen("sndrom","rb");
        fread(sndrom,16384,1,f);
        fclose(f);
        f=fopen("plus1.rom","rb");
        fread(plus1rom,16384,1,f);
        fclose(f);
        chdir(p2);
}

void loadcart(char *fn)
{
        FILE *f=fopen(fn,"rb");
        fread(rom12,16384,1,f);
        fclose(f);
}

void loadcart2(char *fn)
{
        FILE *f=fopen(fn,"rb");
        fread(rom13,16384,1,f);
        fclose(f);
}

void unloadcart()
{
        memset(rom12,0,16384);
        memset(rom13,0,16384);
}

void dumpram()
{
        FILE *f=fopen("ram.dmp","wb");
        fwrite(ram,32768,1,f);
        fclose(f);
}

void resetmem()
{
        FASTLOW=turbo || (mrb && mrbmode);
        FASTHIGH2=(mrb && mrbmode==2);
}

uint8_t readmem(uint16_t addr)
{
        if (debugon) debugread(addr);
        if (addr==pc) fetchc[addr]=31;
        else          readc[addr]=31;
        if (addr<0x2000)
        {
                if (FASTLOW) return ram2[addr];
                waitforramsync();
                cycles++;
                return ram[addr];
        }
        if (addr<0x8000)
        {
                if (FASTHIGH) return ram2[addr];
                waitforramsync();
                cycles++;
                return ram[addr];
        }
        if (addr<0xC000)
        {
                if (!extrom)
                {
                        if (intrombank&2) return basic[addr&0x3FFF];
                        return readkeys(addr);
                }
                if (rombank==0x0) return rom12[addr&0x3FFF];
                if (rombank==0x1) return rom13[addr&0x3FFF];
                if (plus1 && rombank==0xC) return plus1rom[addr&0x3FFF];
                if (sndex && rombank==0xD) return sndrom[addr&0x3FFF];
                if (rombank==0xF && plus3 && adfsena) return adfs[addr&0x3FFF];
                if (rombank==0x3 && plus3 && dfsena)  return dfs[addr&0x3FFF];
                if (rombank==0x6) return ram6[addr&0x3FFF];
//                if (rombank==0) return game[addr&0x3FFF];
                return addr>>8;
        }
        if ((addr&0xFF00)==0xFC00 || (addr&0xFF00)==0xFD00)
        {
                if ((addr&0xFFF8)==0xFCC0 && plus3) return read1770(addr);
                if (addr==0xFCC0 && firstbyte) return readfirstbyte();
                if (plus1)
                {
                        if (addr==0xFC70) return readadc();
                        if (addr==0xFC72) return getplus1stat();
                }
//                if ((addr&~0x1F)==0xFC20) return readsid(addr);
                return addr>>8;
        }
        if ((addr&0xFF00)==0xFE00) return readula(addr);
        if (mrb) return mrbos[addr&0x3FFF];
        return os[addr&0x3FFF];
}

uint16_t pc;
void writemem(uint16_t addr, uint8_t val)
{
        if (debugon) debugwrite(addr,val);
        writec[addr]=31;
//        if (addr==0x5820) rpclog("Write 5820\n");
//        if (addr==0x5B10) rpclog("Write 5B10\n");
//        if (addr==0x5990) rpclog("Write 5990\n");
//        if (addr==0x5D70) rpclog("Write 5D70\n");
//        if (addr==0x6798) rpclog("Write 6798\n");
//        if (addr==0x5C00) rpclog("Write 5C00\n");
//        if (addr==0x6710) rpclog("Write 6710\n");
//        if (addr==0x5FC0) rpclog("Write 5FC0\n");
//        if (addr==0x6490) rpclog("Write 6490\n");
//        if (addr==0x5820) rpclog("Write 5820\n");
//        if (addr>=0x5800 && addr<0x8000) rpclog("Write %04X %02X %04X %i %i\n",addr,val,pc,FASTHIGH,FASTHIGH2);
//if (addr>0xFC00) rpclog("Write %04X %02X\n",addr,val);
        if (addr<0x2000)
        {
                if (FASTLOW) ram2[addr]=val;
                else
                {
                        waitforramsync();
                        cycles++;
                        ram[addr]=val;
                }
                return;
        }
        if (addr<0x8000)
        {
                if (FASTHIGH)
                {
                        ram2[addr]=val;
                        return;
                }
                waitforramsync();
                cycles++;
                ram[addr]=val;
                return;
        }
        if (addr<0xC000)
        {
                if (extrom && rombank==0xD && (addr&0x2000)) sndrom[addr&0x3FFF]=val;
                if (extrom && rombank==0x3 && plus3 && dfsena) dfs[addr&0x3FFF]=val;
                if (extrom && rombank==0x6) { ram6[addr&0x3FFF]=val; usedrom6=1; }
        }
        if ((addr&0xFF00)==0xFE00) writeula(addr,val);
        if ((addr&0xFFF8)==0xFCC0 && plus3) write1770(addr,val);
//        if ((addr&~0x1F)==0xFC20) writesid(addr,val);
        if (addr==0xFC98)
        {
//                rpclog("FC98 write %02X\n",val);
                sndlatch=val;
        }
        if (addr==0xFC99)
        {
//                rpclog("FC99 write %02X\n",val);
                if ((val&1) && !snden)
                {
//                        rpclog("Writesound! %02X\n",sndlatch);
                        writesound(sndlatch);
                }
                snden=val&1;
        }
        if (addr==0xFC7F && mrb)
        {
                mrbmapped=!(val&0x80);
                FASTLOW=(turbo || (mrb && mrbmode && mrbmapped));
                FASTHIGH2=(mrb && mrbmode==2 && mrbmapped);
                
//                rpclog("Write MRB %02X %i %i %04X\n",val,FASTLOW,FASTHIGH2,pc);
//                if (!val) output=1;
        }
        if (addr==0xFC70 && plus1) writeadc(val);
}

int keys[2][14][4]=
{
        {
                {KEY_RIGHT,KEY_END,0,KEY_SPACE},
                {KEY_LEFT,KEY_DOWN,KEY_ENTER,KEY_DEL},
                {KEY_MINUS,KEY_UP,KEY_QUOTE,0},
                {KEY_0,KEY_P,KEY_COLON,KEY_SLASH},
                {KEY_9,KEY_O,KEY_L,KEY_STOP},
                {KEY_8,KEY_I,KEY_K,KEY_COMMA},
                {KEY_7,KEY_U,KEY_J,KEY_M},
                {KEY_6,KEY_Y,KEY_H,KEY_N},
                {KEY_5,KEY_T,KEY_G,KEY_B},
                {KEY_4,KEY_R,KEY_F,KEY_V},
                {KEY_3,KEY_E,KEY_D,KEY_C},
                {KEY_2,KEY_W,KEY_S,KEY_X},
                {KEY_1,KEY_Q,KEY_A,KEY_Z},
                {KEY_ESC,KEY_ALT,KEY_LCONTROL,KEY_LSHIFT}
        },
        {
                {KEY_RIGHT,KEY_END,0,KEY_SPACE},
                {KEY_LEFT,KEY_DOWN,KEY_ENTER,KEY_BACKSPACE},
                {KEY_MINUS,KEY_UP,KEY_QUOTE,0},
                {KEY_0,KEY_P,KEY_SEMICOLON,KEY_SLASH},
                {KEY_9,KEY_O,KEY_L,KEY_STOP},
                {KEY_8,KEY_I,KEY_K,KEY_COMMA},
                {KEY_7,KEY_U,KEY_J,KEY_M},
                {KEY_6,KEY_Y,KEY_H,KEY_N},
                {KEY_5,KEY_T,KEY_G,KEY_B},
                {KEY_4,KEY_R,KEY_F,KEY_V},
                {KEY_3,KEY_E,KEY_D,KEY_C},
                {KEY_2,KEY_W,KEY_S,KEY_X},
                {KEY_1,KEY_Q,KEY_A,KEY_Z},
                {KEY_ESC,KEY_TILDE,KEY_RCONTROL,KEY_RSHIFT}
        }
};

int keyl[128];

void makekeyl()
{
        int c,d,e;
        memset(keyl,0,sizeof(keyl));
        for (c=0;c<14;c++)
        {
                for (d=0;d<4;d++)
                {
                        for (e=0;e<2;e++)
                        {
                                keyl[keys[e][c][d]]=c|(d<<4)|0x80;
                        }
                }
        }
}

uint8_t readkeys(uint16_t addr)
{
        int d;
        uint8_t temp=0;
        for (d=0;d<128;d++)
        {
                if (key[d] && keyl[keylookup[d]]&0x80 && !(addr&(1<<(keyl[keylookup[d]]&15)))) temp|=1<<((keyl[keylookup[d]]&0x30)>>4);
//                if (autoboot && (d==KEY_LSHIFT || d==KEY_RSHIFT) && !(addr&(1<<(keyl[d]&15)))) temp|=1<<((keyl[keylookup[d]]&0x30)>>4);
        }
  //      if (autoboot && !(addr&(1<<(keyl[KEY_LSHIFT]&15)))) temp|=1<<((keyl[keylookup[KEY_LSHIFT]]&0x30)>>4);
//        rpclog("Readkey %i %04X %02X %02X %04X\n",autoboot,addr,temp,keyl[KEY_LSHIFT],1<<(keyl[KEY_LSHIFT]&15));
/*        for (c=0;c<14;c++)
        {
                if (!(addr&(1<<c)))
                {
                        if (key[keylookup[keys[0][c][0]]]) temp|=1;
                        if (key[keylookup[keys[0][c][1]]]) temp|=2;
                        if (key[keylookup[keys[0][c][2]]]) temp|=4;
                        if (key[keylookup[keys[0][c][3]]]) temp|=8;
                        if (key[keylookup[keys[1][c][0]]]) temp|=1;
                        if (key[keylookup[keys[1][c][1]]]) temp|=2;
                        if (key[keylookup[keys[1][c][2]]]) temp|=4;
                        if (key[keylookup[keys[1][c][3]]]) temp|=8;
                }
        }*/
        return temp;
}

void savememstate(FILE *f)
{
        fwrite(ram,32768,1,f);
        if (mrb) fwrite(ram+32768,32768,1,f);
        if (plus3 && dfsena) fwrite(dfs,16384,1,f);
        if (sndex)  fwrite(sndrom,16384,1,f);
        if (usedrom6) fwrite(ram6,16384,1,f);
}

void loadmemstate(FILE *f)
{
        fread(ram,32768,1,f);
        if (mrb) fread(ram+32768,32768,1,f);
        if (plus3 && dfsena) fread(dfs,16384,1,f);
        if (sndex)  fread(sndrom,16384,1,f);
        if (usedrom6) fread(ram6,16384,1,f);
}
