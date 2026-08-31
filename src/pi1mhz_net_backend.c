#include "pi1mhz_net_backend.h"
#include "pi1mhz_mailbox.h"
#include "pi1mhz_ftp.h"
#ifdef PI1MHZ_WOLFSSH
#include "pi1mhz_wolfssh.h"
#endif

#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

#define NET_CMD_URL_OPEN  60u
#define NET_CMD_URL_READ  61u
#define NET_CMD_URL_WRITE 62u
#define NET_CMD_URL_CLOSE 63u
#define NET_CMD_OPEN      45u
#define NET_CMD_DNS       46u
#define NET_CMD_CONNECT   47u
#define NET_CMD_SEND      50u
#define NET_CMD_RECV      51u
#define NET_CMD_CLOSE     53u
#define NET_CMD_COPY_PUBLIC 58u
#define ELKWIFI_CMD_STATUS       80u
#define ELKWIFI_CMD_SCAN         81u
#define ELKWIFI_CMD_JOIN         82u
#define ELKWIFI_CMD_IFCFG        83u
#define ELKWIFI_CMD_LAPOPT       87u
#define ELKWIFI_CMD_PING         88u
#define ELKWIFI_CMD_DATETIME     89u
#define ELKWIFI_CMD_CANCEL       90u
#define ELKWIFI_CMD_RADIO        91u
#define ELKWIFI_CMD_ONLINE       92u
#define ELKWIFI_CMD_UEF_NORMALIZE 93u
/* The host's filing-vector guard, stamped into every JIM page so it is
 * reachable whatever the page selector holds and unreachable by a cassette
 * loader. The host sends the assembled image; the Pi only replicates it. */
#define ELKWIFI_CMD_GUARD_IMAGE  86u
#define UEF_STREAM_VERSION 1u
#define UEF_OP_PROBE 0u
#define UEF_OP_BEGIN 1u
#define UEF_OP_APPEND 2u
#define UEF_OP_FINALIZE 3u
#define UEF_OP_REWIND 4u
#define UEF_OP_REFILL 5u
#define UEF_OP_CLOSE 6u
#define UEF_OP_REPUBLISH 7u
#define SEC_CMD_CAPS       94u
#define SEC_CMD_RANDOM     95u
#define SEC_CMD_SSH_OPEN   96u
#define SEC_CMD_SSH_READ   97u
#define SEC_CMD_SSH_WRITE  98u
#define SEC_CMD_SSH_CLOSE  99u
#define SEC_CMD_SSH_PASSWORD 100u
#define SEC_CMD_SFTP_OPEN 101u
#define SEC_CMD_SFTP_PWD 102u
#define SEC_CMD_SFTP_CD 103u
#define SEC_CMD_SFTP_LS 104u
#define SEC_CMD_SFTP_DELETE 105u
#define SEC_CMD_SFTP_MKDIR 106u
#define SEC_CMD_SFTP_RMDIR 107u
#define SEC_CMD_SFTP_GET_OPEN 108u
#define SEC_CMD_SFTP_GET_READ 109u
#define SEC_CMD_SFTP_PUT_OPEN 110u
#define SEC_CMD_SFTP_PUT_WRITE 111u
#define SEC_CMD_SFTP_TRANSFER_CLOSE 112u
#define SEC_CMD_SFTP_CLOSE 113u
#define NET_ERR_INUSE     0x21u
#define NET_ERR_NOTOPEN   0x22u
#define NET_ERR_PARAM     0x23u
#define NET_ERR_DNS       0x24u
#define NET_ERR_CONN      0x25u
#define NET_ERR_TCP_CLOSED 0x2bu
#define ELKWIFI_ERR_PARAM 0x40u
#define ELKWIFI_ERR_NO_WIFI 0x44u
#define ELKWIFI_ERR_IO 0x45u
#define NET_MAX_HANDLES   8u
#define ELKWIFI_TEXT_MAX  220u
#define UEF_BASE       0u
#define UEF_CAPACITY   0x00fffeu
/* Every JIM page carries the vector trampoline at offset &A0, so the published
 * stream stops there: 160 usable bytes across 256 pages. &FDF0-&FDFF is left
 * alone because the stream length trailer lives in the last two bytes of page
 * &FF. Titles below 40 KB see no extra refills; larger ones pay one more
 * mailbox round trip per 40 KB, which is the price of vectors a cassette
 * loader cannot reach. Uploads are host writes into a contiguous window and
 * are unaffected, so they keep their own larger bound. */
/* JIM page 0 belongs to the host's service reply buffer, which ElkChat and
 * other OSWORD &65 clients read as up to 241 contiguous bytes. Publishing
 * the stream from page 1 keeps the two apart, so a reply arriving while a
 * UEF is being read can no longer overwrite bytes WiCFS has not reached:
 * an "ERR" reply used to be read back as chunk type &5245. */
#define UEF_FIRST_PAGE 1u
#define UEF_WINDOW_SIZE 0x00ff00u
/* The flat window reserves page 0 for the service reply buffer, which the
 * OSWORD &65 clients read in full, and the last page for the length trailer
 * at UEF_CAPACITY. */
#define UEF_FLAT_WINDOW ((256u - UEF_FIRST_PAGE - 1u) * 256u)
/* A published guard occupies the top of every page, so the stream gives those
 * bytes up and is laid out in short runs instead. The guard ends exactly at
 * the page top, which is what leaves the length trailer intact. */
#define UEF_GUARD_OFFSET 0x97u
#define UEF_GUARD_LENGTH (256u - UEF_GUARD_OFFSET)
#define UEF_GUARD_WINDOW ((256u - UEF_FIRST_PAGE - 1u) * UEF_GUARD_OFFSET)
#define UEF_UPLOAD_MAX  UEF_WINDOW_SIZE
#define UEF_STREAM_CAPACITY 0x01000000u
#define FAT_CMD_READ_SECTORS  0u
#define FAT_CMD_WRITE_SECTORS 1u
#define FAT_SECTOR_SIZE       512u
#define FAT_RES_ERROR         1u
#define FAT_RES_WRITE_PROTECT 2u
#define FAT_RES_NOT_READY     3u
#define FAT_RES_PARAMETER     4u

typedef struct net_handle {
    int fd;
    int opening;
    int opened;
    unsigned open_polls;
    unsigned read_index;
    char url[224];
    const uint8_t *fixture_data;
    size_t fixture_size;
    size_t fixture_pos;
    uint8_t fixture_storage[1024];
    int http;
    int raw_allocated;
    int raw_type;
    char request[512];
    size_t request_length;
    size_t request_sent;
    uint8_t response[8192];
    size_t response_size;
    size_t response_pos;
    int headers_done;
    int http_has_length;
    size_t http_content_length;
    size_t http_body_read;
} net_handle;

struct pi1mhz_net_backend {
    int live;
    int exit_on_close;
    int uef_trim_tail;
    uint8_t *uef_stream;
    uint8_t *uef_scratch;
    size_t uef_stream_length;
    size_t uef_stream_cursor;
    size_t uef_published_offset;
    uint16_t uef_published_length;
    uint32_t uef_stream_token;
    uint32_t uef_window_generation;
    uint32_t uef_last_append_sequence;
    uint32_t uef_last_append_crc;
    uint16_t uef_last_append_length;
    int uef_published_final;
    int uef_stream_ready;
    int uef_upload_active;
    uint8_t uef_stream_format;
    FILE *trace;
    char wifi_ssid[33];
    char wifi_password[65];
    char wifi_security[6];
    char wifi_profile_path[512];
    int wifi_present;
    int wifi_enabled;
    int wifi_associated;
    int wifi_has_address;
    int wifi_connecting;
    uint64_t wifi_associate_at_ms;
    uint64_t wifi_dhcp_at_ms;
    unsigned wifi_associate_delay_ms;
    unsigned wifi_dhcp_delay_ms;
    unsigned scan_fields;
    FILE *sd_image;
    size_t sd_image_size;
    int sd_image_writable;
    net_handle handles[NET_MAX_HANDLES];
    pi1mhz_ftp *ftp;
#ifdef PI1MHZ_WOLFSSH
    pi1mhz_wolfssh *ssh;
#endif
};

static void trace_line(pi1mhz_net_backend *backend, const char *event,
                       unsigned handle, const char *detail);

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return 0u;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static unsigned env_unsigned(const char *name, unsigned fallback)
{
    const char *text = getenv(name);
    char *end;
    unsigned long value;
    if (!text || !*text)
        return fallback;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || *end || value > 60000u)
        return fallback;
    return (unsigned)value;
}

static void wifi_start_join(pi1mhz_net_backend *backend)
{
    uint64_t now = monotonic_ms();
    backend->wifi_enabled = 1;
    backend->wifi_connecting = 1;
    backend->wifi_associated = 0;
    backend->wifi_has_address = 0;
    backend->wifi_associate_at_ms = now + backend->wifi_associate_delay_ms;
    backend->wifi_dhcp_at_ms = backend->wifi_associate_at_ms +
                               backend->wifi_dhcp_delay_ms;
}

static void wifi_update(pi1mhz_net_backend *backend)
{
    uint64_t now;
    if (!backend->wifi_connecting)
        return;
    now = monotonic_ms();
    if (!backend->wifi_associated && now >= backend->wifi_associate_at_ms)
        backend->wifi_associated = 1;
    if (backend->wifi_associated && now >= backend->wifi_dhcp_at_ms) {
        backend->wifi_has_address = 1;
        backend->wifi_connecting = 0;
    }
}

static int wifi_credentials_valid(const char *ssid, const char *password,
                                  const char *security);

static int wifi_profile_load(pi1mhz_net_backend *backend)
{
    FILE *profile;
    char header[16];
    char security[16];
    char ssid[64];
    char password[96];
    if (!backend->wifi_profile_path[0])
        return 0;
    profile = fopen(backend->wifi_profile_path, "rb");
    if (!profile)
        return 0;
    if (!fgets(header, sizeof(header), profile) ||
        !fgets(security, sizeof(security), profile) ||
        !fgets(ssid, sizeof(ssid), profile) ||
        !fgets(password, sizeof(password), profile)) {
        fclose(profile);
        return 0;
    }
    fclose(profile);
    header[strcspn(header, "\r\n")] = 0;
    security[strcspn(security, "\r\n")] = 0;
    ssid[strcspn(ssid, "\r\n")] = 0;
    password[strcspn(password, "\r\n")] = 0;
    if (strcmp(header, "ELKWIFI1") ||
        (strcmp(security, "AUTO") && strcmp(security, "OPEN") &&
         strcmp(security, "WEP") && strcmp(security, "WPA") &&
         strcmp(security, "WPA2")) ||
        !wifi_credentials_valid(ssid, password, security))
        return 0;
    strcpy(backend->wifi_security, security);
    strcpy(backend->wifi_ssid, ssid);
    strcpy(backend->wifi_password, password);
    return 1;
}

