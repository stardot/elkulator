#include "pi1mhz_wolfssh.h"

#include <wolfssh/ssh.h>
#include <wolfssh/error.h>
#include <wolfssl/wolfcrypt/coding.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/sha256.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define NTS_OK 0u
#define NTS_PENDING 1u
#define NTS_EOF 0x20u
#define NTS_ERR_PARAM 0x23u
#define NTS_ERR_DNS 0x24u
#define NTS_ERR_CONN 0x25u
#define NTS_ERR_PROTOCOL 0x2Bu
#define NTS_HOSTKEY_UNKNOWN 0x2Cu
#define NTS_AUTH_FAILED 0x2Du

enum provider_stage {
    PROVIDER_IDLE, PROVIDER_TCP, PROVIDER_SSH, PROVIDER_RESIZE, PROVIDER_UP
};

struct pi1mhz_wolfssh {
    enum provider_stage stage;
    int socket_fd;
    WOLFSSH_CTX *ctx;
    WOLFSSH *ssh;
    char directory[384];
    char host[192];
    char host_id[224];
    char username[64];
    char fingerprint[96];
    char host_key_type[64];
    char host_key[2048];
    int trust_unknown;
    int host_unknown;
    int host_changed;
    byte *public_key;
    word32 public_key_size;
    const byte *public_key_type;
    word32 public_key_type_size;
    byte *private_key;
    word32 private_key_size;
    const byte *private_key_type;
    word32 private_key_type_size;
    byte password[128];
    word32 password_size;
};

static int debug_enabled(void)
{
    const char *value = getenv("PI1MHZ_SSH_DEBUG");
    return value && *value && strcmp(value, "0");
}

static int parse_url(const char *url, char *host, size_t host_size,
                     char *port, size_t port_size)
{
    const char *start;
    const char *end;
    const char *colon;
    size_t length;
    if (strncmp(url, "TCP://", 6)) return -1;
    start = url + 6;
    end = strchr(start, '/');
    if (!end) end = start + strlen(start);
    colon = end;
    while (colon > start && colon[-1] != ':') colon--;
    if (colon == start || colon > end) return -1;
    length = (size_t)(colon - start - 1);
    if (!length || length >= host_size || (size_t)(end - colon) >= port_size)
        return -1;
    memcpy(host, start, length); host[length] = 0;
    memcpy(port, colon, (size_t)(end - colon)); port[end - colon] = 0;
    return 0;
}

static int want_io(pi1mhz_wolfssh *provider, int result)
{
    int error;
    if (result == WS_WANT_READ || result == WS_WANT_WRITE) return 1;
    error = provider->ssh ? wolfSSH_get_error(provider->ssh) : result;
    return error == WS_WANT_READ || error == WS_WANT_WRITE;
}

static int make_path(pi1mhz_wolfssh *provider, const char *name,
                     char *out, size_t out_size)
{
    int count = snprintf(out, out_size, "%s/%s", provider->directory, name);
    return count > 0 && (size_t)count < out_size ? 0 : -1;
}

static int load_keys(pi1mhz_wolfssh *provider)
{
    char path[512];
    byte private_flag = 0;
    int result;
    if (make_path(provider, "id_ed25519", path, sizeof(path))) return -1;
    result = wolfSSH_ReadKey_file(path, &provider->private_key,
        &provider->private_key_size, &provider->private_key_type,
        &provider->private_key_type_size, &private_flag, NULL);
    if (result != WS_SUCCESS || !private_flag) {
        if (debug_enabled())
            fprintf(stderr, "private key load %s: %d private=%u\n", path,
                    result, (unsigned)private_flag);
        return -1;
    }
    if (make_path(provider, "id_ed25519.pub", path, sizeof(path))) return -1;
    private_flag = 0;
    result = wolfSSH_ReadKey_file(path, &provider->public_key,
        &provider->public_key_size, &provider->public_key_type,
        &provider->public_key_type_size, &private_flag, NULL);
    if (result != WS_SUCCESS || private_flag) {
        if (debug_enabled())
            fprintf(stderr, "public key load %s: %d private=%u\n", path,
                    result, (unsigned)private_flag);
        return -1;
    }
    return 0;
}

