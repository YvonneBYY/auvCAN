#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <canard.h>


#define CUAUV_ACOUSTICSSTATUS_MAX_SIZE 6
#define CUAUV_ACOUSTICSSTATUS_SIGNATURE (0xF684700D6B27F14FULL)
#define CUAUV_ACOUSTICSSTATUS_ID 20651

#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
class cuauv_AcousticsStatus_cxx_iface;
#endif

struct cuauv_AcousticsStatus {
#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
    using cxx_iface = cuauv_AcousticsStatus_cxx_iface;
#endif
    uint16_t track_freq_khz;
    uint16_t comms_freq_khz;
    uint8_t freq_select;
    bool transmit_active;
    bool transmission_done;
};

#ifdef __cplusplus
extern "C"
{
#endif

uint32_t _cuauv_AcousticsStatus_encode(struct cuauv_AcousticsStatus* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
);
bool _cuauv_AcousticsStatus_decode(const CanardRxTransfer* transfer, struct cuauv_AcousticsStatus* msg);

static inline uint32_t cuauv_AcousticsStatus_encode(struct cuauv_AcousticsStatus* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {

    return _cuauv_AcousticsStatus_encode(msg, buffer
#if CANARD_ENABLE_TAO_OPTION
    , tao
#endif
    );

}

static inline bool cuauv_AcousticsStatus_decode(const CanardRxTransfer* transfer, struct cuauv_AcousticsStatus* msg) {

    return _cuauv_AcousticsStatus_decode(transfer, msg);

}

#if defined(CANARD_DSDLC_INTERNAL)
static inline void __cuauv_AcousticsStatus_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_AcousticsStatus* msg, bool tao);
static inline bool __cuauv_AcousticsStatus_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_AcousticsStatus* msg, bool tao);
void __cuauv_AcousticsStatus_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_AcousticsStatus* msg, bool tao) {
    (void)buffer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;

    canardEncodeScalar(buffer, *bit_ofs, 16, &msg->track_freq_khz);
    *bit_ofs += 16;
    canardEncodeScalar(buffer, *bit_ofs, 16, &msg->comms_freq_khz);
    *bit_ofs += 16;
    canardEncodeScalar(buffer, *bit_ofs, 8, &msg->freq_select);
    *bit_ofs += 8;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->transmit_active);
    *bit_ofs += 1;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->transmission_done);
    *bit_ofs += 1;
    *bit_ofs += 6;
}

/*
 decode cuauv_AcousticsStatus, return true on failure, false on success
*/
bool __cuauv_AcousticsStatus_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_AcousticsStatus* msg, bool tao) {
    (void)transfer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;
    canardDecodeScalar(transfer, *bit_ofs, 16, false, &msg->track_freq_khz);
    *bit_ofs += 16;

    canardDecodeScalar(transfer, *bit_ofs, 16, false, &msg->comms_freq_khz);
    *bit_ofs += 16;

    canardDecodeScalar(transfer, *bit_ofs, 8, false, &msg->freq_select);
    *bit_ofs += 8;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->transmit_active);
    *bit_ofs += 1;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->transmission_done);
    *bit_ofs += 1;

    *bit_ofs += 6;

    return false; /* success */
}
#endif
#ifdef CANARD_DSDLC_TEST_BUILD
struct cuauv_AcousticsStatus sample_cuauv_AcousticsStatus_msg(void);
#endif
#ifdef __cplusplus
} // extern "C"

#ifdef DRONECAN_CXX_WRAPPERS
#include <canard/cxx_wrappers.h>
BROADCAST_MESSAGE_CXX_IFACE(cuauv_AcousticsStatus, CUAUV_ACOUSTICSSTATUS_ID, CUAUV_ACOUSTICSSTATUS_SIGNATURE, CUAUV_ACOUSTICSSTATUS_MAX_SIZE);
#endif
#endif
