#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <canard.h>


#define CUAUV_ACOUSTICSCOMMAND_MAX_SIZE 7
#define CUAUV_ACOUSTICSCOMMAND_SIGNATURE (0x8C4F7FAD939B84ECULL)
#define CUAUV_ACOUSTICSCOMMAND_ID 20652

#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
class cuauv_AcousticsCommand_cxx_iface;
#endif

struct cuauv_AcousticsCommand {
#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
    using cxx_iface = cuauv_AcousticsCommand_cxx_iface;
#endif
    uint16_t track_freq_khz;
    uint16_t comms_freq_khz;
    uint8_t tracking_pinger;
    uint8_t freq_select;
    bool transmit_enable;
};

#ifdef __cplusplus
extern "C"
{
#endif

uint32_t _cuauv_AcousticsCommand_encode(struct cuauv_AcousticsCommand* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
);
bool _cuauv_AcousticsCommand_decode(const CanardRxTransfer* transfer, struct cuauv_AcousticsCommand* msg);

static inline uint32_t cuauv_AcousticsCommand_encode(struct cuauv_AcousticsCommand* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {

    return _cuauv_AcousticsCommand_encode(msg, buffer
#if CANARD_ENABLE_TAO_OPTION
    , tao
#endif
    );

}

static inline bool cuauv_AcousticsCommand_decode(const CanardRxTransfer* transfer, struct cuauv_AcousticsCommand* msg) {

    return _cuauv_AcousticsCommand_decode(transfer, msg);

}

#if defined(CANARD_DSDLC_INTERNAL)
static inline void __cuauv_AcousticsCommand_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_AcousticsCommand* msg, bool tao);
static inline bool __cuauv_AcousticsCommand_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_AcousticsCommand* msg, bool tao);
void __cuauv_AcousticsCommand_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_AcousticsCommand* msg, bool tao) {
    (void)buffer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;

    canardEncodeScalar(buffer, *bit_ofs, 16, &msg->track_freq_khz);
    *bit_ofs += 16;
    canardEncodeScalar(buffer, *bit_ofs, 16, &msg->comms_freq_khz);
    *bit_ofs += 16;
    canardEncodeScalar(buffer, *bit_ofs, 8, &msg->tracking_pinger);
    *bit_ofs += 8;
    canardEncodeScalar(buffer, *bit_ofs, 8, &msg->freq_select);
    *bit_ofs += 8;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->transmit_enable);
    *bit_ofs += 1;
    *bit_ofs += 7;
}

/*
 decode cuauv_AcousticsCommand, return true on failure, false on success
*/
bool __cuauv_AcousticsCommand_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_AcousticsCommand* msg, bool tao) {
    (void)transfer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;
    canardDecodeScalar(transfer, *bit_ofs, 16, false, &msg->track_freq_khz);
    *bit_ofs += 16;

    canardDecodeScalar(transfer, *bit_ofs, 16, false, &msg->comms_freq_khz);
    *bit_ofs += 16;

    canardDecodeScalar(transfer, *bit_ofs, 8, false, &msg->tracking_pinger);
    *bit_ofs += 8;

    canardDecodeScalar(transfer, *bit_ofs, 8, false, &msg->freq_select);
    *bit_ofs += 8;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->transmit_enable);
    *bit_ofs += 1;

    *bit_ofs += 7;

    return false; /* success */
}
#endif
#ifdef CANARD_DSDLC_TEST_BUILD
struct cuauv_AcousticsCommand sample_cuauv_AcousticsCommand_msg(void);
#endif
#ifdef __cplusplus
} // extern "C"

#ifdef DRONECAN_CXX_WRAPPERS
#include <canard/cxx_wrappers.h>
BROADCAST_MESSAGE_CXX_IFACE(cuauv_AcousticsCommand, CUAUV_ACOUSTICSCOMMAND_ID, CUAUV_ACOUSTICSCOMMAND_SIGNATURE, CUAUV_ACOUSTICSCOMMAND_MAX_SIZE);
#endif
#endif
