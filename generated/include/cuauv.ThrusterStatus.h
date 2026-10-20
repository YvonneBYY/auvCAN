#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <canard.h>


#define CUAUV_THRUSTERSTATUS_MAX_SIZE 7
#define CUAUV_THRUSTERSTATUS_SIGNATURE (0x6F4D02CD6C46964AULL)
#define CUAUV_THRUSTERSTATUS_ID 20610

#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
class cuauv_ThrusterStatus_cxx_iface;
#endif

struct cuauv_ThrusterStatus {
#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
    using cxx_iface = cuauv_ThrusterStatus_cxx_iface;
#endif
    int16_t applied[3];
    bool killed;
    bool stale;
};

#ifdef __cplusplus
extern "C"
{
#endif

uint32_t _cuauv_ThrusterStatus_encode(struct cuauv_ThrusterStatus* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
);
bool _cuauv_ThrusterStatus_decode(const CanardRxTransfer* transfer, struct cuauv_ThrusterStatus* msg);

static inline uint32_t cuauv_ThrusterStatus_encode(struct cuauv_ThrusterStatus* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {

    return _cuauv_ThrusterStatus_encode(msg, buffer
#if CANARD_ENABLE_TAO_OPTION
    , tao
#endif
    );

}

static inline bool cuauv_ThrusterStatus_decode(const CanardRxTransfer* transfer, struct cuauv_ThrusterStatus* msg) {

    return _cuauv_ThrusterStatus_decode(transfer, msg);

}

#if defined(CANARD_DSDLC_INTERNAL)
static inline void __cuauv_ThrusterStatus_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_ThrusterStatus* msg, bool tao);
static inline bool __cuauv_ThrusterStatus_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_ThrusterStatus* msg, bool tao);
void __cuauv_ThrusterStatus_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_ThrusterStatus* msg, bool tao) {
    (void)buffer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;

    for (size_t i=0; i < 3; i++) {
        canardEncodeScalar(buffer, *bit_ofs, 16, &msg->applied[i]);
        *bit_ofs += 16;
    }
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->killed);
    *bit_ofs += 1;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->stale);
    *bit_ofs += 1;
    *bit_ofs += 6;
}

/*
 decode cuauv_ThrusterStatus, return true on failure, false on success
*/
bool __cuauv_ThrusterStatus_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_ThrusterStatus* msg, bool tao) {
    (void)transfer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;
    for (size_t i=0; i < 3; i++) {
        canardDecodeScalar(transfer, *bit_ofs, 16, true, &msg->applied[i]);
        *bit_ofs += 16;
    }

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->killed);
    *bit_ofs += 1;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->stale);
    *bit_ofs += 1;

    *bit_ofs += 6;

    return false; /* success */
}
#endif
#ifdef CANARD_DSDLC_TEST_BUILD
struct cuauv_ThrusterStatus sample_cuauv_ThrusterStatus_msg(void);
#endif
#ifdef __cplusplus
} // extern "C"

#ifdef DRONECAN_CXX_WRAPPERS
#include <canard/cxx_wrappers.h>
BROADCAST_MESSAGE_CXX_IFACE(cuauv_ThrusterStatus, CUAUV_THRUSTERSTATUS_ID, CUAUV_THRUSTERSTATUS_SIGNATURE, CUAUV_THRUSTERSTATUS_MAX_SIZE);
#endif
#endif