static int auth_callback(byte auth_type, WS_UserAuthData *data, void *opaque)
{
    pi1mhz_wolfssh *provider = opaque;
    WS_UserAuthData_PublicKey *key;
    if (auth_type == WOLFSSH_USERAUTH_PASSWORD && provider->password_size) {
        if (debug_enabled())
            fprintf(stderr, "wolfSSH password callback: %u bytes\n",
                    (unsigned)provider->password_size);
        data->sf.password.password = provider->password;
        data->sf.password.passwordSz = provider->password_size;
        return WOLFSSH_USERAUTH_SUCCESS;
    }
    if (auth_type != WOLFSSH_USERAUTH_PUBLICKEY || !provider->public_key ||
        !provider->private_key) return WOLFSSH_USERAUTH_FAILURE;
    key = &data->sf.publicKey;
    key->publicKeyType = provider->public_key_type;
    key->publicKeyTypeSz = provider->public_key_type_size;
    key->publicKey = provider->public_key;
    key->publicKeySz = provider->public_key_size;
    key->privateKey = provider->private_key;
    key->privateKeySz = provider->private_key_size;
    return WOLFSSH_USERAUTH_SUCCESS;
}

static int write_known_host(pi1mhz_wolfssh *provider)
{
    char path[512], temporary[520], line[2400];
    FILE *input = NULL, *output = NULL;
    char buffer[512];
    int ok = -1;
    if (make_path(provider, "known_hosts", path, sizeof(path))) return -1;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) <= 0) return -1;
    {
        int count = snprintf(line, sizeof(line), "%s %s %s\n",
                             provider->host_id, provider->host_key_type,
                             provider->host_key);
        if (count <= 0 || (size_t)count >= sizeof(line)) return -1;
    }
    input = fopen(path, "rb");
    output = fopen(temporary, "wb");
    if (!output) goto done;
    if (input) {
        size_t count;
        while ((count = fread(buffer, 1, sizeof(buffer), input)) != 0)
            if (fwrite(buffer, 1, count, output) != count) goto done;
        if (ferror(input)) goto done;
    }
    if (fwrite(line, 1, strlen(line), output) != strlen(line) ||
        fflush(output) || fsync(fileno(output))) goto done;
    if (fclose(output)) { output = NULL; goto done; }
    output = NULL;
    if (rename(temporary, path)) goto done;
    ok = 0;
done:
    if (input) fclose(input);
    if (output) fclose(output);
    if (ok) unlink(temporary);
    return ok;
}

static int check_known_host(pi1mhz_wolfssh *provider)
{
    char path[512], line[2600], host[192], type[64], key[2048];
    FILE *file;
    if (make_path(provider, "known_hosts", path, sizeof(path))) return -1;
    file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? 0 : -1;
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "%191s %63s %2047s", host, type, key) == 3 &&
            !strcmp(host, provider->host_id)) {
            fclose(file);
            return !strcmp(type, provider->host_key_type) &&
                   !strcmp(key, provider->host_key) ? 1 : -1;
        }
    }
    fclose(file);
    return 0;
}

