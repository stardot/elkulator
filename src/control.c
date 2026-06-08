/*
 * control.c — Unix socket control interface for Elkulator
 *
 * Copyright (C) 2024 Elkulator contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Protocol
 * --------
 * The socket presents as a raw byte-stream virtual terminal:
 *   - Every character the Electron writes to the screen (via OSWRCH) is
 *     sent to the client verbatim.
 *   - Every byte the client sends is treated as an ASCII keypress and
 *     injected into the Electron's keyboard matrix.
 *
 * Supported inject characters
 * ---------------------------
 *   a-z, A-Z, 0-9, space, return (\r or \n), escape (\x1b), delete (\x7f)
 *   ! " # $ % & ' ( )   via SHIFT+1..9
 *   - = * / . , ; : @
 *   < > + ?              via SHIFT combos
 *
 * Timing
 * ------
 * Each injected key is held for INJECT_HOLD_FRAMES emulated frames then
 * released for INJECT_GAP_FRAMES before the next key is sent.  At 50 fps
 * this gives ~60 ms hold / 20 ms gap — comfortably within the OS scan rate.
 */

#include <allegro.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "control.h"

#define INJECT_HOLD_FRAMES  3
#define INJECT_GAP_FRAMES   1
#define INJECT_QUEUE_SIZE   4096

/* ------------------------------------------------------------------ */
/* ASCII → (Allegro KEY_*, needs_shift) lookup table                   */
/* ------------------------------------------------------------------ */

typedef struct { int key; int shift; } KeyMap;

/* Index is ASCII code 0-127; key==-1 means unsupported.
   Unspecified entries are zero-initialised ({0,0}); KEY_A==1 so key==0
   is treated as unsupported by control_inject_tick(). We use -1 explicitly
   for the entries that matter. */
static KeyMap ascii_to_key[128];

static void build_keymap(void)
{
    int i;
    for (i = 0; i < 128; i++) { ascii_to_key[i].key = -1; ascii_to_key[i].shift = 0; }

    /* Control */
    ascii_to_key['\n']  = (KeyMap){KEY_ENTER, 0};
    ascii_to_key['\r']  = (KeyMap){KEY_ENTER, 0};
    ascii_to_key['\x1b']= (KeyMap){KEY_ESC,   0};
    ascii_to_key['\x7f']= (KeyMap){KEY_DEL,   0};

    /* Space */
    ascii_to_key[' '] = (KeyMap){KEY_SPACE, 0};

    /* Digits */
    ascii_to_key['0'] = (KeyMap){KEY_0, 0};
    ascii_to_key['1'] = (KeyMap){KEY_1, 0};
    ascii_to_key['2'] = (KeyMap){KEY_2, 0};
    ascii_to_key['3'] = (KeyMap){KEY_3, 0};
    ascii_to_key['4'] = (KeyMap){KEY_4, 0};
    ascii_to_key['5'] = (KeyMap){KEY_5, 0};
    ascii_to_key['6'] = (KeyMap){KEY_6, 0};
    ascii_to_key['7'] = (KeyMap){KEY_7, 0};
    ascii_to_key['8'] = (KeyMap){KEY_8, 0};
    ascii_to_key['9'] = (KeyMap){KEY_9, 0};

    /* SHIFT+digit symbols */
    ascii_to_key['!'] = (KeyMap){KEY_1, 1};
    ascii_to_key['"'] = (KeyMap){KEY_2, 1};
    ascii_to_key['#'] = (KeyMap){KEY_3, 1};
    ascii_to_key['$'] = (KeyMap){KEY_4, 1};
    ascii_to_key['%'] = (KeyMap){KEY_5, 1};
    ascii_to_key['&'] = (KeyMap){KEY_6, 1};
    ascii_to_key['\'']= (KeyMap){KEY_7, 1};
    ascii_to_key['('] = (KeyMap){KEY_8, 1};
    ascii_to_key[')'] = (KeyMap){KEY_9, 1};

    /* Punctuation (Electron UK layout) */
    ascii_to_key['-'] = (KeyMap){KEY_MINUS,     0};
    ascii_to_key['='] = (KeyMap){KEY_MINUS,     1}; /* SHIFT+- */
    ascii_to_key[','] = (KeyMap){KEY_COMMA,     0};
    ascii_to_key['<'] = (KeyMap){KEY_COMMA,     1};
    ascii_to_key['.'] = (KeyMap){KEY_STOP,      0};
    ascii_to_key['>'] = (KeyMap){KEY_STOP,      1};
    ascii_to_key['/'] = (KeyMap){KEY_SLASH,     0};
    ascii_to_key['?'] = (KeyMap){KEY_SLASH,     1};
    /* On the Electron keyboard:
       KEY_QUOTE = ':*' key  (gives ':' unshifted, '*' shifted)
       KEY_COLON = ';+' key  (gives ';' unshifted, '+' shifted) */
    ascii_to_key[':'] = (KeyMap){KEY_QUOTE,  0};
    ascii_to_key['*'] = (KeyMap){KEY_QUOTE,  1}; /* SHIFT+: */
    ascii_to_key[';'] = (KeyMap){KEY_COLON,  0};
    ascii_to_key['+'] = (KeyMap){KEY_COLON,  1}; /* SHIFT+; */
    ascii_to_key['^'] = (KeyMap){KEY_6,         1}; /* caret / up-arrow */

    /* The Electron starts with CAPS LOCK ON (the default for BASIC use).
       CAPS LOCK ON: unshifted letter = uppercase, SHIFT+letter = lowercase.
       So to inject 'A' (uppercase) press the key without SHIFT;
       to inject 'a' (lowercase) press the key with SHIFT. */
    for (i = 0; i < 26; i++)
        ascii_to_key['A' + i] = (KeyMap){KEY_A + i, 0};   /* uppercase: no shift */

    for (i = 0; i < 26; i++)
        ascii_to_key['a' + i] = (KeyMap){KEY_A + i, 1};   /* lowercase: SHIFT */
}

