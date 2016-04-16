#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <allegro.h>
#include <stdio.h>

#define printf rpclog

void rpclog(char *format, ...);

extern int videoresize;
extern int winsizex, winsizey;

extern uint8_t a,x,y,s;
extern uint16_t pc;

typedef struct
{
        int c,z,i,d,v,n;
} CPUStatus;
extern CPUStatus p;

extern uint8_t opcode;
extern int nmi,irq;
extern int rombank,intrombank;
extern int cycles,ulacycles;
extern int extrom;
extern uint8_t ram[32768];
extern uint16_t pc;

uint8_t readmem(uint16_t addr);
void writemem(uint16_t addr, uint8_t val);

extern int cswena;
extern int tapelcount,tapellatch;
extern int tapeon;
void polltape();
void polluef();
void pollcsw();

void openuef(char *fn);
void closeuef();
void opencsw(char *fn);
void closecsw();

extern int resetit;

extern char tapename[512];

extern int infocus;

void resetsound();

extern int plus3;

void dumpregs();

extern int turbo;
extern int mrb,mrbmode,mrbmapped;
extern int ulamode;


void error(const char *format, ...);
//void rpclog(const char *format, ...);

extern char discname[260];
extern char discname2[260];
extern int discchanged[2];

extern int tapewrite;

extern int dfsena,adfsena;
extern int sndex;

#define SCANLINES 0
#define LINEDBL   1
#define _2XSAI    2
#define SCALE2X   3
#define EAGLE     4
#define PAL       5
extern int drawmode;

#define HALFSIZE   (drawmode==_2XSAI || drawmode==SCALE2X || drawmode==EAGLE)
#define LINEDOUBLE (drawmode==SCANLINES || drawmode==PAL)

#define TAPE_SLOW 0
#define TAPE_FAST 1
#define TAPE_REALLY 2
extern int tapespeed;


struct
{
        void (*seek)(int drive, int track);
        void (*readsector)(int drive, int sector, int track, int side, int density);
        void (*writesector)(int drive, int sector, int track, int side, int density);
        void (*readaddress)(int drive, int track, int side, int density);
        void (*format)(int drive, int track, int side, int density);
        void (*poll)();
} drives[2];

int curdrive;

void ssd_reset();
void ssd_load(int drive, char *fn);
void ssd_close(int drive);
void dsd_load(int drive, char *fn);
void ssd_seek(int drive, int track);
void ssd_readsector(int drive, int sector, int track, int side, int density);
void ssd_writesector(int drive, int sector, int track, int side, int density);
void ssd_readaddress(int drive, int sector, int side, int density);
void ssd_format(int drive, int sector, int side, int density);
void ssd_poll();

void adf_reset();
void adf_load(int drive, char *fn);
void adl_loadex(int drive, char *fn, int sectors, int size, int dblstep);
void adf_close(int drive);
void adl_load(int drive, char *fn);
void adf_seek(int drive, int track);
void adf_readsector(int drive, int sector, int track, int side, int density);
void adf_writesector(int drive, int sector, int track, int side, int density);
void adf_readaddress(int drive, int sector, int side, int density);
void adf_format(int drive, int sector, int side, int density);
void adf_poll();

void fdi_reset();
void fdi_load(int drive, char *fn);
void fdi_close(int drive);
void fdi_seek(int drive, int track);
void fdi_readsector(int drive, int sector, int track, int side, int density);
void fdi_writesector(int drive, int sector, int track, int side, int density);
void fdi_readaddress(int drive, int sector, int side, int density);
void fdi_format(int drive, int sector, int side, int density);
void fdi_poll();

void loaddisc(int drive, char *fn);
void newdisc(int drive, char *fn);
void closedisc(int drive);
void disc_reset();
void disc_poll();
void disc_seek(int drive, int track);
void disc_readsector(int drive, int sector, int track, int side, int density);
void disc_writesector(int drive, int sector, int track, int side, int density);
void disc_readaddress(int drive, int track, int side, int density);
void disc_format(int drive, int track, int side, int density);

void setejecttext(int drive, char *fn);

#define WD1770 1

void (*fdccallback)();
void (*fdcdata)(uint8_t dat);
void (*fdcspindown)();
void (*fdcfinishread)();
void (*fdcnotfound)();
void (*fdcdatacrcerror)();
void (*fdcheadercrcerror)();
void (*fdcwriteprotect)();
int  (*fdcgetdata)(int last);

extern int writeprot[2],fwriteprot[2];
extern int defaultwriteprot;

extern int motoron,fdctime,disctime;


void initresid();
void resetsid();
void setsidtype(int resamp, int model);
uint8_t readsid(uint16_t addr);
void writesid(uint16_t addr, uint8_t val);
extern int cursid,sidmethod;

extern int wantloadstate,wantsavestate;
extern char ssname[260];

extern int usedrom6;

extern int firstbyte;

extern int keylookup[128];
extern int plus1;
extern uint8_t plus1stat;
extern int adctime;

extern uint8_t readc[65536],writec[65536],fetchc[65536];

extern int debug,debugon;

extern char scrshotname[260];
extern char moviename[260];
extern uint8_t sndstreambuf[626];
extern int sndstreamptr;

extern int autoboot;
extern int sndint;
extern int sndddnoise,sndtape;
extern int ddvol,ddtype;

extern int discspd;
extern int motorspin;

char exedir[512];

void initelk();
void closeelk();
void cleardrawit();
void runelk();

void redefinekeys();

void loadconfig();
void saveconfig();

void initalmain(int argc, char *argv[]);
void inital();
void addsnd(uint8_t dat);
void mixbuffer(int16_t *d);
void givealbufferdd(int16_t *buf);

void loadroms();
void resetmem();
void dumpram();
void loadcart(char *fn);
void loadcart2(char *fn);
void unloadcart();
void loadmemstate(FILE *f);
void savememstate(FILE *f);

void reset6502();
void reset6502e();
void exec6502();
void load6502state(FILE *f);
void save6502state(FILE *f);

void initula();
void resetula();
uint8_t readula(uint16_t addr);
void writeula(uint16_t addr, uint8_t val);
void yield();
void waitforramsync();
void intula(uint8_t num);
void receive(uint8_t val);
void enterfullscreen();
void leavefullscreen();
void clearall();
void clearscreen();
void savescrshot();
void loadulastate(FILE *f);
void saveulastate(FILE *f);

void reset1770();
uint8_t read1770(uint16_t addr);
void write1770(uint16_t addr, uint8_t val);

void loadtape(char *fn);
void reallyfasttapepoll();

void initsound();
void writesound(uint8_t data);
void logvols();

void loaddiscsamps();
void mixddnoise();
void closeddnoise();
void ddnoise_seek(int len);

void maketapenoise();
void adddatnoise(uint8_t dat);
void addhightone();
void mixtapenoise(int16_t *tapebuffer);

void makekeyl();

void loadstate();
void savestate();
void doloadstate();
void dosavestate();

void dodebugger();
void debugread(uint16_t addr);
void debugwrite(uint16_t addr, uint8_t val);
void startdebug();
void enddebug();

uint8_t readfirstbyte();
extern int joffset;

uint8_t readadc();
void writeadc(uint8_t val);
uint8_t getplus1stat();

void initpaltables();

void startblit();
void endblit();
void setquit();

void scale2x(BITMAP *src, BITMAP *dst, int width, int height);
void palfilter(BITMAP *src, BITMAP *dest, int depth);

void entergui();

#ifdef __cplusplus
}
#endif
