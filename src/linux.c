#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
/*Elkulator v1.0 by Sarah Walker
  Linux main loop*/
#ifndef WIN32

#include <allegro.h>
#include "elk.h"

char ssname[260];
char scrshotname[260];
char moviename[260];

int fullscreen=0;
int gotofullscreen=0;
int videoresize=0;
int wantloadstate=0,wantsavestate=0;
int winsizex=640,winsizey=512;

int plus3=0;
int dfsena=0,adfsena=0;
int turbo=0;
int mrb=0,mrbmode=0;
int ulamode=0;
int drawmode=0;

char discname[260];
char discname2[260];
int quited=0;
int infocus=1;

void native_window_close_button_handler(void)
{
       quited = 1;
}

void startblit()
{
}
void endblit()
{
}

int keylookup[128];

/* Test control: when ELKULATOR_CONTROL names a directory, each pass of the
   main loop runs the commands in $ELKULATOR_CONTROL/cmd (one per line),
   writes their results to $ELKULATOR_CONTROL/out and removes cmd. A test
   writes cmd (atomically, via a rename) and waits for out. Commands:
     regs               PC, A, X, Y and S in hex
     peek ADDR LEN      hex bytes of RAM (addresses in hex)
     poke ADDR HEX...   write bytes
     key CODE 0|1       release/press an Allegro key code
     shot FILE          save a screenshot (BMP)                      */
extern uint8_t ram[32768];
extern uint16_t pc;
extern uint8_t a, x, y, s;
void savescrshot();
static void control_poll(void)
{
        static char dir[512];
        static int checked = 0;
        char path[600], outpath[600], line[512];
        FILE *in, *out;
        if (!checked) {
                const char *env = getenv("ELKULATOR_CONTROL");
                checked = 1;
                dir[0] = 0;
                if (env) snprintf(dir, sizeof dir, "%s", env);
        }
        if (!dir[0]) return;
        snprintf(path, sizeof path, "%s/cmd", dir);
        in = fopen(path, "r");
        if (!in) return;
        snprintf(outpath, sizeof outpath, "%s/out.tmp", dir);
        out = fopen(outpath, "w");
        while (fgets(line, sizeof line, in)) {
                unsigned addr, len, code, value;
                char name[260];
                if (!strncmp(line, "regs", 4)) {
                        fprintf(out, "%04x %02x %02x %02x %02x\n", pc, a, x, y, s);
                } else if (sscanf(line, "peek %x %u", &addr, &len) == 2) {
                        unsigned i;
                        for (i = 0; i < len; i++)
                                fprintf(out, "%02x", ram[(addr + i) & 0x7FFF]);
                        fprintf(out, "\n");
                } else if (sscanf(line, "key %u %u", &code, &value) == 2) {
                        if (code < KEY_MAX) key[code] = value ? 1 : 0;
                        fprintf(out, "ok\n");
                } else if (sscanf(line, "shot %259s", name) == 1) {
                        snprintf(scrshotname, sizeof scrshotname, "%s", name);
                        savescrshot();
                        fprintf(out, "ok\n");
                } else if (sscanf(line, "poke %x", &addr) == 1) {
                        char *p = line + 5;
                        while (*p && *p != ' ') p++;
                        while (*p == ' ') {
                                unsigned b;
                                p++;
                                if (sscanf(p, "%2x", &b) != 1) break;
                                ram[addr++ & 0x7FFF] = b;
                                p += 2;
                        }
                        fprintf(out, "ok\n");
                }
        }
        fclose(in);
        fclose(out);
        remove(path);
        snprintf(path, sizeof path, "%s/out", dir);
        rename(outpath, path);
}


int main(int argc, char *argv[])
{
        int ret = allegro_init();
        if (ret != 0)
        {
                fprintf(stderr, "Error %d initializing Allegro.\n", ret);
                exit(-1);
        }
        initelk(argc,argv);
        set_close_button_callback(native_window_close_button_handler);
        install_mouse();
        while (!quited)
        {
                runelk();
                control_poll();
                if (menu_pressed()) entergui();
        }
        closeelk();
        return 0;
}

END_OF_MAIN();

#endif
