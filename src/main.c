/*Elkulator v1.0 by Tom walker
  Initialisation/Closing/Main loop*/
#include <allegro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elk.h"
#include "ap5_tube.h"
#undef printf
int autoboot;
static int scripted_keys[256];
static int scripted_delays[256];
static int scripted_key_count;
static int scripted_key_index;
static int scripted_key_delay;
static int scripted_key_frames;

static void parse_scripted_keys(const char *spec)
{
        char copy[4096];
        char *item;
        strncpy(copy, spec, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = 0;
        item = strtok(copy, ",");
        while (item && scripted_key_count < 256)
        {
                char *separator = strchr(item, ':');
                int delay = 5;
                int code;
                if (separator)
                {
                        *separator = 0;
                        delay = atoi(item);
                        code = atoi(separator + 1);
                }
                else code = atoi(item);
                if ((code >= 0 && code < KEY_MAX) ||
                    code == 2000 || code == 2001 ||
                    (code >= 1000 && code < 1000 + KEY_MAX))
                {
                        scripted_delays[scripted_key_count] = delay;
                        scripted_keys[scripted_key_count++] = code;
                }
                item = strtok(NULL, ",");
        }
        if (scripted_key_count)
                scripted_key_delay = scripted_delays[0];
}
FILE *rlog;
void rpclog(char *format, ...)
{
   char buf[256];
   return;
   if (!rlog) rlog=fopen("e:/devcpp/cycleelk/rlog.txt","wt");
//turn;
   va_list ap;
   va_start(ap, format);
   vsprintf(buf, format, ap);
   va_end(ap);
   fputs(buf,rlog);
   fflush(rlog);
}

/*int waiting,waiting2;

void waitforthread()
{
        return;
        waiting=1;
        while (!waiting2)
              sleep(10);
}

void stopwaiting()
{
        waiting=0;
}*/

int drawit=0;
void drawitint()
{
        drawit++;
}

void cleardrawit()
{
        drawit=0;
}

/*void wait50()
{
//        if (!infocus) drawit=1;
        if (tapeon && tapespeed) drawit=1;
        while (!drawit || !infocus)
        {
                sleep(1);
//                if (waiting) return;
        }
        drawit--;
}*/

char exedir[MAX_PATH_FILENAME_BUFFER_SIZE];
char tapename[512];
char parallelname[512];
char serialname[512];
char tube6502name[512];
extern int serial_debug;
char romnames[16][1024];
int rambanks[16];

void initelk(int argc, char *argv[])
{
        int c;
        char *p;
        int tapenext=0,discnext=0,romnext=-2,ramnext=0,parallelnext=0,serialnext=0,serialdebugnext=0;
        int autokeysnext=0,tube6502next=0;
        get_executable_name(exedir,MAX_PATH_FILENAME_BUFFER_SIZE - 1);
        p=get_filename(exedir);
        p[0]=0;
        discname[0]=discname2[0]=tapename[0]=0;
        parallelname[0]=0;serialname[0]=0;
        tube6502name[0]=0;
        for (int i = 0; i < 16; i++) {
            romnames[i][0] = 0;
            rambanks[i] = 0;
        }
//        printf("Load config\n");
        loadconfig();
//printf("commandline\n");

        for (c=1;c<argc;c++)
        {
//printf("%i\n",c); fflush(stdout);
//                printf("%i : %s\n",c,argv[c]);
/*                if (!strcasecmp(argv[c],"-1770"))
                {
                        I8271=0;
                        WD1770=1;
                }
                else*/
#ifndef WIN32
                if (!strcasecmp(argv[c],"--help"))
                {
                        printf("Elkulator v1.0 command line options :\n\n");
                        printf("-disc disc.ssd  - load disc.ssd into drives :0/:2\n");
                        printf("-disc1 disc.ssd - load disc.ssd into drives :1/:3\n");
                        printf("-tape tape.uef  - load tape.uef\n");
                        printf("-parallel file  - use file as a socket for parallel output\n");
                        printf("-serial file    - use file as a socket for serial communications\n");
                        printf("-serialdebug n  - set serial debugging output level to n\n");
                        printf("-rom number rom - load rom into the numbered bank\n");
                        printf("-autokeys list  - scripted delay:keycode pairs, comma separated\n");
                        printf("-tube6502 rom   - enable an AP5 external 3 MHz 65C02 Tube\n");
                        printf("-ram number     - make the numbered bank writable\n");
                        printf("-debug          - start debugger\n");
                        exit(-1);
                }
                else
#endif
                if (!strcasecmp(argv[c],"-tape"))
                {
                        tapenext=2;
                }
                else if (!strcasecmp(argv[c],"-disc") || !strcasecmp(argv[c],"-disk"))
                {
                        discnext=1;
                }
                else if (!strcasecmp(argv[c],"-disc1"))
                {
                        discnext=2;
                }
                else if (!strcasecmp(argv[c],"-rom"))
                {
                        romnext=-1;
                }
                else if (!strcasecmp(argv[c],"-ram"))
                {
                        ramnext=1;
                }
                else if (!strcasecmp(argv[c],"-parallel"))
                {
                        parallelnext=1;
                }
                else if (!strcasecmp(argv[c],"-serial"))
                {
                        serialnext=1;
                }
                else if (!strcasecmp(argv[c],"-serialdebug"))
                {
                        serialdebugnext=1;
                }
                else if (!strcasecmp(argv[c],"-autokeys"))
                {
                        autokeysnext=1;
                }
                else if (!strcasecmp(argv[c],"-tube6502"))
                {
                        tube6502next=1;
                }
                else if (!strcasecmp(argv[c],"-debug"))
                {
                        debug=debugon=1;
                }
                else if (tapenext)
                   strcpy(tapename,argv[c]);
                else if (discnext)
                {
                        if (discnext==2) strcpy(discname2,argv[c]);
                        else             strcpy(discname,argv[c]);
                        discnext=0;
                }
                else if (romnext > -2)
                {
                    if (romnext == -1)
                        romnext = atoi(argv[c]);
                    else if (romnext < 16) {
                        fprintf(stderr, "Loading %s in bank %d\n", argv[c], romnext);
                        strcpy(romnames[romnext],argv[c]);
                        romnext = -2;
                    }
                }
                else if (ramnext)
                {
                        int bank = atoi(argv[c]);
                        if (bank >= 0 && bank < 16) rambanks[bank] = 1;
                        ramnext=0;
                }
                else if (autokeysnext)
                {
                        parse_scripted_keys(argv[c]);
                        autokeysnext=0;
                }
                else if (tube6502next)
                {
                        strcpy(tube6502name,argv[c]);
                        tube6502next=0;
                }
                else if (parallelnext)
                {
                        strcpy(parallelname,argv[c]);
                        parallelnext=0;
                }
                else if (serialnext)
                {
                        strcpy(serialname,argv[c]);
                        serialnext=0;
                }
                else if (serialdebugnext)
                {
                        serial_debug = atoi(argv[c]);
                        serialdebugnext=0;
                }
                if (tapenext) tapenext--;
        }
//printf("initalmain\n"); fflush(stdout);

        initalmain(0,NULL);
//printf("loadroms\n"); fflush(stdout);
        loadroms();
        /* Command-line expansion ROMs and RAM must be visible when
           reset6502() performs the MOS service-ROM scan. */
        for (int i = 0; i < 16; i++) {
            if (romnames[i][0] != 0) loadrom_n(i, romnames[i]);
            if (rambanks[i]) enable_ram_n(i);
        }
        if (tube6502name[0] && !ap5_tube_init(tube6502name)) exit(1);
//printf("reset6502\n");
        reset6502();
        initula();
        resetula();
        reset1770();
        resetparallel();
        resetserial();
        
        loadtape(tapename);
        loaddisc(0,discname);
        loaddisc(1,discname2);
        if (defaultwriteprot) writeprot[0]=writeprot[1]=1;
#ifndef WIN32
        install_keyboard();
#endif
        install_timer();
        install_int_ex(drawitint,MSEC_TO_TIMER(20));
        install_joystick(JOY_TYPE_AUTODETECT);
        inital();
        initsound();
        loaddiscsamps();
        maketapenoise();

        makekeyl();
        
        set_display_switch_mode(SWITCH_BACKGROUND);
        
//        initresid();
//        resetsid();
//        setsidtype(0,0);
}

int ddnoiseframes=0;
int oldbreak=0;
int resetit=0;
int runelkframe=0;
int ramdumpframes=0;

static void dump_cpu_state_to(const char *path)
{
        FILE *state;
        if (!path || !*path) return;
        state=fopen(path,"w");
        if (!state) return;
        fprintf(state,
                "PC=%04X A=%02X X=%02X Y=%02X S=%02X P=%d%d%d%d%d%d ROM=%X EXT=%X CYC=%d\n",
                pc,a,x,y,s,p.c,p.z,p.i,p.d,p.v,p.n,rombank,extrom,cycles);
        fclose(state);
}

void runelk()
{
        int c;
        if (drawit || (tapeon && tapespeed))
        {
                if (drawit) drawit--;
                if (drawit>8 || drawit<0) drawit=0;
                for (c=0;c<312;c++) exec6502();
                if (runelkframe) exec6502();
                runelkframe=!runelkframe;
                if (++ramdumpframes >= 50)
                {
                        const char *ram_dump = getenv("PI1MHZ_RAM_DUMP");
                        ramdumpframes=0;
                        if (ram_dump && *ram_dump)
                                dumpram_to(ram_dump);
                        const char *cpu_dump = getenv("PI1MHZ_CPU_DUMP");
                        if (cpu_dump && *cpu_dump)
                                dump_cpu_state_to(cpu_dump);
                }
                if (resetit)
                {
                        memset(ram,0,32768);
                        ap5_tube_prepare_cold_boot();
                        resetula();
                        resetserial();
                        reset6502();
                        resetit=0;
                }
                if (break_pressed() && !oldbreak)
                {
                        ap5_tube_reset();
                        reset6502();
                }
                oldbreak = break_pressed();
                if (wantloadstate) doloadstate();
                if (wantsavestate) dosavestate();
                if (infocus) poll_joystick();
                if (autoboot) autoboot--;
                if (scripted_key_index < scripted_key_count)
                {
                        int scripted_key = scripted_keys[scripted_key_index];
                        if (scripted_key_delay) {
                                scripted_key_delay--;
                        } else if (scripted_key_frames < 6) {
                                if (scripted_key == 2000) {
                                        key[keylookup[KEY_LSHIFT]] = 1;
                                } else if (scripted_key == 2001) {
                                        key[keylookup[KEY_LSHIFT]] = 0;
                                } else if (scripted_key >= 1000) {
                                        key[scripted_key - 1000] = 1;
                                } else {
                                        key[keylookup[scripted_key]] = 1;
                                }
                                scripted_key_frames++;
                        } else {
                                if (scripted_key != 2000 && scripted_key != 2001) {
                                        if (scripted_key >= 1000)
                                                key[scripted_key - 1000] = 0;
                                        else
                                                key[keylookup[scripted_key]] = 0;
                                }
                                scripted_key_index++;
                                scripted_key_frames = 0;
                                if (scripted_key_index < scripted_key_count)
                                        scripted_key_delay = scripted_delays[scripted_key_index];
                        }
                }
                ddnoiseframes++;
                if (ddnoiseframes>=5)
                {
                        ddnoiseframes=0;
                        mixddnoise();
                }
        }
        else
           rest(1);
}

void closeelk()
{
        const char *ram_dump = getenv("PI1MHZ_RAM_DUMP");
        if (ram_dump && *ram_dump)
                dumpram_to(ram_dump);
        const char *cpu_dump = getenv("PI1MHZ_CPU_DUMP");
        if (cpu_dump && *cpu_dump)
                dump_cpu_state_to(cpu_dump);
        ap5_tube_close();
        stopmovie();
        saveconfig();
//        dumpram();
//        dumpregs();
//        printf("Closing!\n");
}