static int wifi_profile_save(pi1mhz_net_backend *backend)
{
    FILE *profile;
    int ok;
    if (!backend->wifi_profile_path[0])
        return 1;
    profile = fopen(backend->wifi_profile_path, "wb");
    if (!profile)
        return 0;
    ok = fprintf(profile, "ELKWIFI1\n%s\n%s\n%s\n",
                 backend->wifi_security, backend->wifi_ssid,
                 backend->wifi_password) > 0;
    if (fclose(profile))
        ok = 0;
    return ok;
}

static int wifi_hex_key(const char *value)
{
    for (; *value; value++)
        if (!(('0' <= *value && *value <= '9') ||
              ('a' <= *value && *value <= 'f') ||
              ('A' <= *value && *value <= 'F')))
            return 0;
    return 1;
}

static int wifi_parse_credentials(const char *input, char security[6],
                                  const char **password)
{
    static const char *const modes[] = { "AUTO", "WPA", "WPA2", "WEP" };
    size_t i;
    size_t length;
    if (!strcasecmp(input, "OPEN")) {
        strcpy(security, "OPEN");
        *password = "";
        return 1;
    }
    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        length = strlen(modes[i]);
        if (!strncasecmp(input, modes[i], length) && input[length] == ':') {
            strcpy(security, modes[i]);
            *password = input + length + 1u;
            return 1;
        }
    }
    strcpy(security, "AUTO");
    *password = input;
    return 1;
}

static int wifi_credentials_valid(const char *ssid, const char *password,
                                  const char *security)
{
    size_t length;
    if (!ssid[0] || strlen(ssid) > 32u || strlen(password) > 64u)
        return 0;
    length = strlen(password);
    if (!strcmp(security, "OPEN"))
        return length == 0u;
    if (!strcmp(security, "AUTO"))
        return length == 0u || (length >= 8u && length <= 63u);
    if (!strcmp(security, "WPA") || !strcmp(security, "WPA2"))
        return length >= 8u && length <= 63u;
    if (!strcmp(security, "WEP"))
        return length == 5u || length == 13u ||
               ((length == 10u || length == 26u) && wifi_hex_key(password));
    return 0;
}

static void elkwifi_response(uint8_t *command, const char *response)
{
    size_t length = strlen(response);
    if (length > ELKWIFI_TEXT_MAX)
        length = ELKWIFI_TEXT_MAX;
    memcpy(command + 1, response, length);
    command[1 + length] = 0;
}

static uint8_t do_elkwifi_control(pi1mhz_net_backend *backend,
                                  uint8_t *command)
{
    char response[ELKWIFI_TEXT_MAX + 1u];
    wifi_update(backend);
    switch (command[0]) {
    case ELKWIFI_CMD_STATUS:
        if (!backend->wifi_present) {
            trace_line(backend, "WIFI_STATUS", 0, "absent");
            return ELKWIFI_ERR_NO_WIFI;
        }
        if (!backend->wifi_enabled)
            backend->wifi_enabled = 1;
        elkwifi_response(command, "Pi1MHz ElkWiFi 0.1.59\r\n\r\nOK\r\n");
        trace_line(backend, "WIFI_STATUS", 0, "ready");
        return PI1MHZ_NET_OK;
    case ELKWIFI_CMD_RADIO:
        if (!backend->wifi_present) {
            trace_line(backend, "WIFI_RADIO", 0, "absent");
            return ELKWIFI_ERR_NO_WIFI;
        }
        backend->wifi_enabled = 1;
        if (backend->wifi_ssid[0] && !backend->wifi_associated &&
            !backend->wifi_connecting)
            wifi_start_join(backend);
        elkwifi_response(command, "OK\r\n");
        trace_line(backend, "WIFI_RADIO", 0, "on");
        return PI1MHZ_NET_OK;
    case ELKWIFI_CMD_SCAN:
        if (!backend->wifi_present || !backend->wifi_enabled)
            return ELKWIFI_ERR_NO_WIFI;
        if (backend->scan_fields == 7u)
            elkwifi_response(command,
                "+CWLAP:(3,\"Pi1MHz-Fixture\",-42)\r\n\r\nOK\r\n");
        else
            elkwifi_response(command,
                "+CWLAP:(3,\"Pi1MHz-Fixture\",-42,\"02:00:00:00:00:01\",6)\r\n\r\nOK\r\n");
        trace_line(backend, "WIFI_SCAN", 0, "complete");
        return PI1MHZ_NET_OK;
    case ELKWIFI_CMD_JOIN:
        if (command[1] == 0u || command[1] == (uint8_t)'?') {
            if (backend->wifi_connecting && !backend->wifi_associated)
                elkwifi_response(command, "WIFI CONNECTING\r\n\r\nOK\r\n");
            else if (!backend->wifi_associated)
                elkwifi_response(command, "No AP\r\n\r\nOK\r\n");
            else {
                snprintf(response, sizeof(response),
                         "+CWJAP:\"%s\"\r\n\r\nOK\r\n",
                         backend->wifi_ssid);
                elkwifi_response(command, response);
            }
            trace_line(backend, "WIFI_JOIN_QUERY", 0,
                       backend->wifi_connecting ? "connecting" :
                       (backend->wifi_associated ? "associated" : "no-ap"));
            return PI1MHZ_NET_OK;
        }
        if (command[1] == 2u || command[1] == 3u) {
            uint8_t operation = command[1];
            backend->wifi_associated = 0;
            backend->wifi_has_address = 0;
            backend->wifi_connecting = 0;
            if (operation == 3u)
                backend->wifi_enabled = 0;
            elkwifi_response(command, operation == 3u
                                      ? "WIFI OFF\r\n\r\nOK\r\n"
                                      : "WIFI DISCONNECT\r\n\r\nOK\r\n");
            trace_line(backend, operation == 3u ? "WIFI_RADIO" : "WIFI_LEAVE",
                       0, operation == 3u ? "off" : "disconnected");
            return PI1MHZ_NET_OK;
        }
        if (command[1] == 1u) {
            const char *ssid = (const char *)command + 2;
            const char *input_password;
            const char *password;
            size_t length = strnlen(ssid, sizeof(backend->wifi_ssid));
            if (!length || length >= sizeof(backend->wifi_ssid))
                return ELKWIFI_ERR_PARAM;
            input_password = ssid + length + 1u;
            if (strnlen(input_password, sizeof(backend->wifi_password)) >=
                sizeof(backend->wifi_password) ||
                !wifi_parse_credentials(input_password, backend->wifi_security,
                                        &password) ||
                !wifi_credentials_valid(ssid, password,
                                        backend->wifi_security))
                return ELKWIFI_ERR_PARAM;
            memcpy(backend->wifi_ssid, ssid, length + 1u);
            strcpy(backend->wifi_password, password);
            if (!wifi_profile_save(backend))
                return ELKWIFI_ERR_IO;
            wifi_start_join(backend);
            snprintf(response, sizeof(response),
                     "WIFI CONNECTING %s\r\n\r\nOK\r\n",
                     backend->wifi_security);
            elkwifi_response(command, response);
            snprintf(response, sizeof(response), "%s %s", backend->wifi_ssid,
                     backend->wifi_security);
            trace_line(backend, "WIFI_JOIN", 0, response);
            return PI1MHZ_NET_OK;
        }
        return ELKWIFI_ERR_PARAM;
    case ELKWIFI_CMD_IFCFG:
        snprintf(response, sizeof(response),
                 "+CIFSR:STAIP,\"%s\"\r\n"
                 "+CIFSR:STAMAC,\"02:00:00:00:00:01\"\r\n\r\nOK\r\n",
                 backend->wifi_has_address ? "192.168.0.2" : "0.0.0.0");
        elkwifi_response(command, response);
        trace_line(backend, "WIFI_IFCFG", 0,
                   backend->wifi_has_address ? "192.168.0.2" : "0.0.0.0");
        return PI1MHZ_NET_OK;
    case ELKWIFI_CMD_LAPOPT:
        if (command[1] != 7u && command[1] != 127u)
            return ELKWIFI_ERR_PARAM;
        backend->scan_fields = command[1];
        snprintf(response, sizeof(response), "+CWLAPOPT:%u\r\n\r\nOK\r\n",
                 backend->scan_fields);
        elkwifi_response(command, response);
        return PI1MHZ_NET_OK;
    case ELKWIFI_CMD_PING:
        trace_line(backend, "PING", 0, (const char *)command + 1);
        elkwifi_response(command, backend->wifi_has_address
                                  ? "+1\r\n" : "ERROR\r\n");
        return PI1MHZ_NET_OK;
    case ELKWIFI_CMD_DATETIME:
        elkwifi_response(command, command[1]
                                  ? "12:00:00\r\n" : "07-08-2026\r\n");
        return PI1MHZ_NET_OK;
    case ELKWIFI_CMD_CANCEL:
        trace_line(backend, "CANCEL", 0, "");
        elkwifi_response(command, "OK\r\n");
        return PI1MHZ_NET_OK;
    case ELKWIFI_CMD_ONLINE:
        if (backend->wifi_has_address)
            elkwifi_response(command, "ONLINE 192.168.0.2\r\n");
        else if (backend->wifi_connecting || backend->wifi_associated)
            elkwifi_response(command, "OFFLINE CONNECTING\r\n");
        else if (!backend->wifi_enabled)
            elkwifi_response(command, "OFFLINE WIFI OFF\r\n");
        else
            elkwifi_response(command, "OFFLINE\r\n");
        trace_line(backend, "WIFI_ONLINE", 0,
                   backend->wifi_has_address ? "online" :
                   (backend->wifi_connecting || backend->wifi_associated
                    ? "connecting" : (backend->wifi_enabled ? "offline" : "off")));
        return PI1MHZ_NET_OK;
    default:
        return 0x42u;
    }
}

static const uint8_t fixture_telnet[] =
    "\033[2J\033[2;3HPi1MHz mailbox OK\r\n";
