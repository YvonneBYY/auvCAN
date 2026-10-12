/*
 * auvcan_fresh.h - "is this subscription still alive?" helper.
 *
 * One primitive covers every timeout in the auvCAN spec:
 *   - cuauv.Kill presence on actuator boards        (250 ms)
 *   - esc.RawCommand staleness on thruster boards   (200 ms)
 *   - cuauv.HostHeartbeat on the controller board   (500 ms)
 *
 * Feed it from the message handler; ask it in the main loop.
 * A monitor that has never been fed reports NOT fresh, so every
 * consumer boots in its safe state.
 */
#ifndef AUVCAN_FRESH_H
#define AUVCAN_FRESH_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t timeout_us;
    uint64_t last_us;
    bool     seen;
    bool     has_seq;
    uint8_t  last_seq;
    uint32_t expirations;   /* fresh -> stale transitions, for diagnostics */
    bool     was_fresh;
} auvcan_fresh_t;

void auvcan_fresh_init(auvcan_fresh_t *f, uint32_t timeout_ms);
void auvcan_fresh_set_timeout(auvcan_fresh_t *f, uint32_t timeout_ms);

/* Mark the subscription alive at now_us. */
void auvcan_fresh_feed(auvcan_fresh_t *f, uint64_t now_us);

/*
 * Feed only if `seq` differs from the previous message's seq.
 * A publisher stuck re-sending the same frame therefore goes stale.
 * Returns true if this was a new sequence number.
 */
bool auvcan_fresh_feed_seq(auvcan_fresh_t *f, uint64_t now_us, uint8_t seq);

/* True if fed within the timeout. Also updates the expiration counter. */
bool auvcan_fresh_ok(auvcan_fresh_t *f, uint64_t now_us);

#endif
