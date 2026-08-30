/*
 * Control server for the external debug GUI.
 *
 * The GUI is a separate process (tools/tm2_debug_gui) so that nothing about
 * it can touch this process's renderer, OpenGL context or SDL event loop.
 * This file is the game-side half: a tiny line-oriented TCP server bound to
 * localhost, polled from the plugin's VBlank callback.
 *
 * Everything here is non-blocking, by necessity. The poll runs on the
 * emulation thread at guest VBlank, so a single blocking accept(), recv() or
 * send() would stall the game. Partial sends are buffered per client and
 * retried on later frames rather than looped on.
 *
 * Protocol -- newline-delimited text, not JSON, because the peer is ours and
 * a hand-rolled JSON parser in C would be the largest and least reliable part
 * of this file. Requests:
 *
 *   LIST                 OK <count>, then <count> tab-separated rows:
 *                        <index> <enabled> <region> <category> <name> <id>
 *   SET <index> <0|1>    OK
 *   TOGGLE <index>       OK <enabled>
 *   CLEAR                OK
 *   STATUS               OK <active> <total>
 *   PING                 OK
 *
 * Anything unrecognised gets "ERR <reason>". Port defaults to 4371 (the
 * framework's own debug server owns 4370) and is overridable with
 * TM2_DEBUG_GUI_PORT.
 */

#include "tm2_debug_state.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef SOCKET tm2_sock;
#  define TM2_INVALID   INVALID_SOCKET
#  define tm2_close      closesocket
#  define tm2_would_block() (WSAGetLastError() == WSAEWOULDBLOCK)
#else
#  include <arpa/inet.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
   typedef int tm2_sock;
#  define TM2_INVALID   (-1)
#  define tm2_close      close
#  define tm2_would_block() (errno == EAGAIN || errno == EWOULDBLOCK)
#endif

#define TM2_DEFAULT_PORT 4371
#define TM2_MAX_CLIENTS  4
#define TM2_IN_CAP       512
#define TM2_OUT_CAP      65536

typedef struct {
    tm2_sock sock;
    char     in[TM2_IN_CAP];
    size_t   in_len;
    char    *out;         /* heap: a LIST reply is ~12 KB */
    size_t   out_len;
    size_t   out_sent;
} Tm2Client;

static tm2_sock  s_listen = TM2_INVALID;
static Tm2Client s_clients[TM2_MAX_CLIENTS];
static int       s_started;

static void set_nonblocking(tm2_sock s)
{
#if defined(_WIN32)
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    int fl = fcntl(s, F_GETFL, 0);
    if (fl >= 0) fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
}

static void client_reset(Tm2Client *c)
{
    if (c->sock != TM2_INVALID) tm2_close(c->sock);
    c->sock = TM2_INVALID;
    c->in_len = 0;
    free(c->out);
    c->out = NULL;
    c->out_len = 0;
    c->out_sent = 0;
}

/* Queue a reply. Replies are small and rare, so growing the buffer by
 * reallocation is fine; a client that will not drain is dropped instead of
 * being allowed to consume memory without bound. */
static void client_write(Tm2Client *c, const char *text, size_t len)
{
    if (c->sock == TM2_INVALID) return;
    if (c->out_len + len > TM2_OUT_CAP) {
        client_reset(c);
        return;
    }
    char *grown = (char *)realloc(c->out, c->out_len + len);
    if (!grown) {
        client_reset(c);
        return;
    }
    c->out = grown;
    memcpy(c->out + c->out_len, text, len);
    c->out_len += len;
}

static void client_printf(Tm2Client *c, const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n > 0) client_write(c, line, (size_t)n);
}

static void client_flush(Tm2Client *c)
{
    while (c->sock != TM2_INVALID && c->out_sent < c->out_len) {
        int n = (int)send(c->sock, c->out + c->out_sent,
                          (int)(c->out_len - c->out_sent), 0);
        if (n > 0) {
            c->out_sent += (size_t)n;
            continue;
        }
        if (n < 0 && tm2_would_block()) return;   /* retry next VBlank */
        client_reset(c);
        return;
    }
    if (c->sock != TM2_INVALID && c->out_sent == c->out_len && c->out_len) {
        free(c->out);
        c->out = NULL;
        c->out_len = 0;
        c->out_sent = 0;
    }
}

