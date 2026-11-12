#define CANARD_DSDLC_INTERNAL
#include <cuauv.PowerStatus.h>
#include <string.h>

#ifdef CANARD_DSDLC_TEST_BUILD
#include <test_helpers.h>
#endif

uint32_t _cuauv_PowerStatus_encode(struct cuauv_PowerStatus* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {
    uint32_t bit_ofs = 0;
    memset(buffer, 0, CUAUV_POWERSTATUS_MAX_SIZE);
    __cuauv_PowerStatus_encode(buffer, &bit_ofs, msg,
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
bool _cuauv_PowerStatus_decode(const CanardRxTransfer* transfer, struct cuauv_PowerStatus* msg) {
#if CANARD_ENABLE_TAO_OPTION
    if (transfer->tao && (transfer->payload_len > CUAUV_POWERSTATUS_MAX_SIZE)) {
        return true; /* invalid payload length */
    }
#endif
    uint32_t bit_ofs = 0;
    if (__cuauv_PowerStatus_decode(transfer, &bit_ofs, msg,
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
struct cuauv_PowerStatus sample_cuauv_PowerStatus_msg(void) {
    struct cuauv_PowerStatus msg;

    msg.vin_v = random_float16_val();
    msg.vout_v = random_float16_val();
    msg.current_a = random_float16_val();
    msg.v12_v = random_float16_val();
    msg.v5_v = random_float16_val();
    msg.killed = (bool)random_bitlen_unsigned_val(1);
    msg.restart_state = (uint8_t)random_bitlen_unsigned_val(2);
    msg.overcurrent = (bool)random_bitlen_unsigned_val(1);
    return msg;
}
#endif
