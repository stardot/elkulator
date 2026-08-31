#ifndef PI1MHZ_NET_BACKEND_H
#define PI1MHZ_NET_BACKEND_H

#include <stddef.h>
#include <stdint.h>

typedef struct pi1mhz_net_backend pi1mhz_net_backend;

/* mode is "fixture" for deterministic CI or "live" for host TCP sockets. */
pi1mhz_net_backend *pi1mhz_net_backend_create(const char *mode,
                                               const char *trace_path,
                                               int exit_on_close);
void pi1mhz_net_backend_destroy(pi1mhz_net_backend *backend);
uint8_t pi1mhz_net_backend_dispatch(void *opaque, uint8_t selector,
                                    uint32_t command_pointer,
                                    uint8_t *jim, size_t jim_size);

#endif
