/*
 * stream_linux.c - the bridge's "UART" as a pseudo-terminal, for testing on a PC.
 *
 * Prints the device path at start-up and links it at $AUVCAN_BRIDGE_TTY
 * (default /tmp/auvcan_bridge_tty), so the host tools can open it like an adapter:
 *   python tools/bench.py monitor /tmp/auvcan_bridge_tty
 */
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600
#include "stream.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

static int s_master = -1, s_slave = -1;
static uint32_t s_errors;
static bridge_stream_t s_stream;

static size_t l_read(void *ctx, uint8_t *buf, size_t max)
{
    (void)ctx;
    const ssize_t r = read(s_master, buf, max);
    return r > 0 ? (size_t)r : 0u;
}

static size_t l_room(void *ctx) { (void)ctx; return 4096u; }

static void l_write(void *ctx, const char *s, size_t n)
{
    (void)ctx;
    while (n > 0) {
        const ssize_t w = write(s_master, s, n);
        if (w <= 0) { if (errno != EINTR) { s_errors++; return; } continue; }
        s += w; n -= (size_t)w;
    }
}

static void l_poll(void *ctx) { (void)ctx; }
static uint32_t l_errors(void *ctx) { (void)ctx; return s_errors; }

const bridge_stream_t *stream_init(void)
{
    s_master = posix_openpt(O_RDWR | O_NOCTTY);
    if (s_master < 0 || grantpt(s_master) != 0 || unlockpt(s_master) != 0) return NULL;
    const char *name = ptsname(s_master);
    if (name == NULL) return NULL;

    /* Hold the slave open in raw mode: no echo of our own output back to us before
     * the host opens it, and no EIO on the master between host sessions. */
    s_slave = open(name, O_RDWR | O_NOCTTY);
    if (s_slave < 0) return NULL;
    struct termios t;
    tcgetattr(s_slave, &t);
    // cfmakeraw(&t);
    // raw mode, spelled out (cfmakeraw() isn't visible on macOS with _XOPEN_SOURCE)
    t.c_iflag &= ~(tcflag_t)(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    t.c_oflag &= ~(tcflag_t)OPOST;
    t.c_lflag &= ~(tcflag_t)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t.c_cflag &= ~(tcflag_t)(CSIZE | PARENB);
    t.c_cflag |= CS8;
    tcsetattr(s_slave, TCSANOW, &t);

    fcntl(s_master, F_SETFL, fcntl(s_master, F_GETFL) | O_NONBLOCK);

    const char *link = getenv("AUVCAN_BRIDGE_TTY");
    if (link == NULL) link = "/tmp/auvcan_bridge_tty";
    unlink(link);
    if (symlink(name, link) != 0) perror("symlink");
    printf("[slcan_bridge] host side: %s  (linked at %s)\n", name, link);
    fflush(stdout);

    s_stream = (bridge_stream_t){ .ctx = NULL, .read = l_read, .room = l_room, .write = l_write,
                                  .poll = l_poll, .errors = l_errors };
    return &s_stream;
}
