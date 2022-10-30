/*Elkulator v1.0 by Sarah Walker
  Linux keyboard redefinition GUI*/

#ifndef WIN32
#include <allegro.h>
#include <string.h>
#include "elk.h"

int keytemp[128];

char *key_names[] =
{
   "",           "A",          "B",          "C",
   "D",          "E",          "F",          "G",
   "H",          "I",          "J",          "K",
   "L",          "M",          "N",          "O",
   "P",          "Q",          "R",          "S",
   "T",          "U",          "V",          "W",
   "X",          "Y",          "Z",          "0",
   "1",          "2",          "3",          "4",
   "5",          "6",          "7",          "8",
   "9",          "0_PAD",      "1_PAD",      "2_PAD",
   "3_PAD",      "4_PAD",      "5_PAD",      "6_PAD",
   "7_PAD",      "8_PAD",      "9_PAD",      "F1",
   "F2",         "F3",         "F4",         "F5",
   "F6",         "F7",         "F8",         "F9",
   "F10",        "F11",        "F12",        "ESC",
   "TILDE",      "MINUS",      "EQUALS",     "BACKSPACE",
   "TAB",        "OPENBRACE",  "CLOSEBRACE", "ENTER",
   "COLON",      "QUOTE",      "BACKSLASH",  "BACKSLASH2",
   "COMMA",      "STOP",       "SLASH",      "SPACE",
   "INSERT",     "DEL",        "HOME",       "END",
   "PGUP",       "PGDN",       "LEFT",       "RIGHT",
   "UP",         "DOWN",       "SLASH_PAD",  "ASTERISK",
   "MINUS_PAD",  "PLUS_PAD",   "DEL_PAD",    "ENTER_PAD",
   "PRTSCR",     "PAUSE",      "ABNT_C1",    "YEN",
   "KANA",       "CONVERT",    "NOCONVERT",  "AT",
   "CIRCUMFLEX", "COLON2",     "KANJI",      "EQUALS_PAD",
   "BACKQUOTE",  "SEMICOLON",  "COMMAND",    "UNKNOWN1",
   "UNKNOWN2",   "UNKNOWN3",   "UNKNOWN4",   "UNKNOWN5",
   "UNKNOWN6",   "UNKNOWN7",   "UNKNOWN8",   "LSHIFT",
   "RSHIFT",     "LCONTROL",   "RCONTROL",   "ALT",
   "ALTGR",      "LWIN",       "RWIN",       "MENU",
   "SCRLOCK",    "NUMLOCK",    "CAPSLOCK",   "MAX"
};

int d_getkey(int msg, DIALOG *d, int cd)
{
        BITMAP *b;
        int x,y;
        int ret = d_button_proc(msg, d, cd);
        int k,k2;
        int c;
        char s[1024],s2[1024],s3[64];
        if (ret==D_EXIT)
        {
                k=(int)d->d2;
                x=(SCREEN_W/2)-100;
                y=(SCREEN_H/2)-36;
                b=create_bitmap(200,72);
                blit(screen,b,x,y,0,0,200,72);
                rectfill(screen,x,y,x+199,y+71,makecol(0,0,0));
                rect(screen,x,y,x+199,y+71,makecol(255,255,255));
                if (d->dp2) textprintf_ex(screen,font,x+8,y+8,makecol(255,255,255),0,"Redefining %s",(char *)d->dp2);
                else        textprintf_ex(screen,font,x+8,y+8,makecol(255,255,255),0,"Redefining %s",(char *)d->dp);
                textprintf_ex(screen,font,x+8,y+24,makecol(255,255,255),0,"Assigned to PC key(s) :");

                s[0]=0;
                for (c=0;c<128;c++)
                {
                        if (keylookup[c]==k)
                        {
                                if (s[0]) sprintf(s3,", %s",key_names[c]);
                                else      sprintf(s3,"%s",key_names[c]);
                                sprintf(s2,"%s%s",s,s3);
                                strcpy(s,s2);
                        }
                }

                textprintf_ex(screen,font,x+8,y+40,makecol(255,255,255),0,s);

                textprintf_ex(screen,font,x+8,y+56,makecol(255,255,255),0,"Please press new key...");
getnewkey:
                while (!keypressed());
                k2=readkey()>>8;
                if (k2==KEY_F11 || k2==KEY_F12) goto getnewkey;
                keylookup[k2]=k;

                blit(b,screen,0,0,x,y,200,72);
                destroy_bitmap(b);
                while (key[KEY_SPACE]);
                while (keypressed()) readkey();
                return 0;
        }
        return ret;
}

