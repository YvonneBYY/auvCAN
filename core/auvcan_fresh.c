/*
General timeout tracker, determines if something is being heard "recently enough
*/

#include "auvcan_fresh.h"

/*
Stores the timeout (converted to us) and clears everything, set seen = false
*/
void auvcan_fresh_init(auvcan_fresh_t *f, uint32_t timeout_ms)
{
    f->timeout_us = (uint64_t)timeout_ms * 1000u;
    f->last_us = 0u;
    f->seen = false;
    f->has_seq = false;
    f->last_seq = 0u;
    f->expirations = 0u;
    f->was_fresh = false;
}

/*
Changes timeout without resetting anything else
*/
void auvcan_fresh_set_timeout(auvcan_fresh_t *f, uint32_t timeout_ms)
{
    f->timeout_us = (uint64_t)timeout_ms * 1000u;
}

/*
Respond if “I just heard from it”
    - last_us = now, seen = true. 
Called from a message handler after the message decodes successfully
*/
void auvcan_fresh_feed(auvcan_fresh_t *f, uint64_t now_us)
{
    f->last_us = now_us;
    f->seen = true;
}

/*
Same functionality as auvcan_fresh_feed, but only if seq changes
    - catches a stuck publisher:
*/
bool auvcan_fresh_feed_seq(auvcan_fresh_t *f, uint64_t now_us, uint8_t seq)
{
    if (f->has_seq && seq == f->last_seq) {
        return false;
    }
    f->has_seq = true;
    f->last_seq = seq;
    auvcan_fresh_feed(f, now_us);
    return true;
}

bool auvcan_fresh_ok(auvcan_fresh_t *f, uint64_t now_us)
{
    /* now_us may be slightly OLDER than last_us when a handler fed us with a
     * fresher clock reading than the caller's; that is "just fed", not stale
     * (unsigned subtraction would wrap to ~2^64 and read as expired). */
    const uint64_t age = (now_us > f->last_us) ? (now_us - f->last_us) : 0u;
    const bool ok = f->seen && age <= f->timeout_us;
    if (f->was_fresh && !ok) {
        f->expirations++;
    }
    f->was_fresh = ok;
    return ok;
}
