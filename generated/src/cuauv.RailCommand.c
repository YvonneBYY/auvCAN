#define CANARD_DSDLC_INTERNAL
#include <cuauv.RailCommand.h>
#include <string.h>

#ifdef CANARD_DSDLC_TEST_BUILD
#include <test_helpers.h>
#endif

uint32_t _cuauv_RailCommand_encode(struct cuauv_RailCommand* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {
    uint32_t bit_ofs = 0;
    memset(buffer, 0, CUAUV_RAILCOMMAND_MAX_SIZE);
    __cuauv_RailCommand_encode(buffer, &bit_ofs, msg,
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
bool _cuauv_RailCommand_decode(const CanardRxTransfer* transfer, struct cuauv_RailCommand* msg) {
#if CANARD_ENABLE_TAO_OPTION
    if (transfer->tao && (transfer->payload_len > CUAUV_RAILCOMMAND_MAX_SIZE)) {
        return true; /* invalid payload length */
    }
#endif
    uint32_t bit_ofs = 0;
    if (__cuauv_RailCommand_decode(transfer, &bit_ofs, msg,
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
struct cuauv_RailCommand sample_cuauv_RailCommand_msg(void) {
    struct cuauv_RailCommand msg;

    msg.enable_mask = (uint16_t)random_bitlen_unsigned_val(16);
    msg.priv_mask = (uint16_t)random_bitlen_unsigned_val(16);
    msg.seq = (uint8_t)random_bitlen_unsigned_val(8);
    return msg;
}
#endif