/* Keyboard dialogue with emulated key values represented by Allegro keycodes.
   Note that keycodes from actual keypresses are converted to these key values
   via the keylookup table. */

DIALOG bemdefinegui[]=
{
        /* proc,        x,y,w,h,        fg,bg,  key, flags,   d1, emulated key,   label,    alt label, dp3 */

        {d_box_proc,    0,0,538,238,    15,15,  0,   0,       0,  0,              NULL,     NULL,      NULL},

        {d_button_proc, 205,200,60,28,  15,15,  0,   D_CLOSE, 0,  0,              "OK",     NULL,      NULL},
        {d_button_proc, 271,200,60,28,  15,15,  0,   D_CLOSE, 0,  0,              "Cancel", NULL,      NULL},

        {d_getkey,      26,24,28,28,    15,15,  0,   D_EXIT,  0,  KEY_ESC,        "ESC",    NULL,      NULL},
        {d_getkey,      58,24,28,28,    15,15,  0,   D_EXIT,  0,  KEY_1,          "1",      NULL,      NULL},
        {d_getkey,      90,24,28,28,    15,15,  0,   D_EXIT,  0,  KEY_2,          "2",      NULL,      NULL},
        {d_getkey,      122,24,28,28,   15,15,  0,   D_EXIT,  0,  KEY_3,          "3",      NULL,      NULL},
        {d_getkey,      154,24,28,28,   15,15,  0,   D_EXIT,  0,  KEY_4,          "4",      NULL,      NULL},
        {d_getkey,      186,24,28,28,   15,15,  0,   D_EXIT,  0,  KEY_5,          "5",      NULL,      NULL},
        {d_getkey,      218,24,28,28,   15,15,  0,   D_EXIT,  0,  KEY_6,          "6",      NULL,      NULL},
        {d_getkey,      250,24,28,28,   15,15,  0,   D_EXIT,  0,  KEY_7,          "7",      NULL,      NULL},
        {d_getkey,      282,24,28,28,   15,15,  0,   D_EXIT,  0,  KEY_8,          "8",      NULL,      NULL},
        {d_getkey,      314,24,28,28,   15,15,  0,   D_EXIT,  0,  KEY_9,          "9",      NULL,      NULL},
        {d_getkey,      346,24,28,28,   15,15,  0,   D_EXIT,  0,  KEY_0,          "0",      NULL,      NULL},
        {d_getkey,      378,24,28,28,   15,15,  0,   D_EXIT,  0,  KEY_MINUS,      "=",      NULL,      NULL},
        {d_getkey,      410,24,28,28,   15,15,  0,   D_EXIT,  0,  KEY_LEFT,       "LFT",    "LEFT",    NULL},
        {d_getkey,      442,24,28,28,   15,15,  0,   D_EXIT,  0,  KEY_RIGHT,      "RGT",    "RIGHT",   NULL},
        {d_getkey,      474,24,28,28,   15,15,  0,   D_EXIT,  0,  KEY_F12,        "BRK",    "BREAK",   NULL},

        {d_getkey,      42,56,28,28,    15,15,  0,   D_EXIT,  0,  KEY_TAB,        "FN",     NULL,      NULL},
        {d_getkey,      74,56,28,28,    15,15,  0,   D_EXIT,  0,  KEY_Q,          "Q",      NULL,      NULL},
        {d_getkey,      106,56,28,28,   15,15,  0,   D_EXIT,  0,  KEY_W,          "W",      NULL,      NULL},
        {d_getkey,      138,56,28,28,   15,15,  0,   D_EXIT,  0,  KEY_E,          "E",      NULL,      NULL},
        {d_getkey,      170,56,28,28,   15,15,  0,   D_EXIT,  0,  KEY_R,          "R",      NULL,      NULL},
        {d_getkey,      202,56,28,28,   15,15,  0,   D_EXIT,  0,  KEY_T,          "T",      NULL,      NULL},
        {d_getkey,      234,56,28,28,   15,15,  0,   D_EXIT,  0,  KEY_Y,          "Y",      NULL,      NULL},
        {d_getkey,      266,56,28,28,   15,15,  0,   D_EXIT,  0,  KEY_U,          "U",      NULL,      NULL},
        {d_getkey,      298,56,28,28,   15,15,  0,   D_EXIT,  0,  KEY_I,          "I",      NULL,      NULL},
        {d_getkey,      330,56,28,28,   15,15,  0,   D_EXIT,  0,  KEY_O,          "O",      NULL,      NULL},
        {d_getkey,      362,56,28,28,   15,15,  0,   D_EXIT,  0,  KEY_P,          "P",      NULL,      NULL},
        {d_getkey,      394,56,28,28,   15,15,  0,   D_EXIT,  0,  KEY_UP,         "UP",     NULL,      NULL},
        {d_getkey,      426,56,28,28,   15,15,  0,   D_EXIT,  0,  KEY_DOWN,       "DWN",    "DOWN",    NULL},
        {d_getkey,      458,56,28,28,   15,15,  0,   D_EXIT,  0,  KEY_END,        "CPY",    NULL,      NULL},

        {d_getkey,      50,88,28,28,    15,15,  0,   D_EXIT,  0,  KEY_LCONTROL,   "CTL",    "CTRL",    NULL},
        {d_getkey,      82,88,28,28,    15,15,  0,   D_EXIT,  0,  KEY_A,          "A",      NULL,      NULL},
        {d_getkey,      114,88,28,28,   15,15,  0,   D_EXIT,  0,  KEY_S,          "S",      NULL,      NULL},
        {d_getkey,      146,88,28,28,   15,15,  0,   D_EXIT,  0,  KEY_D,          "D",      NULL,      NULL},
        {d_getkey,      178,88,28,28,   15,15,  0,   D_EXIT,  0,  KEY_F,          "F",      NULL,      NULL},
        {d_getkey,      210,88,28,28,   15,15,  0,   D_EXIT,  0,  KEY_G,          "G",      NULL,      NULL},
        {d_getkey,      242,88,28,28,   15,15,  0,   D_EXIT,  0,  KEY_H,          "H",      NULL,      NULL},
        {d_getkey,      274,88,28,28,   15,15,  0,   D_EXIT,  0,  KEY_J,          "J",      NULL,      NULL},
        {d_getkey,      306,88,28,28,   15,15,  0,   D_EXIT,  0,  KEY_K,          "K",      NULL,      NULL},
        {d_getkey,      338,88,28,28,   15,15,  0,   D_EXIT,  0,  KEY_L,          "L",      NULL,      NULL},
        {d_getkey,      370,88,28,28,   15,15,  0,   D_EXIT,  0,  KEY_SEMICOLON,  ";",      NULL,      NULL},
        {d_getkey,      402,88,28,28,   15,15,  0,   D_EXIT,  0,  KEY_QUOTE,      ":",      NULL,      NULL},
        {d_getkey,      434,88,44,28,   15,15,  0,   D_EXIT,  0,  KEY_ENTER,      "RET",    "RETURN",  NULL},

        {d_getkey,      50,120,44,28,   15,15,  0,   D_EXIT,  0,  KEY_LSHIFT,     "SHIFT",  NULL,      NULL},
        {d_getkey,      98,120,28,28,   15,15,  0,   D_EXIT,  0,  KEY_Z,          "Z",      NULL,      NULL},
        {d_getkey,      130,120,28,28,  15,15,  0,   D_EXIT,  0,  KEY_X,          "X",      NULL,      NULL},
        {d_getkey,      162,120,28,28,  15,15,  0,   D_EXIT,  0,  KEY_C,          "C",      NULL,      NULL},
        {d_getkey,      194,120,28,28,  15,15,  0,   D_EXIT,  0,  KEY_V,          "V",      NULL,      NULL},
        {d_getkey,      226,120,28,28,  15,15,  0,   D_EXIT,  0,  KEY_B,          "B",      NULL,      NULL},
        {d_getkey,      258,120,28,28,  15,15,  0,   D_EXIT,  0,  KEY_N,          "N",      NULL,      NULL},
        {d_getkey,      290,120,28,28,  15,15,  0,   D_EXIT,  0,  KEY_M,          "M",      NULL,      NULL},
        {d_getkey,      322,120,28,28,  15,15,  0,   D_EXIT,  0,  KEY_COMMA,      ",",      NULL,      NULL},
        {d_getkey,      354,120,28,28,  15,15,  0,   D_EXIT,  0,  KEY_STOP,       ".",      NULL,      NULL},
        {d_getkey,      386,120,28,28,  15,15,  0,   D_EXIT,  0,  KEY_SLASH,      "/",      NULL,      NULL},
        {d_getkey,      418,120,44,28,  15,15,  0,   D_EXIT,  0,  KEY_RSHIFT,     "SHIFT",  NULL,      NULL},
        {d_getkey,      466,120,28,28,  15,15,  0,   D_EXIT,  0,  KEY_DEL,        "DEL",    "DELETE",  NULL},

        {d_getkey,      146,152,252,28, 15,15,  0,   D_EXIT,  0,  KEY_SPACE,      "SPACE",  NULL,      NULL},

        {d_yield_proc},
        {0,             0,0,0,0,        0,0,    0,   0,       0,  0,              NULL,     NULL,      NULL}
};


