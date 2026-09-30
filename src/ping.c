#include "ping.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BAD_SOCK INVALID_SOCKET
#define close_sock closesocket
#else
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
#define BAD_SOCK (-1)
#define close_sock close
#endif

#include "../third_party/cjson/cJSON.h"
#include "util.h"

#define TIMEOUT_MS 3000

void split_address(const char *address, char *host, int *port) {
    snprintf(host, 256, "%s", address);
    *port = 25565;
    char *colon = strrchr(host, ':');
    if (colon && !strchr(colon + 1, ']')) {
        int p = atoi(colon + 1);
        if (p > 0 && p < 65536) {
            *port = p;
            *colon = '\0';
        }
    }
}

static void put_varint(sbuf *b, int value) {
    unsigned v = (unsigned)value;
    do {
        unsigned char c = v & 0x7F;
        v >>= 7;
        if (v) c |= 0x80;
        sb_addn(b, (const char *)&c, 1);
    } while (v);
}

static int read_full(sock_t fd, unsigned char *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        int r = (int)recv(fd, (char *)buf + got, (int)(n - got), 0);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return 0;
}

static int read_varint(sock_t fd, int *out) {
    int value = 0;
    for (int i = 0; i < 5; i++) {
        unsigned char c;
        if (read_full(fd, &c, 1) != 0) return -1;
        value |= (c & 0x7F) << (7 * i);
        if (!(c & 0x80)) {
            *out = value;
            return 0;
        }
    }
    return -1;
}

static void set_blocking(sock_t fd, int blocking) {
#ifdef _WIN32
    u_long nb = blocking ? 0 : 1;
    ioctlsocket(fd, FIONBIO, &nb);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK);
#endif
}

static sock_t connect_timeout(const char *host, int port) {
#ifdef _WIN32
    static int wsa_ready;
    if (!wsa_ready) {
        WSADATA w;
        wsa_ready = WSAStartup(MAKEWORD(2, 2), &w) == 0;
    }
#endif
    char ports[8];
    snprintf(ports, sizeof ports, "%d", port);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, ports, &hints, &res) != 0) return BAD_SOCK;

    sock_t fd = BAD_SOCK;
    for (struct addrinfo *a = res; a && fd == BAD_SOCK; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd == BAD_SOCK) continue;
        set_blocking(fd, 0);
        int rc = connect(fd, a->ai_addr, (int)a->ai_addrlen);
        if (rc != 0) {
            /* connexion en cours : attente avec délai (select fonctionne partout) */
            fd_set wr;
            FD_ZERO(&wr);
            FD_SET(fd, &wr);
            struct timeval tv = {TIMEOUT_MS / 1000, (TIMEOUT_MS % 1000) * 1000};
            int err = 0;
            socklen_t len = sizeof err;
            if (select((int)fd + 1, NULL, &wr, NULL, &tv) == 1 && getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len) == 0 && err == 0)
                rc = 0;
        }
        if (rc != 0) {
            close_sock(fd);
            fd = BAD_SOCK;
            continue;
        }
        set_blocking(fd, 1);
#ifdef _WIN32
        DWORD tmo = TIMEOUT_MS;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof tmo);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tmo, sizeof tmo);
#else
        struct timeval tv = {TIMEOUT_MS / 1000, (TIMEOUT_MS % 1000) * 1000};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#endif
#ifdef SO_NOSIGPIPE
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    }
    freeaddrinfo(res);
    return fd;
}

int server_ping(const char *address, server_status *out) {
    memset(out, 0, sizeof *out);
    char host[256];
    int port;
    split_address(address, host, &port);

    long long t0 = mono_ms();
    sock_t fd = connect_timeout(host, port);
    if (fd == BAD_SOCK) return -1;

    /* Handshake (état 1 = statut) puis requête de statut */
    sbuf payload, packet;
    sb_init(&payload);
    put_varint(&payload, 0x00);
    put_varint(&payload, -1);
    put_varint(&payload, (int)strlen(host));
    sb_add(&payload, host);
    unsigned char pb[2] = {(unsigned char)(port >> 8), (unsigned char)port};
    sb_addn(&payload, (const char *)pb, 2);
    put_varint(&payload, 1);

    sb_init(&packet);
    put_varint(&packet, (int)payload.len);
    sb_addn(&packet, payload.s, payload.len);
    sb_addn(&packet, "\x01\x00", 2);
    int rc = send(fd, packet.s, (int)packet.len, 0) == (int)packet.len ? 0 : -1;
    sb_free(&payload);
    sb_free(&packet);

    int len = 0, id = 0, slen = 0;
    char *json = NULL;
    if (rc == 0 && read_varint(fd, &len) == 0 && read_varint(fd, &id) == 0 && id == 0 && read_varint(fd, &slen) == 0 &&
        slen > 0 && slen < (1 << 21)) {
        json = malloc((size_t)slen + 1);
        if (read_full(fd, (unsigned char *)json, (size_t)slen) == 0) json[slen] = '\0';
        else {
            free(json);
            json = NULL;
        }
    }
    close_sock(fd);
    if (!json) return -1;

    cJSON *j = cJSON_Parse(json);
    free(json);
    const cJSON *players = cJSON_GetObjectItem(j, "players");
    out->online = 1;
    const cJSON *on = cJSON_GetObjectItem(players, "online"), *max = cJSON_GetObjectItem(players, "max");
    out->players = cJSON_IsNumber(on) ? on->valueint : 0;
    out->max_players = cJSON_IsNumber(max) ? max->valueint : 0;
    out->nsample = 0;
    const cJSON *pl;
    cJSON_ArrayForEach(pl, cJSON_GetObjectItem(players, "sample")) {
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(pl, "name"));
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(pl, "id"));
        if (!name || !*name || out->nsample >= 16) continue;
        snprintf(out->sample_name[out->nsample], sizeof out->sample_name[0], "%s", name);
        snprintf(out->sample_id[out->nsample], sizeof out->sample_id[0], "%s", id ? id : "");
        out->nsample++;
    }
    const char *ver = cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetObjectItem(j, "version"), "name"));
    snprintf(out->version, sizeof out->version, "%s", ver ? ver : "");
    out->latency_ms = (int)(mono_ms() - t0);
    cJSON_Delete(j);
    return 0;
}
