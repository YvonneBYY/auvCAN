#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <canard.h>


#define CUAUV_KILL_MAX_SIZE 2
#define CUAUV_KILL_SIGNATURE (0x7449A323ED0D970CULL)
#define CUAUV_KILL_ID 20600

#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
class cuauv_Kill_cxx_iface;
#endif

struct cuauv_Kill {
#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
    using cxx_iface = cuauv_Kill_cxx_iface;
#endif
    bool armed;
    bool hard_kill;
    bool soft_kill;
    bool host_ok;
    uint8_t seq;
};

#ifdef __cplusplus
extern "C"
{
#endif

uint32_t _cuauv_Kill_encode(struct cuauv_Kill* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
);
bool _cuauv_Kill_decode(const CanardRxTransfer* transfer, struct cuauv_Kill* msg);

static inline uint32_t cuauv_Kill_encode(struct cuauv_Kill* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {

    return _cuauv_Kill_encode(msg, buffer
#if CANARD_ENABLE_TAO_OPTION
    , tao
#endif
    );

}

static inline bool cuauv_Kill_decode(const CanardRxTransfer* transfer, struct cuauv_Kill* msg) {

    return _cuauv_Kill_decode(transfer, msg);

}

#if defined(CANARD_DSDLC_INTERNAL)
static inline void __cuauv_Kill_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_Kill* msg, bool tao);
static inline bool __cuauv_Kill_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_Kill* msg, bool tao);
void __cuauv_Kill_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_Kill* msg, bool tao) {
    (void)buffer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;

    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->armed);
    *bit_ofs += 1;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->hard_kill);
    *bit_ofs += 1;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->soft_kill);
    *bit_ofs += 1;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->host_ok);
    *bit_ofs += 1;
    *bit_ofs += 4;
    canardEncodeScalar(buffer, *bit_ofs, 8, &msg->seq);
    *bit_ofs += 8;
}

/*
 decode cuauv_Kill, return true on failure, false on success
*/
bool __cuauv_Kill_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_Kill* msg, bool tao) {
    (void)transfer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;
    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->armed);
    *bit_ofs += 1;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->hard_kill);
    *bit_ofs += 1;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->soft_kill);
    *bit_ofs += 1;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->host_ok);
    *bit_ofs += 1;

    *bit_ofs += 4;

    canardDecodeScalar(transfer, *bit_ofs, 8, false, &msg->seq);
    *bit_ofs += 8;

    return false; /* success */
}
#endif
#ifdef CANARD_DSDLC_TEST_BUILD
struct cuauv_Kill sample_cuauv_Kill_msg(void);
#endif
#ifdef __cplusplus
} // extern "C"

#ifdef DRONECAN_CXX_WRAPPERS
#include <canard/cxx_wrappers.h>
BROADCAST_MESSAGE_CXX_IFACE(cuauv_Kill, CUAUV_KILL_ID, CUAUV_KILL_SIGNATURE, CUAUV_KILL_MAX_SIZE);
#endif
#endif
