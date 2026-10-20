#define CANARD_DSDLC_INTERNAL
#include <cuauv.PingerBearing.h>
#include <string.h>

#ifdef CANARD_DSDLC_TEST_BUILD
#include <test_helpers.h>
#endif

uint32_t _cuauv_PingerBearing_encode(struct cuauv_PingerBearing* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {
    uint32_t bit_ofs = 0;
    memset(buffer, 0, CUAUV_PINGERBEARING_MAX_SIZE);
    __cuauv_PingerBearing_encode(buffer, &bit_ofs, msg,
#if CANARD_ENABLE_TAO_OPTION
    tao
#else
    true
#endif
    );
    return ((bit_ofs+7)/8);
}

/*
  return true if the decode is invalid
 */
bool _cuauv_PingerBearing_decode(const CanardRxTransfer* transfer, struct cuauv_PingerBearing* msg) {
#if CANARD_ENABLE_TAO_OPTION
    if (transfer->tao && (transfer->payload_len > CUAUV_PINGERBEARING_MAX_SIZE)) {
        return true; /* invalid payload length */
    }
#endif
    uint32_t bit_ofs = 0;
    if (__cuauv_PingerBearing_decode(transfer, &bit_ofs, msg,
#if CANARD_ENABLE_TAO_OPTION
    transfer->tao
#else
    true
#endif
    )) {
        return true; /* invalid payload */
    }

    const uint32_t byte_len = (bit_ofs+7U)/8U;
#if CANARD_ENABLE_TAO_OPTION
    // if this could be CANFD then the dlc could indicating more bytes than
    // we actually have
    if (!transfer->tao) {
        return byte_len > transfer->payload_len;
    }
#endif
    return byte_len != transfer->payload_len;
}

#ifdef CANARD_DSDLC_TEST_BUILD
struct cuauv_PingerBearing sample_cuauv_PingerBearing_msg(void) {
    struct cuauv_PingerBearing msg;

    msg.heading_deg = random_float16_val();
    msg.elevation_deg = random_float16_val();
    msg.confidence = (uint8_t)random_bitlen_unsigned_val(8);
    msg.pinger_id = (uint8_t)random_bitlen_unsigned_val(8);
    return msg;
}
#endif
