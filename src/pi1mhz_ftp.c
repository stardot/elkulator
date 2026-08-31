#include "pi1mhz_ftp.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define FTP_OPEN 114u
#define FTP_EXEC 115u
#define FTP_READ 116u
#define FTP_WRITE 117u
#define FTP_CLOSE 118u
#define FTP_CANCEL 119u
#define FTP_OK 0u
#define FTP_EOF 0x20u
#define FTP_PARAM 0x23u
#define FTP_DNS 0x24u
#define FTP_CONNECT 0x25u
#define FTP_TIMEOUT 0x2bu
#define FTP_ABORT 0x2du
#define FTP_SCRATCH 0xfff100u
#define FTP_TEXT 237u

struct pi1mhz_ftp {
    int live;
    int control;
    int data;
    unsigned transfer;
    char host[192];
    char reply[FTP_TEXT + 1u];
    uint8_t fixture_file[4096];
    size_t fixture_size;
    size_t fixture_pos;
};

static void close_fd(int *fd)
{
    if (*fd >= 0) close(*fd);
    *fd = -1;
}

static void close_session(pi1mhz_ftp *ftp)
{
    close_fd(&ftp->data);
    close_fd(&ftp->control);
    ftp->transfer = 0u;
}

static int socket_connect(const char *host, uint16_t port)
{
    struct addrinfo hints = {0}, *addresses = NULL, *it;
    char service[8];
    int fd = -1;
    struct timeval timeout = {30, 0};
    snprintf(service, sizeof service, "%u", (unsigned)port);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, service, &hints, &addresses)) return -2;
    for (it = addresses; it != NULL; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;
        (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
        if (!connect(fd, it->ai_addr, it->ai_addrlen)) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    return fd;
}

static int send_all(int fd, const void *data, size_t length)
{
    const uint8_t *p = data;
    while (length) {
        ssize_t sent = send(fd, p, length, 0);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return -1;
        p += (size_t)sent;
        length -= (size_t)sent;
    }
    return 0;
}

static int read_reply(pi1mhz_ftp *ftp)
{
    char line[256];
    size_t used = 0u, total = 0u;
    int multiline = 0;
    ftp->reply[0] = 0;
    for (;;) {
        char c;
        ssize_t got = recv(ftp->control, &c, 1u, 0);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return -1;
        if (total < FTP_TEXT) ftp->reply[total++] = c;
        if (used + 1u < sizeof line) line[used++] = c;
        if (c != '\n') continue;
        line[used] = 0;
        if (used >= 4u && isdigit((unsigned char)line[0]) &&
            isdigit((unsigned char)line[1]) && isdigit((unsigned char)line[2])) {
            int code = (line[0]-'0')*100 + (line[1]-'0')*10 + line[2]-'0';
            if (!multiline && line[3] == '-') multiline = code;
            else if (line[3] == ' ' && (!multiline || code == multiline)) {
                ftp->reply[total] = 0;
                return code;
            }
        }
        used = 0u;
    }
}

static int control_command(pi1mhz_ftp *ftp, const char *text)
{
    if (send_all(ftp->control, text, strlen(text))) return -1;
    return read_reply(ftp);
}

static int parse_target(const char *url, char *host, size_t capacity,
                        uint16_t *port)
{
    const char *p = url, *end, *colon = NULL;
    size_t length;
    unsigned long value;
    char *tail;
    if (!strncasecmp(p, "ftp://", 6u)) p += 6;
    end = p;
    while (*end && *end != '/') {
        if (*end == ':') colon = end;
        if ((unsigned char)*end < 0x21u || *end == '@') return -1;
        end++;
    }
    *port = 21u;
    if (colon) {
        errno = 0;
        value = strtoul(colon + 1, &tail, 10);
        if (errno || tail != end || value == 0u || value > 65535u) return -1;
        *port = (uint16_t)value;
        end = colon;
    }
    length = (size_t)(end - p);
    if (!length || length >= capacity) return -1;
    memcpy(host, p, length);
    host[length] = 0;
    return 0;
}

static int passive_port(const char *reply, uint16_t *port)
{
    const char *p = strchr(reply, '(');
    unsigned values[6];
    if (!p) return -1;
    p++;
    if (strstr(reply, "229 ")) {
        char delimiter = *p++;
        if (*p++ != delimiter || *p++ != delimiter) return -1;
        values[0] = 0u;
        while (isdigit((unsigned char)*p)) {
            values[0] = values[0] * 10u + (unsigned)(*p++ - '0');
            if (values[0] > 65535u) return -1;
        }
        if (*p != delimiter || !values[0]) return -1;
        *port = (uint16_t)values[0];
        return 0;
    }
    for (unsigned i = 0; i < 6u; i++) {
        values[i] = 0u;
        if (!isdigit((unsigned char)*p)) return -1;
        while (isdigit((unsigned char)*p)) {
            values[i] = values[i] * 10u + (unsigned)(*p++ - '0');
            if (values[i] > 255u) return -1;
        }
        if (i != 5u && *p++ != ',') return -1;
    }
    *port = (uint16_t)(values[4] * 256u + values[5]);
    return *port ? 0 : -1;
}

static int begin_transfer(pi1mhz_ftp *ftp, const char *command,
                          unsigned transfer)
{
    uint16_t port;
    int code = control_command(ftp, "EPSV\r\n");
    if (code != 229 || passive_port(ftp->reply, &port)) {
        code = control_command(ftp, "PASV\r\n");
        if (code != 227 || passive_port(ftp->reply, &port)) return -1;
    }
    ftp->data = socket_connect(ftp->host, port);
    if (ftp->data < 0) return -1;
    code = control_command(ftp, command);
    if (code != 125 && code != 150) {
        close_fd(&ftp->data);
        ftp->transfer = 0u;
        return code > 0 ? 1 : -1;
    }
    ftp->transfer = transfer;
    return 0;
}

static int make_command(const char *input, char *output, size_t capacity,
                        unsigned *transfer)
{
    const char *argument = strchr(input, ' '), *verb = input;
    size_t word = argument ? (size_t)(argument-input) : strlen(input);
    *transfer = 0u;
    if (word == 3u && !strncasecmp(input, "GET", 3u)) {
        verb = "RETR"; *transfer = 1u;
    } else if (word == 3u && !strncasecmp(input, "PUT", 3u)) {
        verb = "STOR"; *transfer = 2u;
    } else if (word == 3u && !strncasecmp(input, "DIR", 3u)) {
        verb = "LIST"; *transfer = 3u;
    } else if (word == 2u && !strncasecmp(input, "LS", 2u)) {
        verb = "NLST"; *transfer = 3u;
    } else if (word == 2u && !strncasecmp(input, "CD", 2u)) verb = "CWD";
    else if (word == 6u && !strncasecmp(input, "DELETE", 6u)) verb = "DELE";
    else if (word == 5u && !strncasecmp(input, "MKDIR", 5u)) verb = "MKD";
    else if (word == 5u && !strncasecmp(input, "RMDIR", 5u)) verb = "RMD";
    else if (word == 6u && !strncasecmp(input, "BINARY", 6u)) {
        verb = "TYPE"; argument = " I";
    } else if (word == 5u && !strncasecmp(input, "ASCII", 5u)) {
        verb = "TYPE"; argument = " A";
    }
    if ((*transfer == 1u || *transfer == 2u) && (!argument || !argument[1]))
        return -1;
    if (verb == input) return snprintf(output, capacity, "%s\r\n", input) >= (int)capacity ? -1 : 0;
    return snprintf(output, capacity, "%s%s\r\n", verb, argument ? argument : "") >= (int)capacity ? -1 : 0;
}

static void publish(pi1mhz_ftp *ftp, uint8_t *command)
{
    size_t length = strnlen(ftp->reply, FTP_TEXT);
    command[1] = (uint8_t)ftp->transfer;
    memcpy(command + 2, ftp->reply, length);
    command[2 + length] = 0;
}

static uint8_t fixture_dispatch(pi1mhz_ftp *ftp, uint8_t *command,
                                uint8_t *scratch)
{
    static const uint8_t default_file[] = "Pi1MHz FTP fixture\r\n";
    switch (command[0]) {
    case FTP_OPEN:
        strcpy(ftp->reply, "220 Pi1MHz FTP fixture ready\r\n");
        publish(ftp, command); return FTP_OK;
    case FTP_EXEC: {
        char wire[256]; unsigned transfer;
        if (make_command((char *)command + 1, wire, sizeof wire, &transfer)) return FTP_PARAM;
        ftp->transfer = transfer;
        if (transfer) {
            ftp->fixture_pos = 0u;
            if (transfer == 1u && !ftp->fixture_size) {
                memcpy(ftp->fixture_file, default_file, sizeof default_file - 1u);
                ftp->fixture_size = sizeof default_file - 1u;
            }
            if (transfer == 3u) {
                static const char listing[] = "fixture  21  fixture.txt\r\n";
                memcpy(ftp->fixture_file, listing, sizeof listing - 1u);
                ftp->fixture_size = sizeof listing - 1u;
            }
            if (transfer == 2u) ftp->fixture_size = 0u;
            strcpy(ftp->reply, "150 Opening data connection\r\n");
        } else if (!strncasecmp(wire, "USER ", 5u)) strcpy(ftp->reply, "331 Password required\r\n");
        else if (!strncasecmp(wire, "PASS ", 5u)) strcpy(ftp->reply, "230 Logged in\r\n");
        else if (!strncasecmp(wire, "PWD", 3u)) strcpy(ftp->reply, "257 \"/\"\r\n");
        else strcpy(ftp->reply, "200 OK\r\n");
        publish(ftp, command); return FTP_OK;
    }
    case FTP_READ: {
        size_t count = command[1];
        size_t remain = ftp->fixture_size - ftp->fixture_pos;
        if (count > remain) count = remain;
        memcpy(scratch, ftp->fixture_file + ftp->fixture_pos, count);
        ftp->fixture_pos += count;
        command[1] = (uint8_t)count;
        if (count) return FTP_OK;
        ftp->transfer = 0u; strcpy(ftp->reply, "226 Transfer complete\r\n");
        publish(ftp, command); return FTP_EOF;
    }
    case FTP_WRITE: {
        size_t count = command[1];
        if (!count) {
            ftp->transfer = 0u; strcpy(ftp->reply, "226 Transfer complete\r\n");
            publish(ftp, command); return FTP_EOF;
        }
        if (ftp->fixture_size + count > sizeof ftp->fixture_file) return FTP_PARAM;
        memcpy(ftp->fixture_file + ftp->fixture_size, scratch, count);
        ftp->fixture_size += count; return FTP_OK;
    }
    case FTP_CLOSE:
        close_session(ftp); strcpy(ftp->reply, "221 Closed\r\n");
        publish(ftp, command); return FTP_OK;
    case FTP_CANCEL:
        close_session(ftp); return FTP_ABORT;
    default: return FTP_PARAM;
    }
}

pi1mhz_ftp *pi1mhz_ftp_create(int live)
{
    pi1mhz_ftp *ftp = calloc(1u, sizeof *ftp);
    if (ftp) { ftp->live = live; ftp->control = -1; ftp->data = -1; }
    return ftp;
}

void pi1mhz_ftp_destroy(pi1mhz_ftp *ftp)
{
    if (!ftp) return;
    close_session(ftp);
    free(ftp);
}

uint8_t pi1mhz_ftp_dispatch(pi1mhz_ftp *ftp, uint8_t *command,
                            uint8_t *service_jim, size_t service_size)
{
    uint8_t *scratch;
    if (!ftp || !command || !service_jim || service_size <= FTP_SCRATCH + 240u)
        return FTP_PARAM;
    scratch = service_jim + FTP_SCRATCH;
    if (!ftp->live) return fixture_dispatch(ftp, command, scratch);
    switch (command[0]) {
    case FTP_OPEN: {
        uint16_t port; int code;
        close_session(ftp);
        if (parse_target((char *)command + 1, ftp->host, sizeof ftp->host, &port)) return FTP_PARAM;
        ftp->control = socket_connect(ftp->host, port);
        if (ftp->control == -2) return FTP_DNS;
        if (ftp->control < 0) return FTP_CONNECT;
        code = read_reply(ftp);
        if (code != 220) { close_session(ftp); return FTP_CONNECT; }
        publish(ftp, command); return FTP_OK;
    }
    case FTP_EXEC: {
        char wire[256]; unsigned transfer; int code;
        if (ftp->control < 0 || make_command((char *)command + 1, wire, sizeof wire, &transfer)) return FTP_PARAM;
        if (transfer) {
            code = begin_transfer(ftp, wire, transfer);
            if (code < 0) return FTP_CONNECT;
            publish(ftp, command); return FTP_OK;
        }
        code = control_command(ftp, wire);
        if (code < 0) return errno == EAGAIN || errno == EWOULDBLOCK ? FTP_TIMEOUT : FTP_CONNECT;
        publish(ftp, command);
        return FTP_OK;
    }
    case FTP_READ: {
        ssize_t count;
        if (ftp->data < 0 || ftp->transfer == 2u) return FTP_PARAM;
        do { count = recv(ftp->data, scratch, command[1], 0); } while (count < 0 && errno == EINTR);
        if (count < 0) return errno == EAGAIN || errno == EWOULDBLOCK ? FTP_TIMEOUT : FTP_CONNECT;
        command[1] = (uint8_t)count;
        if (count) return FTP_OK;
        close_fd(&ftp->data); ftp->transfer = 0u;
        if (read_reply(ftp) < 0) return FTP_CONNECT;
        publish(ftp, command); return FTP_EOF;
    }
    case FTP_WRITE: {
        size_t count = command[1];
        if (ftp->data < 0 || ftp->transfer != 2u) return FTP_PARAM;
        if (count) return send_all(ftp->data, scratch, count) ? FTP_CONNECT : FTP_OK;
        if (shutdown(ftp->data, SHUT_WR)) return FTP_CONNECT;
        close_fd(&ftp->data); ftp->transfer = 0u;
        if (read_reply(ftp) < 0) return FTP_CONNECT;
        publish(ftp, command); return FTP_EOF;
    }
    case FTP_CLOSE:
        if (ftp->control >= 0) (void)control_command(ftp, "QUIT\r\n");
        close_session(ftp); strcpy(ftp->reply, "221 Closed\r\n");
        publish(ftp, command); return FTP_OK;
    case FTP_CANCEL:
        close_session(ftp); return FTP_ABORT;
    default: return FTP_PARAM;
    }
}