static const uint8_t fixture_ssh_identification[] =
    "Emulated Pi1MHz service\r\nSSH-2.0-Pi1MHzFixture_1.0\r\n";
static const uint8_t fixture_ssh_shell[] =
    "discarded\033[2J\033[2;3HPi1MHz SSH shell OK\r\n";
static const unsigned fixture_chunks[] = { 1u, 2u, 7u, 3u, 31u };

static uint32_t rd24(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16);
}

static uint32_t rd32(const uint8_t *p)
{
    return rd24(p) | ((uint32_t)p[3] << 24);
}

static uint8_t do_fat_sectors(pi1mhz_net_backend *backend, uint8_t *command,
                              uint8_t *jim, size_t jim_size)
{
    uint32_t buffer = rd32(command + 4);
    uint32_t sector = rd32(command + 8);
    uint32_t count = rd32(command + 12);
    uint64_t offset = (uint64_t)sector * FAT_SECTOR_SIZE;
    uint64_t length = (uint64_t)count * FAT_SECTOR_SIZE;

    if (!backend->sd_image)
        return FAT_RES_NOT_READY;
    if (command[1] != 0u || count == 0u || length > SIZE_MAX ||
        buffer > jim_size || (size_t)length > jim_size - buffer ||
        offset > backend->sd_image_size ||
        (size_t)length > backend->sd_image_size - (size_t)offset)
        return FAT_RES_PARAMETER;
    if (command[0] == FAT_CMD_WRITE_SECTORS && !backend->sd_image_writable)
        return FAT_RES_WRITE_PROTECT;
    if (fseeko(backend->sd_image, (off_t)offset, SEEK_SET))
        return FAT_RES_ERROR;
    if (command[0] == FAT_CMD_READ_SECTORS) {
        if (fread(jim + buffer, FAT_SECTOR_SIZE, count,
                  backend->sd_image) != count)
            return FAT_RES_ERROR;
    } else {
        if (fwrite(jim + buffer, FAT_SECTOR_SIZE, count,
                   backend->sd_image) != count || fflush(backend->sd_image))
            return FAT_RES_ERROR;
    }
    return PI1MHZ_NET_OK;
}

static uint16_t uef_rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int is_raw_uef(const uint8_t *data, size_t length)
{
    static const uint8_t magic[] = "UEF File!";
    return length >= sizeof(magic) && !memcmp(data, magic, sizeof(magic));
}

static size_t wicfs_stream_length(const uint8_t *window, size_t length)
{
    size_t position = 12u;
    size_t effective = length;
    int saw_data = 0;

    if (!is_raw_uef(window, length) || length < position)
        return length;
    while (position < length) {
        size_t chunk_length;
        uint16_t chunk_type;
        if (length - position < 6u)
            return length;
        chunk_type = uef_rd16(window + position);
        chunk_length = (size_t)rd32(window + position + 2u);
        position += 6u;
        if (chunk_length > length - position)
            return length;
        position += chunk_length;
        if (chunk_type == 0x0100u) {
            effective = position;
            saw_data = 1;
        }
    }
    return saw_data ? effective : length;
}

static int zip_has_one_entry(const uint8_t *source, size_t length)
{
    size_t pos;
    if (length < 22u)
        return 0;
    pos = length - 22u;
    for (;;) {
        if (rd32(source + pos) == 0x06054b50u &&
            uef_rd16(source + pos + 4u) == 0u &&
            uef_rd16(source + pos + 6u) == 0u &&
            uef_rd16(source + pos + 8u) == 1u &&
            uef_rd16(source + pos + 10u) == 1u &&
            (size_t)uef_rd16(source + pos + 20u) == length - pos - 22u)
            return 1;
        if (pos == 0u)
            return 0;
        pos--;
    }
}

/* Return 0 on success, 1 for output overflow, or -1 for malformed data. */
static int inflate_bytes(uint8_t *destination, size_t capacity,
                         const uint8_t *source, size_t source_length,
                         int window_bits, size_t *output_length)
{
    z_stream stream;
    int result;
    memset(&stream, 0, sizeof(stream));
    stream.next_in = (Bytef *)source;
    stream.avail_in = (uInt)source_length;
    stream.next_out = destination;
    stream.avail_out = (uInt)capacity;
    if (inflateInit2(&stream, window_bits) != Z_OK)
        return -1;
    result = inflate(&stream, Z_FINISH);
    *output_length = (size_t)stream.total_out;
    inflateEnd(&stream);
    if (result == Z_BUF_ERROR && *output_length == capacity)
        return 1;
    return result == Z_STREAM_END ? 0 : -1;
}

static uint8_t normalize_uef_control(pi1mhz_net_backend *backend,
                                     uint8_t *command, uint8_t *jim,
                                     size_t jim_size)
{
    uint8_t *window;
    uint8_t *scratch;
    size_t length;
    size_t output_length = 0;
    const char *format = NULL;
    int result;

    if (jim_size < UEF_BASE + 0x10000u)
        return NET_ERR_PARAM;
    window = jim + UEF_BASE;
    length = (size_t)window[UEF_CAPACITY] |
             ((size_t)window[UEF_CAPACITY + 1u] << 8);
    if (!length || length > UEF_CAPACITY) {
        memcpy(command + 1, "INVALID\r\n", 10u);
        return PI1MHZ_NET_OK;
    }
    if (is_raw_uef(window, length)) {
        format = "RAW\r\n";
        output_length = length;
        goto normalized;
    }

    scratch = (uint8_t *)malloc(length);
    if (!scratch)
        return NET_ERR_CONN;
    memcpy(scratch, window, length);

    if (length >= 2u && scratch[0] == 0x1fu && scratch[1] == 0x8bu) {
        result = inflate_bytes(window, UEF_CAPACITY, scratch, length,
                               MAX_WBITS + 16, &output_length);
        if (result == 1)
            format = "TOO LARGE\r\n";
        else if (result || !is_raw_uef(window, output_length))
            format = "INVALID\r\n";
        else
            format = "GZIP\r\n";
    } else if (length >= 30u && rd32(scratch) == 0x04034b50u &&
               zip_has_one_entry(scratch, length)) {
        uint16_t flags = uef_rd16(scratch + 6u);
        uint16_t method = uef_rd16(scratch + 8u);
        uint32_t expected_crc = rd32(scratch + 14u);
        uint32_t compressed_length = rd32(scratch + 18u);
        uint32_t expected_length = rd32(scratch + 22u);
        size_t data = 30u + (size_t)uef_rd16(scratch + 26u) +
                      (size_t)uef_rd16(scratch + 28u);
        if ((flags & 9u) || (method != 0u && method != 8u) ||
            data > length || compressed_length > length - data) {
            format = "INVALID\r\n";
        } else if (expected_length > UEF_CAPACITY) {
            format = "TOO LARGE\r\n";
        } else {
            if (method == 0u) {
                output_length = compressed_length;
                memmove(window, scratch + data, output_length);
                result = output_length == expected_length ? 0 : -1;
            } else {
                result = inflate_bytes(window, UEF_CAPACITY, scratch + data,
                                       compressed_length, -MAX_WBITS,
                                       &output_length);
            }
            if (result == 1)
                format = "TOO LARGE\r\n";
            else if (result || output_length != expected_length ||
                     crc32(0L, window, (uInt)output_length) != expected_crc)
                format = "INVALID\r\n";
            else if (is_raw_uef(window, output_length))
                format = "ZIP\r\n";
            else if (output_length >= 2u && window[0] == 0x1fu &&
                     window[1] == 0x8bu) {
                uint8_t *inner = (uint8_t *)malloc(output_length);
                if (!inner) {
                    free(scratch);
                    return NET_ERR_CONN;
                }
                memcpy(inner, window, output_length);
                result = inflate_bytes(window, UEF_CAPACITY, inner,
                                       output_length, MAX_WBITS + 16,
                                       &output_length);
                free(inner);
                format = result == 1 ? "TOO LARGE\r\n" :
                         result || !is_raw_uef(window, output_length) ?
                         "INVALID\r\n" : "ZIP\r\n";
            } else {
                format = "INVALID\r\n";
            }
        }
    } else {
        format = "INVALID\r\n";
    }
    free(scratch);

    if (!strcmp(format, "TOO LARGE\r\n") || !strcmp(format, "INVALID\r\n")) {
        memcpy(command + 1, format, strlen(format) + 1u);
        return PI1MHZ_NET_OK;
    }

normalized:
    if (backend->uef_trim_tail)
        output_length = wicfs_stream_length(window, output_length);
    memcpy(backend->uef_stream, window, output_length);
    backend->uef_stream_length = output_length;
    backend->uef_stream_cursor = 0u;
    backend->uef_stream_ready = 1;
    backend->uef_upload_active = 0;
    if (++backend->uef_stream_token == 0u)
        ++backend->uef_stream_token;
    backend->uef_stream_format = format[0];
    window[UEF_CAPACITY] = (uint8_t)output_length;
    window[UEF_CAPACITY + 1u] = (uint8_t)(output_length >> 8);
    memcpy(command + 1, format, strlen(format) + 1u);
    command[17] = '1';
    return PI1MHZ_NET_OK;
}

static void uef_stream_reply(pi1mhz_net_backend *backend, uint8_t *command)
{
    uint8_t *p = command + 1;
    memcpy(p, "IUEF", 4u);
    p[4] = UEF_STREAM_VERSION;
    p[5] = (uint8_t)backend->uef_stream_token;
    p[6] = (uint8_t)(backend->uef_stream_token >> 8);
    p[7] = (uint8_t)(backend->uef_stream_token >> 16);
    p[8] = (uint8_t)(backend->uef_stream_token >> 24);
    p[9] = (uint8_t)backend->uef_window_generation;
    p[10] = (uint8_t)(backend->uef_window_generation >> 8);
    p[11] = (uint8_t)(backend->uef_window_generation >> 16);
    p[12] = (uint8_t)(backend->uef_window_generation >> 24);
    p[13] = (uint8_t)backend->uef_published_length;
    p[14] = (uint8_t)(backend->uef_published_length >> 8);
    p[15] = backend->uef_published_final ? 1u : 0u;
    p[16] = backend->uef_stream_format;
    p[17] = 0u;
}

/* Copy a published window into JIM, skipping the mirrored tail of each page so
 * the host's page-and-offset walk lands on exactly these bytes. */
static uint8_t guard_image[UEF_GUARD_LENGTH];
static int     guard_image_valid;