/* ------------------------------------------------------------------ */
/* State                                                                */
/* ------------------------------------------------------------------ */

static int  srv_fd   = -1;  /* listening socket */
static int  cli_fd   = -1;  /* connected client */
static char sock_path[256];

/* Inject queue: ring buffer of raw ASCII chars */
static unsigned char inject_queue[INJECT_QUEUE_SIZE];
static int           inject_head = 0;   /* next read position */
static int           inject_tail = 0;   /* next write position */

/* Current inject state machine */
static int inject_key   = -1;  /* Allegro KEY_* being held, or -1 */
static int inject_shift =  0;
static int inject_timer =  0;  /* frames remaining in current phase */
static int inject_phase =  0;  /* 0 = holding, 1 = gap */

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int queue_size(void)
{
    return (inject_tail - inject_head + INJECT_QUEUE_SIZE) % INJECT_QUEUE_SIZE;
}

static void queue_push(unsigned char c)
{
    int next = (inject_tail + 1) % INJECT_QUEUE_SIZE;
    if (next != inject_head) {  /* drop if full */
        inject_queue[inject_tail] = c;
        inject_tail = next;
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void control_init(const char *path)
{
    build_keymap();

    if (!path || !path[0])
        return;

    strncpy(sock_path, path, sizeof(sock_path) - 1);
    unlink(sock_path);  /* remove stale socket */

    srv_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv_fd < 0) {
        perror("control: socket");
        return;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (bind(srv_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("control: bind");
        close(srv_fd);
        srv_fd = -1;
        return;
    }

    listen(srv_fd, 1);
    set_nonblocking(srv_fd);
    fprintf(stderr, "control: listening on %s\n", sock_path);
}

void control_poll(void)
{
    if (srv_fd < 0)
        return;

    /* Accept a new connection if we don't have one */
    if (cli_fd < 0) {
        cli_fd = accept(srv_fd, NULL, NULL);
        if (cli_fd >= 0) {
            set_nonblocking(cli_fd);
            fprintf(stderr, "control: client connected\n");
        }
    }

    if (cli_fd < 0)
        return;

    /* Drain incoming bytes into the inject queue */
    unsigned char buf[64];
    ssize_t n;
    while ((n = read(cli_fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++)
            queue_push(buf[i]);
    }

    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        /* Client disconnected */
        close(cli_fd);
        cli_fd = -1;
        fprintf(stderr, "control: client disconnected\n");
    }
}

void control_oswrch(unsigned char c)
{
    if (cli_fd < 0)
        return;
    /* Best-effort non-blocking write; if the client is slow we drop chars
       rather than stalling the emulator. */
    write(cli_fd, &c, 1);
}

void control_close(void)
{
    if (cli_fd >= 0) { close(cli_fd); cli_fd = -1; }
    if (srv_fd >= 0) { close(srv_fd); srv_fd = -1; }
    if (sock_path[0]) { unlink(sock_path); sock_path[0] = 0; }
}

int control_inject_dequeue(void)
{
    if (inject_head == inject_tail)
        return -1;
    unsigned char c = inject_queue[inject_head];
    inject_head = (inject_head + 1) % INJECT_QUEUE_SIZE;
    return c;
}

int control_inject_active(void)
{
    return inject_key >= 0;
}

void control_inject_tick(void)
{
    if (inject_timer > 0) {
        inject_timer--;
        return;
    }

    if (inject_phase == 0 && inject_key >= 0) {
        /* End of hold — release the key */
        inject_key   = -1;
        inject_shift =  0;
        inject_phase =  1;
        inject_timer =  INJECT_GAP_FRAMES;
        return;
    }

    /* Gap expired (or no key was held) — load the next char */
    inject_phase = 0;
    int ch = control_inject_dequeue();
    if (ch < 0 || ch >= 128)
        return;

    const KeyMap *km = &ascii_to_key[ch];
    if (km->key < 0)
        return;     /* unsupported character — skip */

    inject_key   = km->key;
    inject_shift = km->shift;
    inject_timer = INJECT_HOLD_FRAMES;
}

void control_inject_current(int *key_out, int *shift_out)
{
    *key_out   = inject_key;
    *shift_out = inject_shift;
}