static void handle_line(Tm2Client *c, char *line)
{
    while (*line == ' ') line++;
    char *nl = strpbrk(line, "\r\n");
    if (nl) *nl = '\0';
    if (!*line) return;

    if (strcmp(line, "PING") == 0) {
        client_printf(c, "OK\n");
        return;
    }
    if (strcmp(line, "STATUS") == 0) {
        client_printf(c, "OK %d %d\n", tm2_cheat_active_count(),
                      tm2_cheat_count());
        return;
    }
    if (strcmp(line, "CLEAR") == 0) {
        tm2_cheat_clear_all();
        client_printf(c, "OK\n");
        return;
    }
    if (strcmp(line, "LIST") == 0) {
        int n = tm2_cheat_count();
        client_printf(c, "OK %d\n", n);
        for (int i = 0; i < n; i++) {
            const Tm2Cheat *ch = tm2_cheat_at(i);
            const char *reg = ch->region == TM2_REGION_IMAGE ? "EXE"
                            : ch->region == TM2_REGION_LOW   ? "LVL" : "RAM";
            client_printf(c, "%d\t%d\t%s\t%s\t%s\t%s\n", i,
                          tm2_cheat_is_enabled(i), reg, ch->category,
                          ch->name, ch->id);
        }
        return;
    }
    if (strncmp(line, "SET ", 4) == 0) {
        int idx = -1, on = 0;
        if (sscanf(line + 4, "%d %d", &idx, &on) != 2 ||
            idx < 0 || idx >= tm2_cheat_count()) {
            client_printf(c, "ERR bad SET\n");
            return;
        }
        tm2_cheat_set_enabled(idx, on ? 1 : 0);
        client_printf(c, "OK\n");
        return;
    }
    if (strncmp(line, "TOGGLE ", 7) == 0) {
        int idx = -1;
        if (sscanf(line + 7, "%d", &idx) != 1 ||
            idx < 0 || idx >= tm2_cheat_count()) {
            client_printf(c, "ERR bad TOGGLE\n");
            return;
        }
        int now = !tm2_cheat_is_enabled(idx);
        tm2_cheat_set_enabled(idx, now);
        client_printf(c, "OK %d\n", now);
        return;
    }
    client_printf(c, "ERR unknown command\n");
}

static void client_read(Tm2Client *c)
{
    for (;;) {
        if (c->in_len >= TM2_IN_CAP - 1) {   /* no newline in a full buffer */
            client_reset(c);
            return;
        }
        int n = (int)recv(c->sock, c->in + c->in_len,
                          (int)(TM2_IN_CAP - 1 - c->in_len), 0);
        if (n == 0) { client_reset(c); return; }
        if (n < 0) {
            if (tm2_would_block()) break;
            client_reset(c);
            return;
        }
        c->in_len += (size_t)n;

        for (;;) {
            char *nl = (char *)memchr(c->in, '\n', c->in_len);
            if (!nl) break;
            *nl = '\0';
            handle_line(c, c->in);
            if (c->sock == TM2_INVALID) return;
            size_t used = (size_t)(nl - c->in) + 1;
            memmove(c->in, c->in + used, c->in_len - used);
            c->in_len -= used;
        }
    }
}

void tm2_ipc_start(void)
{
    if (s_started) return;
    for (int i = 0; i < TM2_MAX_CLIENTS; i++) s_clients[i].sock = TM2_INVALID;

#if defined(_WIN32)
    /* Refcounted; the framework's own debug server may already have called
     * this, and a second call is harmless. */
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    int port = TM2_DEFAULT_PORT;
    const char *env = getenv("TM2_DEBUG_GUI_PORT");
    if (env && *env) {
        int v = atoi(env);
        if (v > 0 && v < 65536) port = v;
    }

    s_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen == TM2_INVALID) {
        fprintf(stderr, "tm2.debug: control socket unavailable\n");
        return;
    }
    int yes = 1;
    setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes,
               sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  /* localhost only */

    if (bind(s_listen, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(s_listen, TM2_MAX_CLIENTS) != 0) {
        fprintf(stderr, "tm2.debug: cannot listen on 127.0.0.1:%d "
                        "(GUI will not connect)\n", port);
        tm2_close(s_listen);
        s_listen = TM2_INVALID;
        return;
    }
    set_nonblocking(s_listen);
    s_started = 1;
    fprintf(stdout, "tm2.debug: control server on 127.0.0.1:%d\n", port);
}

void tm2_ipc_poll(void)
{
    if (!s_started || s_listen == TM2_INVALID) return;

    for (;;) {
        tm2_sock cs = accept(s_listen, NULL, NULL);
        if (cs == TM2_INVALID) break;
        set_nonblocking(cs);
        int slot = -1;
        for (int i = 0; i < TM2_MAX_CLIENTS; i++)
            if (s_clients[i].sock == TM2_INVALID) { slot = i; break; }
        if (slot < 0) { tm2_close(cs); continue; }
        s_clients[slot].sock = cs;
        s_clients[slot].in_len = 0;
    }

    for (int i = 0; i < TM2_MAX_CLIENTS; i++) {
        if (s_clients[i].sock == TM2_INVALID) continue;
        client_read(&s_clients[i]);
        if (s_clients[i].sock != TM2_INVALID) client_flush(&s_clients[i]);
    }
}

void tm2_ipc_stop(void)
{
    for (int i = 0; i < TM2_MAX_CLIENTS; i++) client_reset(&s_clients[i]);
    if (s_listen != TM2_INVALID) tm2_close(s_listen);
    s_listen = TM2_INVALID;
    s_started = 0;
}