static void guard_stamp(uint8_t *jim)
{
    unsigned page;
    if (!guard_image_valid)
        return;
    /* Page 0 is the service command and reply buffer, which OSWORD &65
     * clients read as 241 contiguous bytes, so it cannot also hold the guard.
     * The host keeps the selector off page 0 outside service calls. */
    for (page = UEF_FIRST_PAGE; page < 256u; page++)
        memcpy(jim + UEF_BASE + (page << 8) + UEF_GUARD_OFFSET,
               guard_image, UEF_GUARD_LENGTH);
}

/* Lay the stream out in UEF_GUARD_OFFSET runs, one per page, so it never
 * covers the guard. */
static void uef_window_scatter(uint8_t *jim, const uint8_t *source, size_t count)
{
    size_t page = 0;
    while (count) {
        size_t chunk = count < UEF_GUARD_OFFSET ? count : UEF_GUARD_OFFSET;
        memcpy(jim + UEF_BASE + ((page + UEF_FIRST_PAGE) << 8), source, chunk);
        source += chunk;
        count -= chunk;
        page++;
    }
    guard_stamp(jim);
}

static void uef_stream_publish(pi1mhz_net_backend *backend, uint8_t *jim)
{
    size_t available = backend->uef_stream_length - backend->uef_stream_cursor;
    size_t limit = guard_image_valid ? UEF_GUARD_WINDOW : UEF_FLAT_WINDOW;
    size_t count = available < limit ? available : limit;
    backend->uef_published_offset = backend->uef_stream_cursor;
    if (count) {
        if (guard_image_valid)
            uef_window_scatter(jim,
                               backend->uef_stream + backend->uef_stream_cursor,
                               count);
        else
            memcpy(jim + UEF_BASE + (UEF_FIRST_PAGE << 8),
                   backend->uef_stream + backend->uef_stream_cursor, count);
    }
    backend->uef_stream_cursor += count;
    backend->uef_published_length = (uint16_t)count;
    backend->uef_published_final =
        backend->uef_stream_cursor == backend->uef_stream_length;
    jim[UEF_CAPACITY] = (uint8_t)count;
    jim[UEF_CAPACITY + 1u] = (uint8_t)(count >> 8);
}

/* Publishing or withdrawing the guard changes the shape of the window, so
 * anything already published was laid out for the old shape and the host would
 * read it misaligned. Lay the same bytes down again under the new rule. */
static void uef_window_relayout(pi1mhz_net_backend *backend, uint8_t *jim);

static void uef_stream_republish(pi1mhz_net_backend *backend, uint8_t *jim)
{
    if (!backend->uef_published_length)
        return;
    if (guard_image_valid)
        uef_window_scatter(jim, backend->uef_stream +
                                    backend->uef_published_offset,
                           backend->uef_published_length);
    else
        memcpy(jim + UEF_BASE + (UEF_FIRST_PAGE << 8),
               backend->uef_stream + backend->uef_published_offset,
               backend->uef_published_length);
    jim[UEF_CAPACITY] = (uint8_t)backend->uef_published_length;
    jim[UEF_CAPACITY + 1u] = (uint8_t)(backend->uef_published_length >> 8);
}

static void uef_window_relayout(pi1mhz_net_backend *backend, uint8_t *jim)
{
    if (!backend->uef_stream_ready)
        return;
    backend->uef_stream_cursor = backend->uef_published_offset;
    uef_stream_publish(backend, jim);
}

static void uef_stream_clear(pi1mhz_net_backend *backend, uint8_t *jim)
{
    backend->uef_stream_length = 0u;
    backend->uef_stream_cursor = 0u;
    backend->uef_published_offset = 0u;
    backend->uef_published_length = 0u;
    backend->uef_window_generation = 0u;
    backend->uef_last_append_sequence = UINT32_MAX;
    backend->uef_last_append_crc = 0u;
    backend->uef_last_append_length = 0u;
    backend->uef_published_final = 0;
    backend->uef_stream_ready = 0;
    backend->uef_upload_active = 0;
    backend->uef_stream_format = 0u;
    jim[UEF_CAPACITY] = 0u;
    jim[UEF_CAPACITY + 1u] = 0u;
}

static uint8_t uef_stream_normalize_reply(uint8_t *command,
                                          const char *message)
{
    size_t length = strlen(message);
    memcpy(command + 1u, message, length + 1u);
    return PI1MHZ_NET_OK;
}

static uint8_t incremental_uef_control(pi1mhz_net_backend *backend,
                                       uint8_t *command, uint8_t *jim)
{
    uint8_t *p = command + 1;
    uint8_t operation = p[5];
    uint32_t token = rd32(p + 6);
    uint32_t value = rd32(p + 10);
    uint16_t length = uef_rd16(p + 14);
    uint32_t crc = rd32(p + 16);
    static const char *const operation_names[] = {
        "PROBE", "BEGIN", "APPEND", "FINALIZE", "REWIND", "REFILL", "CLOSE"
    };
    char trace_detail[96];

    snprintf(trace_detail, sizeof(trace_detail),
             "%s token=%08X value=%u length=%u generation=%u",
             operation < sizeof(operation_names) / sizeof(operation_names[0])
                 ? operation_names[operation] : "UNKNOWN",
             token, value, length, backend->uef_window_generation);
    trace_line(backend, "UEF_STREAM", operation, trace_detail);

    switch (operation) {
    case UEF_OP_PROBE:
        break;
    case UEF_OP_BEGIN:
        uef_stream_clear(backend, jim);
        backend->uef_upload_active = 1;
        if (++backend->uef_stream_token == 0u)
            ++backend->uef_stream_token;
        break;
    case UEF_OP_APPEND:
    {
        uint32_t actual_crc;
        if (!token) token = backend->uef_stream_token;
        if (!length)
            length = (uint16_t)(jim[UEF_CAPACITY] |
                                ((uint16_t)jim[UEF_CAPACITY + 1u] << 8));
        if (!backend->uef_upload_active || token != backend->uef_stream_token ||
            !length || length > UEF_UPLOAD_MAX)
            return NET_ERR_PARAM;
        actual_crc = (uint32_t)crc32(0L, jim, length);
        if (value == backend->uef_window_generation) {
            if (length > UEF_STREAM_CAPACITY - backend->uef_stream_length)
                return NET_ERR_PARAM;
            if (crc && crc != actual_crc)
                return NET_ERR_PARAM;
            memcpy(backend->uef_stream + backend->uef_stream_length, jim, length);
            backend->uef_stream_length += length;
            backend->uef_last_append_sequence = value;
            backend->uef_last_append_length = length;
            backend->uef_last_append_crc = actual_crc;
            backend->uef_window_generation++;
        } else if (value + 1u != backend->uef_window_generation ||
                   value != backend->uef_last_append_sequence ||
                   length != backend->uef_last_append_length ||
                   actual_crc != backend->uef_last_append_crc ||
                   (crc && crc != actual_crc)) {
            return NET_ERR_PARAM;
        }
        break;
    }
    case UEF_OP_FINALIZE:
    {
        size_t normalized = backend->uef_stream_length;
        size_t output = 0u;
        int result;
        if (!token) token = backend->uef_stream_token;
        if (!backend->uef_upload_active || token != backend->uef_stream_token ||
            !normalized)
            return NET_ERR_PARAM;
        memcpy(backend->uef_scratch, backend->uef_stream, normalized);
        if (is_raw_uef(backend->uef_scratch, normalized)) {
            backend->uef_stream_format = 'R';
        } else if (normalized >= 2u && backend->uef_scratch[0] == 0x1fu &&
                   backend->uef_scratch[1] == 0x8bu) {
            result = inflate_bytes(backend->uef_stream, UEF_STREAM_CAPACITY,
                                   backend->uef_scratch, normalized,
                                   MAX_WBITS + 16, &output);
            if (result == Z_BUF_ERROR)
                return uef_stream_normalize_reply(command, "TOO LARGE\r\n");
            if (result || !is_raw_uef(backend->uef_stream, output))
                return uef_stream_normalize_reply(command, "INVALID\r\n");
            normalized = output;
            backend->uef_stream_format = 'G';
        } else if (normalized >= 30u &&
                   rd32(backend->uef_scratch) == 0x04034b50u &&
                   zip_has_one_entry(backend->uef_scratch, normalized)) {
            uint16_t flags = uef_rd16(backend->uef_scratch + 6u);
            uint16_t method = uef_rd16(backend->uef_scratch + 8u);
            uint32_t expected_crc = rd32(backend->uef_scratch + 14u);
            uint32_t compressed_length = rd32(backend->uef_scratch + 18u);
            uint32_t expected_length = rd32(backend->uef_scratch + 22u);
            size_t data = 30u +
                          (size_t)uef_rd16(backend->uef_scratch + 26u) +
                          (size_t)uef_rd16(backend->uef_scratch + 28u);
            if (expected_length > UEF_STREAM_CAPACITY)
                return uef_stream_normalize_reply(command, "TOO LARGE\r\n");
            if ((flags & 9u) || (method != 0u && method != 8u) ||
                data > normalized ||
                compressed_length > normalized - data)
                return uef_stream_normalize_reply(command, "INVALID\r\n");
            if (method == 0u) {
                output = compressed_length;
                memcpy(backend->uef_stream, backend->uef_scratch + data, output);
                result = output == expected_length ? 0 : -1;
            } else {
                result = inflate_bytes(backend->uef_stream,
                                       UEF_STREAM_CAPACITY,
                                       backend->uef_scratch + data,
                                       compressed_length, -MAX_WBITS, &output);
            }
            if (result == Z_BUF_ERROR)
                return uef_stream_normalize_reply(command, "TOO LARGE\r\n");
            if (result || output != expected_length ||
                crc32(0L, backend->uef_stream, (uInt)output) != expected_crc)
                return uef_stream_normalize_reply(command, "INVALID\r\n");
            if (is_raw_uef(backend->uef_stream, output)) {
                normalized = output;
            } else if (output >= 2u && backend->uef_stream[0] == 0x1fu &&
                       backend->uef_stream[1] == 0x8bu) {
                memcpy(backend->uef_scratch, backend->uef_stream, output);
                result = inflate_bytes(backend->uef_stream,
                                       UEF_STREAM_CAPACITY,
                                       backend->uef_scratch, output,
                                       MAX_WBITS + 16, &normalized);
                if (result == Z_BUF_ERROR)
                    return uef_stream_normalize_reply(command, "TOO LARGE\r\n");
                if (result || !is_raw_uef(backend->uef_stream, normalized))
                    return uef_stream_normalize_reply(command, "INVALID\r\n");
            } else {
                return uef_stream_normalize_reply(command, "INVALID\r\n");
            }
            backend->uef_stream_format = 'Z';
        } else {
            return uef_stream_normalize_reply(command, "INVALID\r\n");
        }
        backend->uef_stream_length = normalized;
        backend->uef_stream_cursor = 0u;
        backend->uef_stream_ready = 1;
        backend->uef_upload_active = 0;
        backend->uef_window_generation++;
        uef_stream_publish(backend, jim);
        break;
    }
    case UEF_OP_REWIND:
        if (!backend->uef_stream_ready ||
            (token && token != backend->uef_stream_token))
            return NET_ERR_PARAM;
        backend->uef_stream_cursor = 0u;
        backend->uef_window_generation++;
        uef_stream_publish(backend, jim);
        break;
    case UEF_OP_REFILL:
        if (!backend->uef_stream_ready ||
            (token && token != backend->uef_stream_token))
            return NET_ERR_PARAM;
        if ((token == 0u && value == 0u) || value == 0xffffffffu ||
            value == backend->uef_window_generation) {
            backend->uef_window_generation++;
            uef_stream_publish(backend, jim);
        } else if (value + 1u == backend->uef_window_generation) {
            uef_stream_republish(backend, jim);
        } else {
            return NET_ERR_PARAM;
        }
        break;
    case UEF_OP_REPUBLISH:
        /* The host's reply buffer is the first bytes of JIM page 0, which is
         * also where the published window starts, so a command that copies a
         * reply while a stream is open treads on the window. Lay the same
         * bytes down again without moving the cursor or the generation. */
        if (!backend->uef_stream_ready ||
            (token && token != backend->uef_stream_token))
            return NET_ERR_PARAM;
        uef_stream_republish(backend, jim);
        break;
    case UEF_OP_CLOSE:
        if (token && token != backend->uef_stream_token)
            return NET_ERR_PARAM;
        uef_stream_clear(backend, jim);
        break;
    default:
        return NET_ERR_PARAM;
    }
    snprintf(trace_detail, sizeof(trace_detail),
             "total=%zu cursor=%zu window=%u final=%u generation=%u",
             backend->uef_stream_length, backend->uef_stream_cursor,
             (unsigned)backend->uef_published_length,
             backend->uef_published_final ? 1u : 0u,
             backend->uef_window_generation);
    trace_line(backend, "UEF_WINDOW", operation, trace_detail);
    uef_stream_reply(backend, command);
    return PI1MHZ_NET_OK;
}

