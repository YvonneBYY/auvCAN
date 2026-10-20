#define CANARD_DSDLC_INTERNAL
#include <cuauv.AcousticsCommand.h>
#include <string.h>

#ifdef CANARD_DSDLC_TEST_BUILD
#include <test_helpers.h>
#endif

uint32_t _cuauv_AcousticsCommand_encode(struct cuauv_AcousticsCommand* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {
    uint32_t bit_ofs = 0;
    memset(buffer, 0, CUAUV_ACOUSTICSCOMMAND_MAX_SIZE);
    __cuauv_AcousticsCommand_encode(buffer, &bit_ofs, msg,
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
bool _cuauv_AcousticsCommand_decode(const CanardRxTransfer* transfer, struct cuauv_AcousticsCommand* msg) {
#if CANARD_ENABLE_TAO_OPTION
    if (transfer->tao && (transfer->payload_len > CUAUV_ACOUSTICSCOMMAND_MAX_SIZE)) {
        return true; /* invalid payload length */
    }
#endif
    uint32_t bit_ofs = 0;
    if (__cuauv_AcousticsCommand_decode(transfer, &bit_ofs, msg,
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
struct cuauv_AcousticsCommand sample_cuauv_AcousticsCommand_msg(void) {
    struct cuauv_AcousticsCommand msg;

    msg.track_freq_khz = (uint16_t)random_bitlen_unsigned_val(16);
    msg.comms_freq_khz = (uint16_t)random_bitlen_unsigned_val(16);
    msg.tracking_pinger = (uint8_t)random_bitlen_unsigned_val(8);
    msg.freq_select = (uint8_t)random_bitlen_unsigned_val(8);
    msg.transmit_enable = (bool)random_bitlen_unsigned_val(1);
    return msg;
}
#endif
