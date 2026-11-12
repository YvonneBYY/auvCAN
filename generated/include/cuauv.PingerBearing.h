#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <canard.h>


#define CUAUV_PINGERBEARING_MAX_SIZE 6
#define CUAUV_PINGERBEARING_SIGNATURE (0xD53A8F8444CF362BULL)
#define CUAUV_PINGERBEARING_ID 20650

#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
class cuauv_PingerBearing_cxx_iface;
#endif

struct cuauv_PingerBearing {
#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
    using cxx_iface = cuauv_PingerBearing_cxx_iface;
#endif
    float heading_deg;
    float elevation_deg;
    uint8_t confidence;
    uint8_t pinger_id;
};

#ifdef __cplusplus
extern "C"
{
#endif

uint32_t _cuauv_PingerBearing_encode(struct cuauv_PingerBearing* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
);
bool _cuauv_PingerBearing_decode(const CanardRxTransfer* transfer, struct cuauv_PingerBearing* msg);

static inline uint32_t cuauv_PingerBearing_encode(struct cuauv_PingerBearing* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {

    return _cuauv_PingerBearing_encode(msg, buffer
#if CANARD_ENABLE_TAO_OPTION
    , tao
#endif
    );

}

static inline bool cuauv_PingerBearing_decode(const CanardRxTransfer* transfer, struct cuauv_PingerBearing* msg) {

    return _cuauv_PingerBearing_decode(transfer, msg);

}

#if defined(CANARD_DSDLC_INTERNAL)
static inline void __cuauv_PingerBearing_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_PingerBearing* msg, bool tao);
static inline bool __cuauv_PingerBearing_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_PingerBearing* msg, bool tao);
void __cuauv_PingerBearing_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_PingerBearing* msg, bool tao) {
    (void)buffer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;

    {
        uint16_t float16_val = canardConvertNativeFloatToFloat16(msg->heading_deg);
        canardEncodeScalar(buffer, *bit_ofs, 16, &float16_val);
    }
    *bit_ofs += 16;
    {
        uint16_t float16_val = canardConvertNativeFloatToFloat16(msg->elevation_deg);
        canardEncodeScalar(buffer, *bit_ofs, 16, &float16_val);
    }
    *bit_ofs += 16;
    canardEncodeScalar(buffer, *bit_ofs, 8, &msg->confidence);
    *bit_ofs += 8;
    canardEncodeScalar(buffer, *bit_ofs, 8, &msg->pinger_id);
    *bit_ofs += 8;
}

/*
 decode cuauv_PingerBearing, return true on failure, false on success
*/
bool __cuauv_PingerBearing_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_PingerBearing* msg, bool tao) {
    (void)transfer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;
    {
        uint16_t float16_val;
        canardDecodeScalar(transfer, *bit_ofs, 16, true, &float16_val);
        msg->heading_deg = canardConvertFloat16ToNativeFloat(float16_val);
    }
    *bit_ofs += 16;

    {
        uint16_t float16_val;
        canardDecodeScalar(transfer, *bit_ofs, 16, true, &float16_val);
        msg->elevation_deg = canardConvertFloat16ToNativeFloat(float16_val);
    }
    *bit_ofs += 16;

    canardDecodeScalar(transfer, *bit_ofs, 8, false, &msg->confidence);
    *bit_ofs += 8;

    canardDecodeScalar(transfer, *bit_ofs, 8, false, &msg->pinger_id);
    *bit_ofs += 8;

    return false; /* success */
}
#endif
#ifdef CANARD_DSDLC_TEST_BUILD
struct cuauv_PingerBearing sample_cuauv_PingerBearing_msg(void);
#endif
#ifdef __cplusplus
} // extern "C"

#ifdef DRONECAN_CXX_WRAPPERS
#include <canard/cxx_wrappers.h>
BROADCAST_MESSAGE_CXX_IFACE(cuauv_PingerBearing, CUAUV_PINGERBEARING_ID, CUAUV_PINGERBEARING_SIGNATURE, CUAUV_PINGERBEARING_MAX_SIZE);
#endif
#endif
