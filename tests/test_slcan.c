/*
 * test_slcan.c - the SLCAN codec in boards/slcan_bridge/slcan.c, no hardware.
 *   ./build/test_slcan
 */
#include <stdio.h>
#include <string.h>

#include "slcan.h"

static int g_fail, g_pass;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static char   out[512];
static size_t out_len;
static CanardCANFrame sent[8];
static int    n_sent;
static int    send_result = 1;
static uint8_t flags_now;

static int  t_send(void *c, const CanardCANFrame *f) { (void)c; if (send_result == 1 && n_sent < 8) sent[n_sent++] = *f; return send_result; }
static void t_write(void *c, const char *s, size_t n) { (void)c; memcpy(out + out_len, s, n); out_len += n; out[out_len] = 0; }
static uint8_t t_flags(void *c) { (void)c; return flags_now; }

static slcan_t S;
static void reset(void)
{
    const slcan_io_t io = { NULL, t_send, t_write, t_flags };
    slcan_init(&S, &io);
    out_len = 0; out[0] = 0; n_sent = 0; send_result = 1; flags_now = 0;
}
static const char *cmd(const char *text)      /* feed text, return what came back */
{
    out_len = 0; out[0] = 0;
    slcan_feed(&S, (const uint8_t *)text, strlen(text));
    return out;
}

static void test_pydronecan_init(void)
{
    printf("pydronecan's init sequence\n");
    reset();
    CHECK(strcmp(cmd("\r"), "\r") == 0);              /* empty command */
    CHECK(strcmp(cmd("C\r"), "\r") == 0);             /* close while closed: still OK */
    CHECK(strcmp(cmd("S8\r"), "\r") == 0);            /* 1 Mbit/s */
    CHECK(strcmp(cmd("O\r"), "\r") == 0);
    CHECK(S.open);
    CHECK(strcmp(cmd("F\r"), "F00\r") == 0);
}

static void test_bitrate_and_modes(void)
{
    printf("bit rate and modes\n");
    reset();
    CHECK(strcmp(cmd("S6\r"), "\a") == 0);            /* 500 k: not supported */
    CHECK(strcmp(cmd("s011C\r"), "\a") == 0);
    CHECK(strcmp(cmd("L\r"), "\a") == 0);             /* listen-only: no */
    CHECK(strcmp(cmd("O\r"), "\r") == 0);
    CHECK(strcmp(cmd("S8\r"), "\a") == 0);            /* bit rate while open: no */
    CHECK(strcmp(cmd("O\r"), "\a") == 0);             /* already open */
    CHECK(strcmp(cmd("C\r"), "\r") == 0 && !S.open);
    CHECK(strcmp(cmd("V\r"), "V1013\r") == 0);
    CHECK(strcmp(cmd("N\r"), "NAUV1\r") == 0);
    CHECK(strcmp(cmd("Z1\r"), "\r") == 0);            /* timestamps: accepted, ignored */
    CHECK(strcmp(cmd("?\r"), "\a") == 0);
}

static void test_transmit(void)
{
    printf("host -> bus (T)\n");
    reset();
    CHECK(strcmp(cmd("T005078013900007\r"), "\a") == 0 && n_sent == 0);    /* channel closed */
    cmd("O\r");
    /* the Kill frame: ID 0x00507801, 4 bytes 90 00 07 + tail C0 */
    CHECK(strcmp(cmd("T0050780149000 07C0\r"), "\a") == 0);               /* bad hex (space) */
    CHECK(strcmp(cmd("T005078014900007c0\r"), "Z\r") == 0);                /* lowercase hex OK */
    CHECK(n_sent == 1);
    CHECK(sent[0].id == (0x00507801u | CANARD_CAN_FRAME_EFF));
    CHECK(sent[0].data_len == 4 && sent[0].data[0] == 0x90 && sent[0].data[3] == 0xC0);
    CHECK(strcmp(cmd("T1FFFFFFF0\r"), "Z\r") == 0 && sent[1].data_len == 0);   /* max ID, DLC 0 */
    CHECK(strcmp(cmd("T200000000\r"), "\a") == 0);                        /* ID > 29 bits */
    CHECK(strcmp(cmd("T0000000190\r"), "\a") == 0);                       /* DLC 9 */
    CHECK(strcmp(cmd("T00000001200\r"), "\a") == 0);                      /* DLC 2, 1 byte */
    CHECK(strcmp(cmd("t1230\r"), "\a") == 0);                             /* standard frame: no */
    send_result = 0;
    CHECK(strcmp(cmd("T000000010\r"), "\a") == 0);                        /* TX full */
    CHECK(strcmp(cmd("F\r"), "F02\r") == 0);                              /* ...reported once */
    CHECK(strcmp(cmd("F\r"), "F00\r") == 0);
}

static void test_format(void)
{
    printf("bus -> host (format)\n");
    CanardCANFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0x00507801u | CANARD_CAN_FRAME_EFF;
    f.data_len = 4; f.data[0] = 0x90; f.data[1] = 0x00; f.data[2] = 0x07; f.data[3] = 0xC0;
    char line[SLCAN_FRAME_TEXT + 1];
    size_t n = slcan_format(&f, line); line[n] = 0;
    CHECK(strcmp(line, "T005078014900007C0\r") == 0);
    f.data_len = 8; memset(f.data, 0xAB, 8);
    n = slcan_format(&f, line); line[n] = 0;
    CHECK(n == SLCAN_FRAME_TEXT && line[n - 1] == '\r');
    f.data_len = 0;
    n = slcan_format(&f, line); line[n] = 0;
    CHECK(strcmp(line, "T005078010\r") == 0);

    /* round trip: format -> feed -> same frame */
    reset(); cmd("O\r");
    f.data_len = 8; for (int i = 0; i < 8; i++) f.data[i] = (uint8_t)(i * 37);
    n = slcan_format(&f, line); line[n] = 0;
    CHECK(strcmp(cmd(line), "Z\r") == 0);
    CHECK(n_sent == 1 && sent[0].id == f.id && memcmp(sent[0].data, f.data, 8) == 0);
}

static void test_framing(void)
{
    printf("line framing\n");
    reset();
    /* split across feeds, CRLF from pydronecan's CLI, an overlong line */
    CHECK(strcmp(cmd("S"), "") == 0);
    CHECK(strcmp(cmd("8\r\n"), "\r") == 0);
    CHECK(strcmp(cmd("T0000000180000000000000000000000000\r"), "\a") == 0);   /* overflow */
    CHECK(strcmp(cmd("O\r"), "\r") == 0);                                     /* parser recovered */
    flags_now = SLCAN_F_ERR_PASSIVE;
    CHECK(strcmp(cmd("F\r"), "F20\r") == 0);
}

int main(void)
{
    test_pydronecan_init();
    test_bitrate_and_modes();
    test_transmit();
    test_format();
    test_framing();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