static void wr24(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
}

static void wr32be(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static size_t append_ssh_string(uint8_t *p, const char *value)
{
    size_t length = strlen(value);
    wr32be(p, (uint32_t)length);
    memcpy(p + 4, value, length);
    return length + 4;
}

static size_t build_fixture_kexinit(uint8_t *packet)
{
    static const char *names[] = {
        "curve25519-sha256", "ssh-ed25519",
        "aes128-ctr", "aes128-ctr",
        "hmac-sha2-256", "hmac-sha2-256",
        "none", "none", "", ""
    };
    size_t position = 5;
    size_t payload_start = position;
    size_t payload_length;
    size_t padding;
    unsigned i;

    packet[position++] = 20; /* SSH_MSG_KEXINIT */
    for (i = 0; i < 16; i++)
        packet[position++] = (uint8_t)(0x10u + i);
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        position += append_ssh_string(packet + position, names[i]);
    packet[position++] = 0; /* first_kex_packet_follows */
    memset(packet + position, 0, 4);
    position += 4;
    payload_length = position - payload_start;
    padding = (8 - ((5 + payload_length) & 7)) & 7;
    if (padding < 4)
        padding += 8;
    packet[4] = (uint8_t)padding;
    for (i = 0; i < padding; i++)
        packet[position++] = (uint8_t)(0x80u + i);
    wr32be(packet, (uint32_t)(1 + payload_length + padding));
    return position;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint8_t secure_random(uint8_t *destination, size_t length)
{
    int fd = open("/dev/urandom", O_RDONLY);
    size_t done = 0;
    if (fd < 0)
        return NET_ERR_CONN;
    while (done < length) {
        ssize_t count = read(fd, destination + done, length - done);
        if (count <= 0) {
            close(fd);
            return NET_ERR_CONN;
        }
        done += (size_t)count;
    }
    close(fd);
    return PI1MHZ_NET_OK;
}

static uint8_t do_read(pi1mhz_net_backend *backend, net_handle *handle,
                       unsigned index, uint8_t *command, uint8_t *jim,
                       size_t jim_size);
static uint8_t do_write(pi1mhz_net_backend *backend, net_handle *handle,
                        unsigned index, uint8_t *command, uint8_t *jim,
                        size_t jim_size);
static uint8_t do_close(pi1mhz_net_backend *backend, net_handle *handle,
                        unsigned index);
static uint8_t live_open(net_handle *handle);
static void trace_line(pi1mhz_net_backend *backend, const char *event,
                       unsigned handle, const char *detail);
static void trace_bytes(pi1mhz_net_backend *backend, const char *event,
                        unsigned handle, const uint8_t *data, size_t size);

static uint8_t do_raw_dns(pi1mhz_net_backend *backend, net_handle *handle,
                          unsigned index, uint8_t *command)
{
    const char *hostname = (const char *)command + 1;
    char traced_hostname[224];
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    uint8_t result = NET_ERR_DNS;
    if (!handle->raw_allocated || !memchr(hostname, 0, 223u))
        return NET_ERR_PARAM;
    snprintf(traced_hostname, sizeof(traced_hostname), "%s", hostname);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = handle->raw_type ? SOCK_DGRAM : SOCK_STREAM;
    if (getaddrinfo(hostname, NULL, &hints, &addresses))
        return NET_ERR_DNS;
    for (address = addresses; address; address = address->ai_next) {
        const struct sockaddr_in *ipv4;
        if (address->ai_family != AF_INET ||
            address->ai_addrlen < sizeof(struct sockaddr_in))
            continue;
        ipv4 = (const struct sockaddr_in *)address->ai_addr;
        memcpy(command + 4, &ipv4->sin_addr, 4u);
        result = PI1MHZ_NET_OK;
        break;
    }
    freeaddrinfo(addresses);
    trace_line(backend, "DNS", index, traced_hostname);
    return result;
}

static uint8_t do_raw_connect(pi1mhz_net_backend *backend,
                              net_handle *handle, unsigned index,
                              uint8_t *command)
{
    unsigned port;
    if (!handle->raw_allocated)
        return NET_ERR_NOTOPEN;
    port = (unsigned)command[5] | ((unsigned)command[6] << 8);
    if (!port)
        return NET_ERR_PARAM;
    if (!handle->opening && !handle->opened && handle->fd < 0) {
        snprintf(handle->url, sizeof(handle->url), "%s://%u.%u.%u.%u:%u/",
                 handle->raw_type ? "UDP" : "TCP",
                 (unsigned)command[1], (unsigned)command[2],
                 (unsigned)command[3], (unsigned)command[4], port);
        trace_line(backend, "CONNECT", index, handle->url);
    }
    if (!backend->live) {
        handle->opened = 1;
        return PI1MHZ_NET_OK;
    }
    return live_open(handle);
}

static uint8_t do_secure(pi1mhz_net_backend *backend, net_handle *handle,
                         unsigned index, uint8_t *command, uint8_t *jim,
                         size_t jim_size)
{
    uint32_t destination;
    uint32_t url_address;
    uint32_t user_address;
    uint16_t length;
    switch (command[0]) {
    case SEC_CMD_CAPS:
        command[1] = 1;       /* ABI major */
        command[2] = 1;       /* ABI minor */
        command[3] = 0x01;
#ifdef PI1MHZ_WOLFSSH
        if (backend->ssh)
            command[3] |= 0x02;
#endif
        if (!backend->live)
            command[3] |= 0x02;
        if (command[3] & 0x02)
            command[3] |= 0x0c; /* password fallback and SFTP */
        command[4] = 0xB8;    /* maximum SSH packet: 35000 */
        command[5] = 0x88;
        command[6] = 1;       /* contexts */
        /* Match the firmware: bit 0 identifies the wolfSSH/wolfCrypt
         * managed-SSH provider.  The deterministic fixture models it too. */
        command[7] = (command[3] & 0x02) ? 1 : 0;
        command[8] = 'N';
        command[9] = 'T';
        command[10] = 'S';
        return PI1MHZ_NET_OK;
    case SEC_CMD_RANDOM:
        length = rd16(command + 1);
        destination = rd32(command + 4);
        if (!length || length > 64 || destination >= jim_size ||
            length > jim_size - destination)
            return NET_ERR_PARAM;
        return secure_random(jim + destination, length);
    case SEC_CMD_SSH_OPEN:
        url_address = rd32(command + 2);
        user_address = rd32(command + 6);
        if (url_address >= jim_size || user_address >= jim_size ||
            !memchr(jim + url_address, 0, jim_size - url_address) ||
            !memchr(jim + user_address, 0, jim_size - user_address))
            return NET_ERR_PARAM;
#ifdef PI1MHZ_WOLFSSH
        if (backend->live && backend->ssh) {
            char fingerprint[96];
            uint8_t result;
            if (!handle->open_polls) {
                snprintf(handle->url, sizeof(handle->url), "%s",
                         (const char *)jim + url_address);
                trace_line(backend, "SSH_OPEN", index, handle->url);
                trace_line(backend, "SSH_USER", index,
                           (const char *)jim + user_address);
                handle->open_polls = 1;
            }
            result = pi1mhz_wolfssh_open(
                backend->ssh, (const char *)jim + url_address,
                (const char *)jim + user_address, (command[1] & 1u) != 0,
                fingerprint);
            if (result == 0x2Cu) {
                size_t count = strnlen(fingerprint, sizeof(fingerprint));
                if (count == sizeof(fingerprint) || 0x020500u + count >= jim_size)
                    return NET_ERR_PARAM;
                memcpy(jim + 0x020500u, fingerprint, count + 1u);
                handle->open_polls = 0;
            }
            if (result == PI1MHZ_NET_OK) handle->opened = 1;
            return result;
        }
#endif
        if (backend->live)
            return PI1MHZ_NET_UNSUPPORTED;
        if (!handle->open_polls++) {
            snprintf(handle->url, sizeof(handle->url), "%s",
                     (const char *)jim + url_address);
            trace_line(backend, "SSH_OPEN", index, handle->url);
            trace_line(backend, "SSH_USER", index,
                       (const char *)jim + user_address);
            return PI1MHZ_NET_PENDING;
        }
        handle->opened = 1;
        handle->fixture_data = fixture_ssh_shell;
        handle->fixture_size = sizeof(fixture_ssh_shell) - 1;
        handle->fixture_pos = 0;
        handle->read_index = 0;
        return PI1MHZ_NET_OK;
    case SEC_CMD_SSH_READ:
#ifdef PI1MHZ_WOLFSSH
        if (backend->live && backend->ssh) {
            uint32_t maximum = rd24(command + 1);
            uint32_t target = rd32(command + 4);
            int count;
            if (target >= jim_size || maximum > jim_size - target)
                return NET_ERR_PARAM;
            count = pi1mhz_wolfssh_read(backend->ssh, jim + target, maximum);
            if (count < 0) { wr24(command + 1, 0); return (uint8_t)-count; }
            wr24(command + 1, (uint32_t)count);
            if (count) trace_bytes(backend, "READ", index, jim + target,
                                   (size_t)count);
            return PI1MHZ_NET_OK;
        }
#endif
        return do_read(backend, handle, index, command, jim, jim_size);
    case SEC_CMD_SSH_WRITE:
#ifdef PI1MHZ_WOLFSSH
        if (backend->live && backend->ssh) {
            uint32_t amount = rd24(command + 1);
            uint32_t source = rd32(command + 4);
            int count;
            if (source >= jim_size || amount > jim_size - source)
                return NET_ERR_PARAM;
            count = pi1mhz_wolfssh_write(backend->ssh, jim + source, amount);
            if (count < 0) { wr24(command + 1, 0); return (uint8_t)-count; }
            wr24(command + 1, (uint32_t)count);
            if (count) trace_bytes(backend, "WRITE", index, jim + source,
                                   (size_t)count);
            return PI1MHZ_NET_OK;
        }
#endif
        return do_write(backend, handle, index, command, jim, jim_size);
    case SEC_CMD_SSH_CLOSE:
#ifdef PI1MHZ_WOLFSSH
        if (backend->live && backend->ssh) {
            pi1mhz_wolfssh_close(backend->ssh);
            handle->opened = 0; handle->open_polls = 0;
            trace_line(backend, "CLOSE", index, handle->url);
            if (backend->exit_on_close) { if (backend->trace) fflush(backend->trace); exit(0); }
            return PI1MHZ_NET_OK;
        }
#endif
        return do_close(backend, handle, index);
    case SEC_CMD_SSH_PASSWORD: {
        uint32_t password_address = rd32(command + 4);
        size_t password_length = command[1];
        char redacted[48];
        uint8_t result = PI1MHZ_NET_OK;
        if (!password_length || password_length > 127u ||
            password_address >= jim_size ||
            password_length > jim_size - password_address)
            return NET_ERR_PARAM;
#ifdef PI1MHZ_WOLFSSH
        if (backend->live) {
            if (!backend->ssh)
                result = PI1MHZ_NET_UNSUPPORTED;
            else if (pi1mhz_wolfssh_password(backend->ssh,
                                             jim + password_address,
                                             password_length) != 0)
                result = NET_ERR_PARAM;
        }
#else
        if (backend->live) result = PI1MHZ_NET_UNSUPPORTED;
#endif
        snprintf(redacted, sizeof(redacted), "[redacted:%u bytes]",
                 (unsigned)password_length);
        trace_line(backend, "SSH_PASSWORD", index, redacted);
        memset(jim + password_address, 0, password_length);
        return result;
    }
    case SEC_CMD_SFTP_OPEN:
        url_address = rd32(command + 2);
        user_address = rd32(command + 6);
        if (url_address >= jim_size || user_address >= jim_size ||
            !memchr(jim + url_address, 0, jim_size - url_address) ||
            !memchr(jim + user_address, 0, jim_size - user_address))
            return NET_ERR_PARAM;
        if (backend->live) return PI1MHZ_NET_UNSUPPORTED;
        handle->opened = 1;
        snprintf(handle->url, sizeof(handle->url), "%s",
                 (const char *)jim + url_address);
        trace_line(backend, "SFTP_OPEN", index, handle->url);
        return PI1MHZ_NET_OK;
    case SEC_CMD_SFTP_PWD:
    case SEC_CMD_SFTP_CD:
    case SEC_CMD_SFTP_LS:
    case SEC_CMD_SFTP_DELETE:
    case SEC_CMD_SFTP_MKDIR:
    case SEC_CMD_SFTP_RMDIR: {
        uint32_t maximum = rd24(command + 1);
        uint32_t path_address = rd32(command + 4);
        uint32_t output = rd32(command + 8);
        const char *reply = command[0] == SEC_CMD_SFTP_PWD ? "/fixture\n" :
                            (command[0] == SEC_CMD_SFTP_LS ?
                             "fixture.txt\n" : "");
        size_t count = strlen(reply);
        if (!handle->opened || path_address >= jim_size ||
            !memchr(jim + path_address, 0, jim_size - path_address) ||
            output >= jim_size || maximum > jim_size - output ||
            count > maximum)
            return NET_ERR_PARAM;
        memcpy(jim + output, reply, count);
        wr24(command + 1, (uint32_t)count);
        return PI1MHZ_NET_OK;
    }
    case SEC_CMD_SFTP_GET_OPEN:
    case SEC_CMD_SFTP_PUT_OPEN:
        if (!handle->opened) return NET_ERR_NOTOPEN;
        handle->fixture_data = (const uint8_t *)"SFTP fixture\n";
        handle->fixture_size = 13u;
        handle->fixture_pos = 0u;
        return PI1MHZ_NET_OK;
    case SEC_CMD_SFTP_GET_READ:
        return do_read(backend, handle, index, command, jim, jim_size);
    case SEC_CMD_SFTP_PUT_WRITE:
        return do_write(backend, handle, index, command, jim, jim_size);
    case SEC_CMD_SFTP_TRANSFER_CLOSE:
        handle->fixture_data = NULL;
        handle->fixture_size = handle->fixture_pos = 0u;
        return PI1MHZ_NET_OK;
    case SEC_CMD_SFTP_CLOSE:
        return do_close(backend, handle, index);
    default:
        return PI1MHZ_NET_UNSUPPORTED;
    }
}

static void trace_line(pi1mhz_net_backend *backend, const char *event,
                       unsigned handle, const char *detail)
{
    if (!backend->trace)
        return;
    fprintf(backend->trace, "%s\t%u\t%s\n", event, handle,
            detail ? detail : "");
    fflush(backend->trace);
}

static void trace_bytes(pi1mhz_net_backend *backend, const char *event,
                        unsigned handle, const uint8_t *data, size_t size)
{
    size_t i;
    if (!backend->trace)
        return;
    fprintf(backend->trace, "%s\t%u\t", event, handle);
    for (i = 0; i < size; i++)
        fprintf(backend->trace, "%02x", data[i]);
    fputc('\n', backend->trace);
    fflush(backend->trace);
}

static int parse_url(const char *url, char *host, size_t host_size,
                     char *port, size_t port_size, char *path,
                     size_t path_size, int *http)
{
    const char *authority;
    const char *end;
    const char *colon;
    size_t length;
    const char *default_port;

    *http = 0;
    if (!strncasecmp(url, "HTTP://", 7)) {
        authority = url + 7;
        default_port = "80";
        *http = 1;
    } else if (!strncasecmp(url, "TCP://", 6) ||
               !strncasecmp(url, "UDP://", 6)) {
        authority = url + 6;
        default_port = "22";
    } else if (!strncasecmp(url, "TELNET://", 9)) {
        authority = url + 9;
        default_port = "23";
    } else {
        return -1;
    }
    end = strchr(authority, '/');
    if (!end)
        end = authority + strlen(authority);
    if (*http) {
        const char *url_path = *end ? end : "/";
        if (strlen(url_path) >= path_size)
            return -1;
        strcpy(path, url_path);
    } else {
        strcpy(path, "/");
    }
    colon = end;
    while (colon > authority && colon[-1] != ':')
        colon--;
    if (colon > authority) {
        length = (size_t)(colon - authority - 1);
        if (!length || length >= host_size || (size_t)(end - colon) >= port_size)
            return -1;
        memcpy(host, authority, length);
        host[length] = 0;
        memcpy(port, colon, (size_t)(end - colon));
        port[end - colon] = 0;
    } else {
        length = (size_t)(end - authority);
        if (!length || length >= host_size)
            return -1;
        memcpy(host, authority, length);
        host[length] = 0;
        snprintf(port, port_size, "%s", default_port);
    }
    return 0;
}

static uint8_t live_finish_open(net_handle *handle, const char *host,
                                const char *path)
{
    ssize_t count;
    if (!handle->http) {
        handle->opened = 1;
        return PI1MHZ_NET_OK;
    }
    if (!handle->request_length) {
        int length = snprintf(handle->request, sizeof(handle->request),
                              "GET %s HTTP/1.0\r\nHost: %s\r\n"
                              "User-Agent: Pi1MHz-Elkulator\r\n"
                              "Connection: close\r\n\r\n", path, host);
        if (length < 0 || (size_t)length >= sizeof(handle->request))
            return NET_ERR_PARAM;
        handle->request_length = (size_t)length;
    }
    count = send(handle->fd, handle->request + handle->request_sent,
                 handle->request_length - handle->request_sent, 0);
    if (count < 0)
        return (errno == EAGAIN || errno == EWOULDBLOCK)
                   ? PI1MHZ_NET_PENDING : NET_ERR_CONN;
    if (!count)
        return NET_ERR_CONN;
    handle->request_sent += (size_t)count;
    if (handle->request_sent != handle->request_length)
        return PI1MHZ_NET_PENDING;
    handle->opened = 1;
    return PI1MHZ_NET_OK;
}

static uint8_t live_open(net_handle *handle)
{
    char host[192];
    char port[16];
    char path[224];
    int http;
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    int error;

    if (parse_url(handle->url, host, sizeof(host), port, sizeof(port),
                  path, sizeof(path), &http))
        return NET_ERR_PARAM;
    handle->http = http;
    if (handle->opening) {
        socklen_t error_size = sizeof(error);
        if (getsockopt(handle->fd, SOL_SOCKET, SO_ERROR, &error, &error_size))
            error = errno;
        if (error == EINPROGRESS || error == EALREADY)
            return PI1MHZ_NET_PENDING;
        if (error) {
            close(handle->fd);
            handle->fd = -1;
            handle->opening = 0;
            return NET_ERR_CONN;
        }
        handle->opening = 0;
        return live_finish_open(handle, host, path);
    }
    if (handle->fd >= 0)
        return live_finish_open(handle, host, path);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = handle->raw_type ? SOCK_DGRAM : SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &addresses))
        return NET_ERR_DNS;
    for (address = addresses; address; address = address->ai_next) {
        int flags;
        handle->fd = socket(address->ai_family, address->ai_socktype,
                            address->ai_protocol);
        if (handle->fd < 0)
            continue;
        flags = fcntl(handle->fd, F_GETFL, 0);
        if (flags >= 0)
            (void)fcntl(handle->fd, F_SETFL, flags | O_NONBLOCK);
        if (!connect(handle->fd, address->ai_addr, address->ai_addrlen)) {
            freeaddrinfo(addresses);
            return live_finish_open(handle, host, path);
        }
        if (errno == EINPROGRESS) {
            handle->opening = 1;
            freeaddrinfo(addresses);
            return PI1MHZ_NET_PENDING;
        }
        close(handle->fd);
        handle->fd = -1;
    }
    freeaddrinfo(addresses);
    return NET_ERR_CONN;
}

