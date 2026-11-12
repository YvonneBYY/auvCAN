#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <canard.h>


#define CUAUV_RAILCOMMAND_MAX_SIZE 5
#define CUAUV_RAILCOMMAND_SIGNATURE (0xF41E7CE93BCE3405ULL)
#define CUAUV_RAILCOMMAND_ID 20632

#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
class cuauv_RailCommand_cxx_iface;
#endif

struct cuauv_RailCommand {
#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
    using cxx_iface = cuauv_RailCommand_cxx_iface;
#endif
    uint16_t enable_mask;
    uint16_t priv_mask;
    uint8_t seq;
};

#ifdef __cplusplus
extern "C"
{
#endif

uint32_t _cuauv_RailCommand_encode(struct cuauv_RailCommand* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
);
bool _cuauv_RailCommand_decode(const CanardRxTransfer* transfer, struct cuauv_RailCommand* msg);

static inline uint32_t cuauv_RailCommand_encode(struct cuauv_RailCommand* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {

    return _cuauv_RailCommand_encode(msg, buffer
#if CANARD_ENABLE_TAO_OPTION
    , tao
#endif
    );

}

static inline bool cuauv_RailCommand_decode(const CanardRxTransfer* transfer, struct cuauv_RailCommand* msg) {

    return _cuauv_RailCommand_decode(transfer, msg);

}

#if defined(CANARD_DSDLC_INTERNAL)
static inline void __cuauv_RailCommand_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_RailCommand* msg, bool tao);
static inline bool __cuauv_RailCommand_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_RailCommand* msg, bool tao);
void __cuauv_RailCommand_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_RailCommand* msg, bool tao) {
    (void)buffer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;

    canardEncodeScalar(buffer, *bit_ofs, 16, &msg->enable_mask);
    *bit_ofs += 16;
    canardEncodeScalar(buffer, *bit_ofs, 16, &msg->priv_mask);
    *bit_ofs += 16;
    canardEncodeScalar(buffer, *bit_ofs, 8, &msg->seq);
    *bit_ofs += 8;
}

/*
 decode cuauv_RailCommand, return true on failure, false on success
*/
bool __cuauv_RailCommand_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_RailCommand* msg, bool tao) {
    (void)transfer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;
    canardDecodeScalar(transfer, *bit_ofs, 16, false, &msg->enable_mask);
    *bit_ofs += 16;

    canardDecodeScalar(transfer, *bit_ofs, 16, false, &msg->priv_mask);
    *bit_ofs += 16;

    canardDecodeScalar(transfer, *bit_ofs, 8, false, &msg->seq);
    *bit_ofs += 8;

    return false; /* success */
}
#endif
#ifdef CANARD_DSDLC_TEST_BUILD
struct cuauv_RailCommand sample_cuauv_RailCommand_msg(void);
#endif
#ifdef __cplusplus
} // extern "C"

#ifdef DRONECAN_CXX_WRAPPERS
#include <canard/cxx_wrappers.h>
BROADCAST_MESSAGE_CXX_IFACE(cuauv_RailCommand, CUAUV_RAILCOMMAND_ID, CUAUV_RAILCOMMAND_SIGNATURE, CUAUV_RAILCOMMAND_MAX_SIZE);
#endif
#endif
