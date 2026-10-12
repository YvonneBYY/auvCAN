#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <canard.h>


#define CUAUV_HOSTHEARTBEAT_MAX_SIZE 2
#define CUAUV_HOSTHEARTBEAT_SIGNATURE (0x52413E499337E7EEULL)
#define CUAUV_HOSTHEARTBEAT_ID 20601

#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
class cuauv_HostHeartbeat_cxx_iface;
#endif

struct cuauv_HostHeartbeat {
#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
    using cxx_iface = cuauv_HostHeartbeat_cxx_iface;
#endif
    bool soft_kill;
    uint8_t seq;
};

#ifdef __cplusplus
extern "C"
{
#endif

uint32_t _cuauv_HostHeartbeat_encode(struct cuauv_HostHeartbeat* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
);
bool _cuauv_HostHeartbeat_decode(const CanardRxTransfer* transfer, struct cuauv_HostHeartbeat* msg);

static inline uint32_t cuauv_HostHeartbeat_encode(struct cuauv_HostHeartbeat* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {

    return _cuauv_HostHeartbeat_encode(msg, buffer
#if CANARD_ENABLE_TAO_OPTION
    , tao
#endif
    );

}

static inline bool cuauv_HostHeartbeat_decode(const CanardRxTransfer* transfer, struct cuauv_HostHeartbeat* msg) {

    return _cuauv_HostHeartbeat_decode(transfer, msg);

}

#if defined(CANARD_DSDLC_INTERNAL)
static inline void __cuauv_HostHeartbeat_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_HostHeartbeat* msg, bool tao);
static inline bool __cuauv_HostHeartbeat_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_HostHeartbeat* msg, bool tao);
void __cuauv_HostHeartbeat_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_HostHeartbeat* msg, bool tao) {
    (void)buffer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;

    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->soft_kill);
    *bit_ofs += 1;
    *bit_ofs += 7;
    canardEncodeScalar(buffer, *bit_ofs, 8, &msg->seq);
    *bit_ofs += 8;
}

/*
 decode cuauv_HostHeartbeat, return true on failure, false on success
*/
bool __cuauv_HostHeartbeat_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_HostHeartbeat* msg, bool tao) {
    (void)transfer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;
    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->soft_kill);
    *bit_ofs += 1;

    *bit_ofs += 7;

    canardDecodeScalar(transfer, *bit_ofs, 8, false, &msg->seq);
    *bit_ofs += 8;

    return false; /* success */
}
#endif
#ifdef CANARD_DSDLC_TEST_BUILD
struct cuauv_HostHeartbeat sample_cuauv_HostHeartbeat_msg(void);
#endif
#ifdef __cplusplus
} // extern "C"

#ifdef DRONECAN_CXX_WRAPPERS
#include <canard/cxx_wrappers.h>
BROADCAST_MESSAGE_CXX_IFACE(cuauv_HostHeartbeat, CUAUV_HOSTHEARTBEAT_ID, CUAUV_HOSTHEARTBEAT_SIGNATURE, CUAUV_HOSTHEARTBEAT_MAX_SIZE);
#endif
#endif