pi1mhz_net_backend *pi1mhz_net_backend_create(const char *mode,
                                               const char *trace_path,
                                               int exit_on_close)
{
    const char *profile_path;
    const char *present;
    const char *sd_image_path;
    unsigned i;
    pi1mhz_net_backend *backend =
        (pi1mhz_net_backend *)calloc(1, sizeof(*backend));
    if (!backend)
        return NULL;
    backend->uef_stream = (uint8_t *)malloc(UEF_STREAM_CAPACITY);
    backend->uef_scratch = (uint8_t *)malloc(UEF_STREAM_CAPACITY);
    if (!backend->uef_stream || !backend->uef_scratch) {
        free(backend->uef_scratch);
        free(backend->uef_stream);
        free(backend);
        return NULL;
    }
    backend->live = mode && !strcasecmp(mode, "live");
    backend->exit_on_close = exit_on_close;
    {
        const char *trim = getenv("PI1MHZ_UEF_TRIM_TAIL");
        backend->uef_trim_tail = trim &&
            (!strcmp(trim, "1") || !strcasecmp(trim, "yes") ||
             !strcasecmp(trim, "true") || !strcasecmp(trim, "on"));
    }
    strcpy(backend->wifi_security, "AUTO");
    backend->wifi_present = 1;
    backend->wifi_enabled = 1;
    backend->wifi_associate_delay_ms =
        env_unsigned("PI1MHZ_WIFI_ASSOCIATE_MS", 500u);
    backend->wifi_dhcp_delay_ms = env_unsigned("PI1MHZ_WIFI_DHCP_MS", 500u);
    present = getenv("PI1MHZ_WIFI_PRESENT");
    if (present && (!strcmp(present, "0") || !strcasecmp(present, "no"))) {
        backend->wifi_present = 0;
        backend->wifi_enabled = 0;
    }
    profile_path = getenv("PI1MHZ_WIFI_PROFILE");
    if (profile_path && *profile_path) {
        if (strlen(profile_path) >= sizeof(backend->wifi_profile_path)) {
            free(backend->uef_scratch);
            free(backend->uef_stream);
            free(backend);
            return NULL;
        }
        strcpy(backend->wifi_profile_path, profile_path);
        if (backend->wifi_present && wifi_profile_load(backend))
            wifi_start_join(backend);
    } else {
        /* Preserve the historical fixture default unless a persistence path
           explicitly requests the boot/rejoin state machine. */
        strcpy(backend->wifi_ssid, "Pi1MHz-Fixture");
        backend->wifi_associated = backend->wifi_present;
        backend->wifi_has_address = backend->wifi_present;
    }
    backend->scan_fields = 127u;
    for (i = 0; i < NET_MAX_HANDLES; i++)
        backend->handles[i].fd = -1;
    sd_image_path = getenv("PI1MHZ_SD_IMAGE");
    if (sd_image_path && *sd_image_path) {
        off_t length;
        backend->sd_image = fopen(sd_image_path, "r+b");
        backend->sd_image_writable = backend->sd_image != NULL;
        if (!backend->sd_image)
            backend->sd_image = fopen(sd_image_path, "rb");
        if (!backend->sd_image ||
            fseeko(backend->sd_image, 0, SEEK_END) ||
            (length = ftello(backend->sd_image)) < 0 ||
            (uintmax_t)length > SIZE_MAX ||
            fseeko(backend->sd_image, 0, SEEK_SET)) {
            fprintf(stderr, "Pi1MHz mailbox: cannot open SD image %s\n",
                    sd_image_path);
            if (backend->sd_image)
                fclose(backend->sd_image);
            free(backend->uef_scratch);
            free(backend->uef_stream);
            free(backend);
            return NULL;
        }
        backend->sd_image_size = (size_t)length;
    }
#ifdef PI1MHZ_WOLFSSH
    if (backend->live) {
        const char *directory = getenv("PI1MHZ_SSH_DIR");
        if (directory && *directory)
            backend->ssh = pi1mhz_wolfssh_create(directory);
    }
#endif
    if (trace_path && *trace_path) {
        backend->trace = fopen(trace_path, "w");
        if (!backend->trace) {
            if (backend->sd_image)
                fclose(backend->sd_image);
#ifdef PI1MHZ_WOLFSSH
            if (backend->ssh)
                pi1mhz_wolfssh_destroy(backend->ssh);
#endif
            free(backend->uef_scratch);
            free(backend->uef_stream);
            free(backend);
            return NULL;
        }
        setvbuf(backend->trace, NULL, _IOLBF, 0);
    }
    backend->ftp = pi1mhz_ftp_create(backend->live);
    if (!backend->ftp) {
        pi1mhz_net_backend_destroy(backend);
        return NULL;
    }
    trace_line(backend, "MODE", 0, backend->live ? "live" : "fixture");
    return backend;
}

