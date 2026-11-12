#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <canard.h>


#define CUAUV_DEPTH_MAX_SIZE 7
#define CUAUV_DEPTH_SIGNATURE (0x9566AB20C2001E40ULL)
#define CUAUV_DEPTH_ID 20620

#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
class cuauv_Depth_cxx_iface;
#endif

struct cuauv_Depth {
#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
    using cxx_iface = cuauv_Depth_cxx_iface;
#endif
    float depth_m;
    float water_temp_c;
    bool sensor_ok;
};

#ifdef __cplusplus
extern "C"
{
#endif

uint32_t _cuauv_Depth_encode(struct cuauv_Depth* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
);
bool _cuauv_Depth_decode(const CanardRxTransfer* transfer, struct cuauv_Depth* msg);

static inline uint32_t cuauv_Depth_encode(struct cuauv_Depth* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {

    return _cuauv_Depth_encode(msg, buffer
#if CANARD_ENABLE_TAO_OPTION
    , tao
#endif
    );

}

static inline bool cuauv_Depth_decode(const CanardRxTransfer* transfer, struct cuauv_Depth* msg) {

    return _cuauv_Depth_decode(transfer, msg);

}

#if defined(CANARD_DSDLC_INTERNAL)
static inline void __cuauv_Depth_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_Depth* msg, bool tao);
static inline bool __cuauv_Depth_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_Depth* msg, bool tao);
void __cuauv_Depth_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_Depth* msg, bool tao) {
    (void)buffer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;

    canardEncodeScalar(buffer, *bit_ofs, 32, &msg->depth_m);
    *bit_ofs += 32;
    {
        uint16_t float16_val = canardConvertNativeFloatToFloat16(msg->water_temp_c);
        canardEncodeScalar(buffer, *bit_ofs, 16, &float16_val);
    }
    *bit_ofs += 16;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->sensor_ok);
    *bit_ofs += 1;
    *bit_ofs += 7;
}

/*
 decode cuauv_Depth, return true on failure, false on success
*/
bool __cuauv_Depth_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_Depth* msg, bool tao) {
    (void)transfer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;
    canardDecodeScalar(transfer, *bit_ofs, 32, true, &msg->depth_m);
    *bit_ofs += 32;

    {
        uint16_t float16_val;
        canardDecodeScalar(transfer, *bit_ofs, 16, true, &float16_val);
        msg->water_temp_c = canardConvertFloat16ToNativeFloat(float16_val);
    }
    *bit_ofs += 16;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->sensor_ok);
    *bit_ofs += 1;

    *bit_ofs += 7;

    return false; /* success */
}
#endif
#ifdef CANARD_DSDLC_TEST_BUILD
struct cuauv_Depth sample_cuauv_Depth_msg(void);
#endif
#ifdef __cplusplus
} // extern "C"

#ifdef DRONECAN_CXX_WRAPPERS
#include <canard/cxx_wrappers.h>
BROADCAST_MESSAGE_CXX_IFACE(cuauv_Depth, CUAUV_DEPTH_ID, CUAUV_DEPTH_SIGNATURE, CUAUV_DEPTH_MAX_SIZE);
#endif
#endif
