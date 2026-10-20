#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <canard.h>


#define CUAUV_KILLREQUEST_MAX_SIZE 3
#define CUAUV_KILLREQUEST_SIGNATURE (0x4BE7CB54203DED5AULL)
#define CUAUV_KILLREQUEST_ID 20602

#define CUAUV_KILLREQUEST_REASON_BATTERY_FAULT 1
#define CUAUV_KILLREQUEST_REASON_OVERTEMP 2
#define CUAUV_KILLREQUEST_REASON_UNDERVOLTAGE 4
#define CUAUV_KILLREQUEST_REASON_OVERCURRENT 8

#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
class cuauv_KillRequest_cxx_iface;
#endif

struct cuauv_KillRequest {
#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
    using cxx_iface = cuauv_KillRequest_cxx_iface;
#endif
    bool active;
    uint8_t reason;
    uint8_t seq;
};

#ifdef __cplusplus
extern "C"
{
#endif

uint32_t _cuauv_KillRequest_encode(struct cuauv_KillRequest* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
);
bool _cuauv_KillRequest_decode(const CanardRxTransfer* transfer, struct cuauv_KillRequest* msg);

static inline uint32_t cuauv_KillRequest_encode(struct cuauv_KillRequest* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {

    return _cuauv_KillRequest_encode(msg, buffer
#if CANARD_ENABLE_TAO_OPTION
    , tao
#endif
    );

}

static inline bool cuauv_KillRequest_decode(const CanardRxTransfer* transfer, struct cuauv_KillRequest* msg) {

    return _cuauv_KillRequest_decode(transfer, msg);

}

#if defined(CANARD_DSDLC_INTERNAL)
static inline void __cuauv_KillRequest_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_KillRequest* msg, bool tao);
static inline bool __cuauv_KillRequest_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_KillRequest* msg, bool tao);
void __cuauv_KillRequest_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_KillRequest* msg, bool tao) {
    (void)buffer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;

    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->active);
    *bit_ofs += 1;
    *bit_ofs += 7;
    canardEncodeScalar(buffer, *bit_ofs, 8, &msg->reason);
    *bit_ofs += 8;
    canardEncodeScalar(buffer, *bit_ofs, 8, &msg->seq);
    *bit_ofs += 8;
}

/*
 decode cuauv_KillRequest, return true on failure, false on success
*/
bool __cuauv_KillRequest_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_KillRequest* msg, bool tao) {
    (void)transfer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;
    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->active);
    *bit_ofs += 1;

    *bit_ofs += 7;

    canardDecodeScalar(transfer, *bit_ofs, 8, false, &msg->reason);
    *bit_ofs += 8;

    canardDecodeScalar(transfer, *bit_ofs, 8, false, &msg->seq);
    *bit_ofs += 8;

    return false; /* success */
}
#endif
#ifdef CANARD_DSDLC_TEST_BUILD
struct cuauv_KillRequest sample_cuauv_KillRequest_msg(void);
#endif
#ifdef __cplusplus
} // extern "C"

#ifdef DRONECAN_CXX_WRAPPERS
#include <canard/cxx_wrappers.h>
BROADCAST_MESSAGE_CXX_IFACE(cuauv_KillRequest, CUAUV_KILLREQUEST_ID, CUAUV_KILLREQUEST_SIGNATURE, CUAUV_KILLREQUEST_MAX_SIZE);
#endif
#endif