void pi1mhz_net_backend_destroy(pi1mhz_net_backend *backend)
{
    unsigned i;
    if (!backend)
        return;
    for (i = 0; i < NET_MAX_HANDLES; i++)
        if (backend->handles[i].fd >= 0)
            close(backend->handles[i].fd);
    pi1mhz_ftp_destroy(backend->ftp);
#ifdef PI1MHZ_WOLFSSH
    if (backend->ssh)
        pi1mhz_wolfssh_destroy(backend->ssh);
#endif
    if (backend->trace)
        fclose(backend->trace);
    if (backend->sd_image)
        fclose(backend->sd_image);
    free(backend->uef_scratch);
    free(backend->uef_stream);
    free(backend);
}

static uint8_t do_open(pi1mhz_net_backend *backend, net_handle *handle,
                       unsigned index, uint8_t *command)
{
    size_t length = strnlen((const char *)command + 2, 221);
    uint8_t result;
    if (length >= 221)
        return NET_ERR_PARAM;
    if (!handle->opening && !handle->opened && !handle->open_polls) {
        memcpy(handle->url, command + 2, length + 1);
        trace_line(backend, "OPEN", index, handle->url);
    }
    if (backend->live) {
        result = live_open(handle);
        return result;
    }
    if (!handle->open_polls++)
        return PI1MHZ_NET_PENDING;
    handle->opened = 1;
    if (!strncasecmp(handle->url, "TELNET://", 9)) {
        handle->fixture_data = fixture_telnet;
        handle->fixture_size = sizeof(fixture_telnet) - 1;
    } else if (!strncasecmp(handle->url, "TCP://", 6)) {
        size_t identification_size = sizeof(fixture_ssh_identification) - 1;
        memcpy(handle->fixture_storage, fixture_ssh_identification,
               identification_size);
        handle->fixture_size = identification_size + build_fixture_kexinit(
            handle->fixture_storage + identification_size);
        handle->fixture_data = handle->fixture_storage;
    } else {
        result = PI1MHZ_NET_UNSUPPORTED;
        handle->opened = 0;
        return result;
    }
    return PI1MHZ_NET_OK;
}