int gui_keydefine()
{
        DIALOG_PLAYER *dp;
        BITMAP *b;
        DIALOG *d=bemdefinegui;
        int x=0,y;
        while (d[x].proc)
        {
                d[x].x+=(SCREEN_W/2)-(d[0].w/2);
                d[x].y+=(SCREEN_H/2)-(d[0].h/2);
                d[x].fg=0xFFFFFF;
                x++;
        }
        for (x=0;x<128;x++) keytemp[x]=keylookup[x];
        x=(SCREEN_W/2)-(d[0].w/2);
        y=(SCREEN_H/2)-(d[0].h/2);
        b=create_bitmap(d[0].w,d[0].h);
        blit(screen,b,x,y,0,0,d[0].w,d[0].h);
        dp=init_dialog(d,0);
        while (x && !key[KEY_F11] && !(mouse_b&2) && !key[KEY_ESC])
        {
                x=update_dialog(dp);
        }
        shutdown_dialog(dp);
        if (x==1)
        {
                for (x=0;x<128;x++) keylookup[x]=keytemp[x];
        }
        x=(SCREEN_W/2)-(d[0].w/2);
        y=(SCREEN_H/2)-(d[0].h/2);
        blit(b,screen,0,0,x,y,d[0].w,d[0].h);
        x=0;
        while (d[x].proc)
        {
                d[x].x -= (SCREEN_W / 2) - (d[0].w / 2);
                d[x].y -= (SCREEN_H / 2) - (d[0].h / 2);
                x++;
        }
        return D_O_K;
}
#endif
