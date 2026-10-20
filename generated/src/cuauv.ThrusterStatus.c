#define CANARD_DSDLC_INTERNAL
#include <cuauv.ThrusterStatus.h>
#include <string.h>

#ifdef CANARD_DSDLC_TEST_BUILD
#include <test_helpers.h>
#endif

uint32_t _cuauv_ThrusterStatus_encode(struct cuauv_ThrusterStatus* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {
    uint32_t bit_ofs = 0;
    memset(buffer, 0, CUAUV_THRUSTERSTATUS_MAX_SIZE);
    __cuauv_ThrusterStatus_encode(buffer, &bit_ofs, msg,
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
bool _cuauv_ThrusterStatus_decode(const CanardRxTransfer* transfer, struct cuauv_ThrusterStatus* msg) {
#if CANARD_ENABLE_TAO_OPTION
    if (transfer->tao && (transfer->payload_len > CUAUV_THRUSTERSTATUS_MAX_SIZE)) {
        return true; /* invalid payload length */
    }
#endif
    uint32_t bit_ofs = 0;
    if (__cuauv_ThrusterStatus_decode(transfer, &bit_ofs, msg,
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
struct cuauv_ThrusterStatus sample_cuauv_ThrusterStatus_msg(void) {
    struct cuauv_ThrusterStatus msg;

    for (size_t i=0; i < 3; i++) {
        msg.applied[i] = (int16_t)random_bitlen_signed_val(16);
    }
    msg.killed = (bool)random_bitlen_unsigned_val(1);
    msg.stale = (bool)random_bitlen_unsigned_val(1);
    return msg;
}
#endif