static uint8_t do_read(pi1mhz_net_backend *backend, net_handle *handle,
                       unsigned index, uint8_t *command, uint8_t *jim,
                       size_t jim_size)
{
    uint32_t maximum = rd24(command + 1);
    uint32_t destination = rd32(command + 4);
    ssize_t count;
    (void)index;
    if (!handle->opened)
        return NET_ERR_NOTOPEN;
    if (destination >= jim_size || maximum > jim_size - destination)
        return NET_ERR_PARAM;
    if (backend->live) {
        if (handle->http && !handle->headers_done) {
            uint8_t *separator;
            size_t header_length;
            if (handle->response_size >= sizeof(handle->response) - 1u)
                return NET_ERR_CONN;
            count = recv(handle->fd, handle->response + handle->response_size,
                         sizeof(handle->response) - 1u - handle->response_size,
                         0);
            if (count < 0) {
                wr24(command + 1, 0);
                return (errno == EAGAIN || errno == EWOULDBLOCK)
                           ? PI1MHZ_NET_OK : NET_ERR_CONN;
            }
            if (!count) {
                return NET_ERR_CONN;
            }
            handle->response_size += (size_t)count;
            handle->response[handle->response_size] = 0;
            separator = (uint8_t *)strstr((char *)handle->response,
                                          "\r\n\r\n");
            if (!separator) {
                wr24(command + 1, 0);
                return PI1MHZ_NET_OK;
            }
            if (handle->response_size < 12u ||
                memcmp(handle->response, "HTTP/1.", 7u) ||
                handle->response[9] != '2') {
                return NET_ERR_CONN;
            }
            {
                const char *field = strstr((const char *)handle->response,
                                           "\r\nContent-Length:");
                if (field && (const uint8_t *)field < separator) {
                    char *end;
                    unsigned long value = strtoul(field + 17, &end, 10);
                    if (end != field + 17 && value <= SIZE_MAX) {
                        handle->http_has_length = 1;
                        handle->http_content_length = (size_t)value;
                    }
                }
            }
            header_length = (size_t)(separator - handle->response) + 4u;
            memmove(handle->response, handle->response + header_length,
                    handle->response_size - header_length);
            handle->response_size -= header_length;
            handle->headers_done = 1;
        }
        if (handle->http && handle->response_pos < handle->response_size) {
            size_t available = handle->response_size - handle->response_pos;
            size_t copied = maximum < available ? maximum : available;
            memcpy(jim + destination, handle->response + handle->response_pos,
                   copied);
            handle->response_pos += copied;
            handle->http_body_read += copied;
            trace_bytes(backend, "READ", index, jim + destination, copied);
            wr24(command + 1, (uint32_t)copied);
            return PI1MHZ_NET_OK;
        }
        count = recv(handle->fd, jim + destination, maximum, 0);
        if (count > 0) {
            if (handle->http) {
                handle->http_body_read += (size_t)count;
            }
            trace_bytes(backend, "READ", index, jim + destination,
                        (size_t)count);
            wr24(command + 1, (uint32_t)count);
            return PI1MHZ_NET_OK;
        }
        wr24(command + 1, 0);
        if (!count) {
            if (handle->http_has_length &&
                handle->http_body_read < handle->http_content_length) {
                return NET_ERR_TCP_CLOSED;
            }
            return PI1MHZ_NET_EOF;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return PI1MHZ_NET_OK;
        return NET_ERR_CONN;
    }
    if (handle->fixture_pos >= handle->fixture_size) {
        wr24(command + 1, 0);
        return PI1MHZ_NET_EOF;
    }
    maximum = maximum < fixture_chunks[handle->read_index % 5]
                  ? maximum : fixture_chunks[handle->read_index % 5];
    handle->read_index++;
    if (maximum > handle->fixture_size - handle->fixture_pos)
        maximum = (uint32_t)(handle->fixture_size - handle->fixture_pos);
    memcpy(jim + destination, handle->fixture_data + handle->fixture_pos,
           maximum);
    trace_bytes(backend, "READ", index, jim + destination, maximum);
    handle->fixture_pos += maximum;
    wr24(command + 1, maximum);
    return PI1MHZ_NET_OK;
}

static uint8_t do_write(pi1mhz_net_backend *backend, net_handle *handle,
                        unsigned index, uint8_t *command, uint8_t *jim,
                        size_t jim_size)
{
    uint32_t length = rd24(command + 1);
    uint32_t source = rd32(command + 4);
    ssize_t count;
    if (!handle->opened)
        return NET_ERR_NOTOPEN;
    if (source >= jim_size || length > jim_size - source)
        return NET_ERR_PARAM;
    if (backend->live) {
        count = send(handle->fd, jim + source, length, 0);
        if (count < 0) {
            wr24(command + 1, 0);
            return (errno == EAGAIN || errno == EWOULDBLOCK)
                       ? PI1MHZ_NET_OK : NET_ERR_CONN;
        }
    } else {
        count = length > 5u ? 5 : (ssize_t)length;
    }
    trace_bytes(backend, "WRITE", index, jim + source, (size_t)count);
    wr24(command + 1, (uint32_t)count);
    return PI1MHZ_NET_OK;
}

static uint8_t do_close(pi1mhz_net_backend *backend, net_handle *handle,
                        unsigned index)
{
    if (handle->fd >= 0)
        close(handle->fd);
    handle->fd = -1;
    handle->opening = 0;
    handle->opened = 0;
    handle->request_length = 0;
    handle->request_sent = 0;
    handle->response_size = 0;
    handle->response_pos = 0;
    handle->headers_done = 0;
    handle->http_has_length = 0;
    handle->http_content_length = 0u;
    handle->http_body_read = 0u;
    handle->raw_allocated = 0;
    handle->raw_type = 0;
    trace_line(backend, "CLOSE", index, handle->url);
    if (backend->exit_on_close) {
        if (backend->trace)
            fflush(backend->trace);
        exit(0);
    }
    return PI1MHZ_NET_OK;
}

uint8_t pi1mhz_net_backend_dispatch(void *opaque, uint8_t selector,
                                    uint32_t command_pointer,
                                    uint8_t *jim, size_t jim_size)
{
    pi1mhz_net_backend *backend = (pi1mhz_net_backend *)opaque;
    unsigned index = selector & 0x0Fu;
    uint8_t *command;
    uint8_t *service_jim;
    size_t service_base;
    size_t service_size;
    net_handle *handle;
    char command_detail[16];
    if (!backend || jim_size < PI1MHZ_SERVICE_SIZE)
        return NET_ERR_PARAM;
    service_base = jim_size - PI1MHZ_SERVICE_SIZE;
    service_size = jim_size - service_base;
    if (command_pointer < service_base ||
        command_pointer - service_base + 224u > service_size)
        return NET_ERR_PARAM;
    service_jim = jim + service_base;
    command = service_jim + (command_pointer - service_base);
    snprintf(command_detail, sizeof(command_detail), "%02X:%02X",
             selector, command[0]);
    trace_line(backend, "COMMAND", index, command_detail);
    if (command[0] == FAT_CMD_READ_SECTORS ||
        command[0] == FAT_CMD_WRITE_SECTORS)
        return do_fat_sectors(backend, command, service_jim, service_size);
    if (command[0] == ELKWIFI_CMD_GUARD_IMAGE) {
        /* The host sends its assembled filing-vector guard, which the Pi
         * replicates into the top of every JIM page. A zero length withdraws
         * it and hands the whole page back to the stream. Only the host knows
         * what the guard does; the Pi never interprets it. */
        if (command[1] == 0u) {
            guard_image_valid = 0;
            memset(guard_image, 0, sizeof(guard_image));
            uef_window_relayout(backend, jim);
            elkwifi_response(command, "OFF\r\n");
            return PI1MHZ_NET_OK;
        }
        if (command[1] != UEF_GUARD_LENGTH)
            return NET_ERR_PARAM;
        memcpy(guard_image, command + 2, UEF_GUARD_LENGTH);
        guard_image_valid = 1;
        guard_stamp(jim);
        uef_window_relayout(backend, jim);
        elkwifi_response(command, "OK\r\n");
        return PI1MHZ_NET_OK;
    }
    if (selector == 0xFFu && command[0] == ELKWIFI_CMD_UEF_NORMALIZE) {
        if (command[1] == 'I' && command[2] == 'U' && command[3] == 'E' &&
            command[4] == 'F' && command[5] == UEF_STREAM_VERSION)
            return incremental_uef_control(backend, command, jim);
        return normalize_uef_control(backend, command, jim, jim_size);
    }
    switch (command[0]) {
    case ELKWIFI_CMD_STATUS:
    case ELKWIFI_CMD_SCAN:
    case ELKWIFI_CMD_JOIN:
    case ELKWIFI_CMD_IFCFG:
    case ELKWIFI_CMD_LAPOPT:
    case ELKWIFI_CMD_PING:
    case ELKWIFI_CMD_DATETIME:
    case ELKWIFI_CMD_CANCEL:
    case ELKWIFI_CMD_RADIO:
    case ELKWIFI_CMD_ONLINE:
        return do_elkwifi_control(backend, command);
    default:
        break;
    }
    if (command[0] >= 114u && command[0] <= 119u)
        return pi1mhz_ftp_dispatch(backend->ftp, command, service_jim,
                                   service_size);
    if (index >= NET_MAX_HANDLES)
        return NET_ERR_PARAM;
    handle = &backend->handles[index];
    switch (command[0]) {
    case NET_CMD_OPEN:
        if (command[1] > 1u)
            return PI1MHZ_NET_UNSUPPORTED;
        if (handle->raw_allocated || handle->opened || handle->opening)
            return NET_ERR_INUSE;
        handle->raw_allocated = 1;
        handle->raw_type = command[1];
        handle->http = 0;
        return PI1MHZ_NET_OK;
    case NET_CMD_DNS:
        return do_raw_dns(backend, handle, index, command);
    case NET_CMD_CONNECT:
        return do_raw_connect(backend, handle, index, command);
    case NET_CMD_SEND:
        return do_write(backend, handle, index, command, service_jim, service_size);
    case NET_CMD_RECV:
        return do_read(backend, handle, index, command, service_jim, service_size);
    case NET_CMD_COPY_PUBLIC: {
        size_t count = command[1];
        size_t destination = (size_t)command[2] | ((size_t)command[3] << 8);
        const size_t scratch = 0xfff100u;
        if (!count || count > 240u || destination + count > 0x10000u ||
            scratch + count > service_size)
            return NET_ERR_PARAM;
        memmove(jim + destination, service_jim + scratch, count);
        return PI1MHZ_NET_OK;
    }
    case NET_CMD_CLOSE:
        return do_close(backend, handle, index);
    case NET_CMD_URL_OPEN:
        return do_open(backend, handle, index, command);
    case NET_CMD_URL_READ:
        return do_read(backend, handle, index, command, service_jim, service_size);
    case NET_CMD_URL_WRITE:
        return do_write(backend, handle, index, command, service_jim, service_size);
    case NET_CMD_URL_CLOSE:
        return do_close(backend, handle, index);
    case SEC_CMD_CAPS:
    case SEC_CMD_SSH_OPEN:
    case SEC_CMD_SSH_READ:
    case SEC_CMD_SSH_WRITE:
    case SEC_CMD_SSH_CLOSE:
    case SEC_CMD_SSH_PASSWORD:
    case SEC_CMD_SFTP_OPEN:
    case SEC_CMD_SFTP_PWD:
    case SEC_CMD_SFTP_CD:
    case SEC_CMD_SFTP_LS:
    case SEC_CMD_SFTP_DELETE:
    case SEC_CMD_SFTP_MKDIR:
    case SEC_CMD_SFTP_RMDIR:
    case SEC_CMD_SFTP_GET_OPEN:
    case SEC_CMD_SFTP_GET_READ:
    case SEC_CMD_SFTP_PUT_OPEN:
    case SEC_CMD_SFTP_PUT_WRITE:
    case SEC_CMD_SFTP_TRANSFER_CLOSE:
    case SEC_CMD_SFTP_CLOSE:
        return do_secure(backend, handle, index, command, service_jim, service_size);
    case SEC_CMD_RANDOM:
        return do_secure(backend, handle, index, command, service_jim, service_size);
    default:
        return PI1MHZ_NET_UNSUPPORTED;
    }
}