static int hostkey_callback(const byte *public_key, word32 public_key_size,
                            void *opaque)
{
    pi1mhz_wolfssh *provider = opaque;
    byte digest[WC_SHA256_DIGEST_SIZE];
    byte encoded[64];
    word32 encoded_size = sizeof(encoded);
    word32 host_key_size = sizeof(provider->host_key) - 1u;
    word32 name_size;
    int known;
    name_size = public_key_size >= 4 ? ((word32)public_key[0] << 24) |
                ((word32)public_key[1] << 16) |
                ((word32)public_key[2] << 8) | public_key[3] : 0;
    if (!name_size || name_size >= sizeof(provider->host_key_type) ||
        name_size > public_key_size - 4u ||
        Base64_Encode_NoNl(public_key, public_key_size,
                           (byte *)provider->host_key, &host_key_size) != 0)
        return -1;
    provider->host_key[host_key_size] = 0;
    memcpy(provider->host_key_type, public_key + 4, name_size);
    provider->host_key_type[name_size] = 0;
    if (debug_enabled()) {
        fprintf(stderr, "host key algorithm %s blob=%u\n",
                provider->host_key_type, (unsigned)public_key_size);
    }
    if (wc_Sha256Hash(public_key, public_key_size, digest) != 0 ||
        Base64_Encode_NoNl(digest, sizeof(digest), encoded, &encoded_size) != 0 ||
        encoded_size + 8 >= sizeof(provider->fingerprint)) return -1;
    /* OpenSSH's SHA256 fingerprint spelling omits Base64 padding. */
    while (encoded_size && encoded[encoded_size - 1] == '=') encoded_size--;
    memcpy(provider->fingerprint, "SHA256:", 7);
    memcpy(provider->fingerprint + 7, encoded, encoded_size);
    provider->fingerprint[7 + encoded_size] = 0;
    known = check_known_host(provider);
    if (debug_enabled())
        fprintf(stderr, "host key %s %s known=%d trust=%d\n",
                provider->host_id, provider->fingerprint, known,
                provider->trust_unknown);
    if (known > 0) return 0;
    if (known < 0) { provider->host_changed = 1; return -1; }
    if (!provider->trust_unknown) { provider->host_unknown = 1; return -1; }
    if (write_known_host(provider)) return -1;
    return 0;
}

static void free_keys(pi1mhz_wolfssh *provider)
{
    if (provider->private_key) {
        memset(provider->private_key, 0, provider->private_key_size);
        free(provider->private_key);
    }
    if (provider->public_key) free(provider->public_key);
    provider->private_key = NULL; provider->public_key = NULL;
    provider->private_key_size = provider->public_key_size = 0;
}

static void clear_password(pi1mhz_wolfssh *provider)
{
    volatile byte *p = provider->password;
    word32 count = sizeof(provider->password);
    while (count-- != 0u) *p++ = 0;
    provider->password_size = 0;
}

static void reset_connection(pi1mhz_wolfssh *provider, int keep_password)
{
    byte saved_password[128];
    word32 saved_size = 0;
    if (!provider) return;
    if (keep_password && provider->password_size) {
        saved_size = provider->password_size;
        memcpy(saved_password, provider->password, saved_size);
    }
    if (provider->ssh) {
        if (provider->stage == PROVIDER_UP) (void)wolfSSH_shutdown(provider->ssh);
        wolfSSH_free(provider->ssh);
    }
    if (provider->ctx) wolfSSH_CTX_free(provider->ctx);
    if (provider->socket_fd >= 0) close(provider->socket_fd);
    free_keys(provider);
    provider->ssh = NULL; provider->ctx = NULL; provider->socket_fd = -1;
    provider->stage = PROVIDER_IDLE;
    clear_password(provider);
    if (saved_size) {
        memcpy(provider->password, saved_password, saved_size);
        provider->password_size = saved_size;
        memset(saved_password, 0, sizeof(saved_password));
    }
}

void pi1mhz_wolfssh_close(pi1mhz_wolfssh *provider)
{
    reset_connection(provider, 0);
}

pi1mhz_wolfssh *pi1mhz_wolfssh_create(const char *storage_directory)
{
    pi1mhz_wolfssh *provider = calloc(1, sizeof(*provider));
    if (!provider || !storage_directory ||
        strlen(storage_directory) >= sizeof(provider->directory)) {
        free(provider); return NULL;
    }
    strcpy(provider->directory, storage_directory);
    provider->socket_fd = -1;
    if (wolfSSH_Init() != WS_SUCCESS) { free(provider); return NULL; }
    /* A peer can close between wolfSSH's non-blocking receive and its next
       protocol write.  POSIX would otherwise terminate the whole emulator
       with SIGPIPE.  Bare-metal Pi1MHz reports this as EOF/error through the
       mailbox, so make the emulator follow the same observable contract. */
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) { free(provider); return NULL; }
    /* wolfSSH obtains the PTY name from TERM on POSIX. The mailbox contract is
       explicitly VT100, independent of the emulator process's own terminal. */
    if (setenv("TERM", "vt100", 1)) { free(provider); return NULL; }
    return provider;
}

