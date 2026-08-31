#ifndef PI1MHZ_FTP_H
#define PI1MHZ_FTP_H

#include <stddef.h>
#include <stdint.h>

typedef struct pi1mhz_ftp pi1mhz_ftp;

pi1mhz_ftp *pi1mhz_ftp_create(int live);
void pi1mhz_ftp_destroy(pi1mhz_ftp *ftp);
uint8_t pi1mhz_ftp_dispatch(pi1mhz_ftp *ftp, uint8_t *command,
                            uint8_t *service_jim, size_t service_size);

#endif
