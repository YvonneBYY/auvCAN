#include "slcan.h"

#include <string.h>

static const char HEX[] = "0123456789ABCDEF";

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool parse_hex(const char *p, uint8_t n, uint32_t *out)
{
    uint32_t v = 0;
    for (uint8_t i = 0; i < n; i++) {
        const int h = hexval(p[i]);
        if (h < 0) return false;
        v = (v << 4) | (uint32_t)h;
    }
    *out = v;
    return true;
}

static void reply(slcan_t *s, const char *text)
{
    s->io.write(s->io.ctx, text, strlen(text));
}

static void ok(slcan_t *s)    { reply(s, "\r"); }
static void fail(slcan_t *s)  { s->errors++; reply(s, "\a"); }

void slcan_init(slcan_t *s, const slcan_io_t *io)
{
    memset(s, 0, sizeof(*s));
    s->io = *io;
}

void slcan_note(slcan_t *s, uint8_t flags)
{
    s->flags |= flags;
}

size_t slcan_format(const CanardCANFrame *f, char out[SLCAN_FRAME_TEXT])
{
    const uint32_t id = f->id & CANARD_CAN_EXT_ID_MASK;
    const uint8_t dlc = (f->data_len > 8u) ? 8u : f->data_len;
    size_t n = 0;
    out[n++] = 'T';
    for (int shift = 28; shift >= 0; shift -= 4) out[n++] = HEX[(id >> shift) & 0xFu];
    out[n++] = HEX[dlc];
    for (uint8_t i = 0; i < dlc; i++) {
        out[n++] = HEX[f->data[i] >> 4];
        out[n++] = HEX[f->data[i] & 0xFu];
    }
    out[n++] = '\r';
    return n;
}

static void cmd_transmit(slcan_t *s)
{
    /* T iiiiiiii l dd...  (len = 10 + 2*dlc) */
    uint32_t id, dlc;
    if (!s->open || s->len < 10 || !parse_hex(&s->line[1], 8, &id) || id > CANARD_CAN_EXT_ID_MASK ||
        !parse_hex(&s->line[9], 1, &dlc) || dlc > 8u || s->len != 10u + 2u * dlc) {
        fail(s);
        return;
    }
    CanardCANFrame f;
    memset(&f, 0, sizeof(f));
    f.id = id | CANARD_CAN_FRAME_EFF;
    f.data_len = (uint8_t)dlc;
    for (uint32_t i = 0; i < dlc; i++) {
        uint32_t b;
        if (!parse_hex(&s->line[10 + 2 * i], 2, &b)) { fail(s); return; }
        f.data[i] = (uint8_t)b;
    }
    const int r = s->io.send_frame(s->io.ctx, &f);
    if (r == 1) {
        s->tx_frames++;
        reply(s, "Z\r");
    } else {
        if (r == 0) s->flags |= SLCAN_F_TX_FULL;
        fail(s);
    }
}

static void execute(slcan_t *s)
{
    if (s->overlong) { s->overlong = false; fail(s); return; }
    if (s->len == 0) { ok(s); return; }

    const char c = s->line[0];
    switch (c) {
    case 'S':
        if (s->open || s->len != 2 || s->line[1] != '8') fail(s);   /* 1 Mbit/s only */
        else ok(s);
        return;
    case 's': fail(s); return;                                      /* raw bit timing: no */
    case 'O': if (s->open) fail(s); else { s->open = true; ok(s); } return;
    case 'C': s->open = false; ok(s); return;                       /* always OK, even if closed */
    case 'L': fail(s); return;
    case 'T': cmd_transmit(s); return;
    case 't': case 'r': case 'R': case 'D': case 'd': fail(s); return;
    case 'F': {
        uint8_t f = s->flags;
        if (s->io.port_flags != NULL) f |= s->io.port_flags(s->io.ctx);
        s->flags = 0;
        char out[5] = { 'F', HEX[f >> 4], HEX[f & 0xFu], '\r', 0 };
        reply(s, out);
        return;
    }
    case 'V': reply(s, "V1013\r"); return;
    case 'v': reply(s, "vAUVCAN\r"); return;
    case 'N': reply(s, "NAUV1\r"); return;
    case 'Z': case 'M': case 'm': case 'Q': case 'W': case 'X': ok(s); return;
    default:  fail(s); return;
    }
}

void slcan_feed(slcan_t *s, const uint8_t *data, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        const char c = (char)data[i];
        if (c == '\r') {
            execute(s);
            s->len = 0;
        } else if (c == '\n') {
            /* pydronecan's CLI sends "\r\n": ignore the '\n' */
        } else if (s->len < SLCAN_MAX_LINE) {
            s->line[s->len++] = c;
        } else {
            s->overlong = true;
        }
    }
}