void pi1mhz_wolfssh_destroy(pi1mhz_wolfssh *provider)
{
    pi1mhz_wolfssh_close(provider);
    free(provider);
    (void)wolfSSH_Cleanup();
}

static uint8_t begin_tcp(pi1mhz_wolfssh *provider, const char *url,
                         const char *username, int trust_unknown)
{
    char port[16];
    struct addrinfo hints, *addresses = NULL, *address;
    if (parse_url(url, provider->host, sizeof(provider->host), port,
                  sizeof(port)) || !*username ||
        strlen(username) >= sizeof(provider->username)) return NTS_ERR_PARAM;
    strcpy(provider->username, username);
    if (!strcmp(port, "22")) {
        if (strlen(provider->host) >= sizeof(provider->host_id))
            return NTS_ERR_PARAM;
        strcpy(provider->host_id, provider->host);
    } else {
        int count = snprintf(provider->host_id, sizeof(provider->host_id),
                             "[%s]:%s", provider->host, port);
        if (count <= 0 || (size_t)count >= sizeof(provider->host_id))
            return NTS_ERR_PARAM;
    }
    provider->trust_unknown = trust_unknown;
    provider->host_unknown = provider->host_changed = 0;
    provider->fingerprint[0] = 0;
    if (load_keys(provider)) {
        free_keys(provider);
        if (!provider->password_size) return NTS_AUTH_FAILED;
    }
    memset(&hints, 0, sizeof(hints)); hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(provider->host, port, &hints, &addresses)) return NTS_ERR_DNS;
    for (address = addresses; address; address = address->ai_next) {
        int flags;
        provider->socket_fd = socket(address->ai_family, address->ai_socktype,
                                     address->ai_protocol);
        if (provider->socket_fd < 0) continue;
        flags = fcntl(provider->socket_fd, F_GETFL, 0);
        if (flags >= 0) fcntl(provider->socket_fd, F_SETFL, flags | O_NONBLOCK);
        if (!connect(provider->socket_fd, address->ai_addr, address->ai_addrlen) ||
            errno == EINPROGRESS) break;
        close(provider->socket_fd); provider->socket_fd = -1;
    }
    freeaddrinfo(addresses);
    if (provider->socket_fd < 0) return NTS_ERR_CONN;
    provider->stage = PROVIDER_TCP;
    return NTS_PENDING;
}

static uint8_t start_ssh(pi1mhz_wolfssh *provider)
{
    int error = 0; socklen_t size = sizeof(error);
    if (getsockopt(provider->socket_fd, SOL_SOCKET, SO_ERROR, &error, &size) ||
        error) return error == EINPROGRESS ? NTS_PENDING : NTS_ERR_CONN;
    provider->ctx = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_CLIENT, NULL);
    if (!provider->ctx) return NTS_ERR_CONN;
    wolfSSH_SetUserAuth(provider->ctx, auth_callback);
    wolfSSH_CTX_SetPublicKeyCheck(provider->ctx, hostkey_callback);
    provider->ssh = wolfSSH_new(provider->ctx);
    if (!provider->ssh ||
        wolfSSH_SetUsername(provider->ssh, provider->username) != WS_SUCCESS ||
        wolfSSH_SetChannelType(provider->ssh, WOLFSSH_SESSION_TERMINAL,
                               NULL, 0) != WS_SUCCESS ||
        wolfSSH_set_fd(provider->ssh, provider->socket_fd) != WS_SUCCESS)
        return NTS_ERR_CONN;
    wolfSSH_SetUserAuthCtx(provider->ssh, provider);
    wolfSSH_SetPublicKeyCheckCtx(provider->ssh, provider);
    provider->stage = PROVIDER_SSH;
    return NTS_PENDING;
}

