#define CANARD_DSDLC_INTERNAL
#include <cuauv.HullStatus.h>
#include <string.h>

#ifdef CANARD_DSDLC_TEST_BUILD
#include <test_helpers.h>
#endif

uint32_t _cuauv_HullStatus_encode(struct cuauv_HullStatus* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {
    uint32_t bit_ofs = 0;
    memset(buffer, 0, CUAUV_HULLSTATUS_MAX_SIZE);
    __cuauv_HullStatus_encode(buffer, &bit_ofs, msg,
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
bool _cuauv_HullStatus_decode(const CanardRxTransfer* transfer, struct cuauv_HullStatus* msg) {
#if CANARD_ENABLE_TAO_OPTION
    if (transfer->tao && (transfer->payload_len > CUAUV_HULLSTATUS_MAX_SIZE)) {
        return true; /* invalid payload length */
    }
#endif
    uint32_t bit_ofs = 0;
    if (__cuauv_HullStatus_decode(transfer, &bit_ofs, msg,
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
struct cuauv_HullStatus sample_cuauv_HullStatus_msg(void) {
    struct cuauv_HullStatus msg;

    msg.hull_pressure_atm = random_float_val();
    msg.internal_temp_c = random_float16_val();
    for (size_t i=0; i < 4; i++) {
        msg.leak[i] = (bool)random_bitlen_unsigned_val(1);
    }
    msg.pressure_sensor_ok = (bool)random_bitlen_unsigned_val(1);
    return msg;
}
#endif
