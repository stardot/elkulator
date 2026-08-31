#ifndef PI1MHZ_WOLFSSH_H
#define PI1MHZ_WOLFSSH_H

#include <stddef.h>
#include <stdint.h>

typedef struct pi1mhz_wolfssh pi1mhz_wolfssh;

pi1mhz_wolfssh *pi1mhz_wolfssh_create(const char *storage_directory);
void pi1mhz_wolfssh_destroy(pi1mhz_wolfssh *provider);
uint8_t pi1mhz_wolfssh_open(pi1mhz_wolfssh *provider, const char *url,
                            const char *username, int trust_unknown,
                            char fingerprint[96]);
int pi1mhz_wolfssh_read(pi1mhz_wolfssh *provider, uint8_t *out,
                        size_t maximum);
int pi1mhz_wolfssh_write(pi1mhz_wolfssh *provider, const uint8_t *data,
                         size_t length);
int pi1mhz_wolfssh_password(pi1mhz_wolfssh *provider,
                            const uint8_t *password, size_t length);
void pi1mhz_wolfssh_close(pi1mhz_wolfssh *provider);

#endif