uint8_t pi1mhz_wolfssh_open(pi1mhz_wolfssh *provider, const char *url,
                            const char *username, int trust_unknown,
                            char fingerprint[96])
{
    int result;
    uint8_t status;
    if (!provider || !fingerprint) return NTS_ERR_PARAM;
    if (provider->stage == PROVIDER_IDLE) {
        status = begin_tcp(provider, url, username, trust_unknown);
        if (status != NTS_PENDING) pi1mhz_wolfssh_close(provider);
        return status;
    }
    if (provider->stage == PROVIDER_TCP) {
        status = start_ssh(provider);
        if (status != NTS_PENDING) pi1mhz_wolfssh_close(provider);
        return status;
    }
    if (provider->stage == PROVIDER_UP) return NTS_OK;
    if (provider->stage == PROVIDER_RESIZE) {
        result = wolfSSH_ChangeTerminalSize(provider->ssh, 40, 24, 0, 0);
        if (result == WS_SUCCESS) { provider->stage = PROVIDER_UP; return NTS_OK; }
        if (want_io(provider, result)) return NTS_PENDING;
        pi1mhz_wolfssh_close(provider);
        return NTS_ERR_PROTOCOL;
    }
    result = wolfSSH_connect(provider->ssh);
    if (result == WS_SUCCESS) {
        free_keys(provider);
        clear_password(provider);
        provider->stage = PROVIDER_RESIZE;
        return NTS_PENDING;
    }
    if (want_io(provider, result)) return NTS_PENDING;
    if (debug_enabled())
        fprintf(stderr, "wolfSSH_connect=%d (%s), session=%d (%s)\n",
                result, wolfSSH_ErrorToName(result),
                wolfSSH_get_error(provider->ssh),
                wolfSSH_get_error_name(provider->ssh));
    strncpy(fingerprint, provider->fingerprint, 95); fingerprint[95] = 0;
    status = provider->host_unknown ? NTS_HOSTKEY_UNKNOWN :
             (provider->host_changed ? NTS_ERR_PROTOCOL : NTS_AUTH_FAILED);
    reset_connection(provider, status == NTS_HOSTKEY_UNKNOWN);
    return status;
}

int pi1mhz_wolfssh_password(pi1mhz_wolfssh *provider,
                            const uint8_t *password, size_t length)
{
    if (!provider || !password || provider->stage != PROVIDER_IDLE ||
        length == 0 || length > sizeof(provider->password)) return -1;
    clear_password(provider);
    memcpy(provider->password, password, length);
    provider->password_size = (word32)length;
    return 0;
}

int pi1mhz_wolfssh_read(pi1mhz_wolfssh *provider, uint8_t *out, size_t maximum)
{
    int result, error;
    if (!provider || provider->stage != PROVIDER_UP || maximum > UINT32_MAX)
        return -(int)NTS_ERR_CONN;
    result = wolfSSH_stream_read(provider->ssh, out, (word32)maximum);
    if (result >= 0) return result;
    if (want_io(provider, result)) return 0;
    error = wolfSSH_get_error(provider->ssh);
    if (debug_enabled())
        fprintf(stderr, "wolfSSH_stream_read=%d (%s), session=%d (%s)\n",
                result, wolfSSH_ErrorToName(result), error,
                wolfSSH_get_error_name(provider->ssh));
    if (result == WS_EOF || result == WS_CHANNEL_CLOSED ||
        result == WS_SOCKET_ERROR_E ||
        error == WS_EOF || error == WS_CHANNEL_CLOSED ||
        error == WS_SOCKET_ERROR_E)
        return -(int)NTS_EOF;
    return -(int)NTS_ERR_PROTOCOL;
}

int pi1mhz_wolfssh_write(pi1mhz_wolfssh *provider, const uint8_t *data,
                         size_t length)
{
    int result;
    if (!provider || provider->stage != PROVIDER_UP || length > UINT32_MAX)
        return -(int)NTS_ERR_CONN;
    result = wolfSSH_stream_send(provider->ssh, (byte *)(uintptr_t)data,
                                 (word32)length);
    if (result >= 0) return result;
    return want_io(provider, result) ? 0 : -(int)NTS_ERR_PROTOCOL;
}
