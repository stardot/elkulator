/*
 * control.h — Unix socket control interface for Elkulator
 *
 * Exposes a simple byte-stream socket:
 *   - bytes written by the emulator via OSWRCH are forwarded to the client
 *   - bytes sent by the client are queued as virtual keypresses
 *
 * Usage:
 *   elkulator --control /tmp/elkulator.sock
 */

#ifndef CONTROL_H
#define CONTROL_H

/* Open (or skip if path is empty) the control socket server.
   Must be called before the main loop starts. */
void control_init(const char *path);

/* Accept pending connections and drain incoming bytes into the inject queue.
   Call once per emulated frame. */
void control_poll(void);

/* Called from the OSWRCH hook (pc == 0xFFEE) with the character in A.
   Forwards it to any connected client. */
void control_oswrch(unsigned char c);

/* Close the socket and clean up. */
void control_close(void);

/* Returns the next queued inject character and advances the queue,
   or -1 if the queue is empty. */
int control_inject_dequeue(void);

/* True if a virtual key is currently being held (injection in progress). */
int control_inject_active(void);

/* Advance the injection state machine by one frame tick.
   Call once per emulated frame from runelk(). */
void control_inject_tick(void);

/* Retrieve the currently-held virtual key and shift state.
   key_out receives an Allegro KEY_* constant, shift_out is 1 if SHIFT
   should be asserted, 0 otherwise.  Both are set to -1 when inactive. */
void control_inject_current(int *key_out, int *shift_out);

#endif /* CONTROL_H */
