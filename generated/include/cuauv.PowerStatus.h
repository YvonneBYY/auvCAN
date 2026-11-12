#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <canard.h>


#define CUAUV_POWERSTATUS_MAX_SIZE 11
#define CUAUV_POWERSTATUS_SIGNATURE (0x32337C26ACCD597ULL)
#define CUAUV_POWERSTATUS_ID 20630

#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
class cuauv_PowerStatus_cxx_iface;
#endif

struct cuauv_PowerStatus {
#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
    using cxx_iface = cuauv_PowerStatus_cxx_iface;
#endif
    float vin_v;
    float vout_v;
    float current_a;
    float v12_v;
    float v5_v;
    bool killed;
    uint8_t restart_state;
    bool overcurrent;
};

#ifdef __cplusplus
extern "C"
{
#endif

uint32_t _cuauv_PowerStatus_encode(struct cuauv_PowerStatus* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
);
bool _cuauv_PowerStatus_decode(const CanardRxTransfer* transfer, struct cuauv_PowerStatus* msg);

static inline uint32_t cuauv_PowerStatus_encode(struct cuauv_PowerStatus* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {

    return _cuauv_PowerStatus_encode(msg, buffer
#if CANARD_ENABLE_TAO_OPTION
    , tao
#endif
    );

}

static inline bool cuauv_PowerStatus_decode(const CanardRxTransfer* transfer, struct cuauv_PowerStatus* msg) {

    return _cuauv_PowerStatus_decode(transfer, msg);

}

#if defined(CANARD_DSDLC_INTERNAL)
static inline void __cuauv_PowerStatus_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_PowerStatus* msg, bool tao);
static inline bool __cuauv_PowerStatus_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_PowerStatus* msg, bool tao);
void __cuauv_PowerStatus_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_PowerStatus* msg, bool tao) {
    (void)buffer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;

    {
        uint16_t float16_val = canardConvertNativeFloatToFloat16(msg->vin_v);
        canardEncodeScalar(buffer, *bit_ofs, 16, &float16_val);
    }
    *bit_ofs += 16;
    {
        uint16_t float16_val = canardConvertNativeFloatToFloat16(msg->vout_v);
        canardEncodeScalar(buffer, *bit_ofs, 16, &float16_val);
    }
    *bit_ofs += 16;
    {
        uint16_t float16_val = canardConvertNativeFloatToFloat16(msg->current_a);
        canardEncodeScalar(buffer, *bit_ofs, 16, &float16_val);
    }
    *bit_ofs += 16;
    {
        uint16_t float16_val = canardConvertNativeFloatToFloat16(msg->v12_v);
        canardEncodeScalar(buffer, *bit_ofs, 16, &float16_val);
    }
    *bit_ofs += 16;
    {
        uint16_t float16_val = canardConvertNativeFloatToFloat16(msg->v5_v);
        canardEncodeScalar(buffer, *bit_ofs, 16, &float16_val);
    }
    *bit_ofs += 16;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->killed);
    *bit_ofs += 1;
    canardEncodeScalar(buffer, *bit_ofs, 2, &msg->restart_state);
    *bit_ofs += 2;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->overcurrent);
    *bit_ofs += 1;
    *bit_ofs += 4;
}

/*
 decode cuauv_PowerStatus, return true on failure, false on success
*/
bool __cuauv_PowerStatus_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_PowerStatus* msg, bool tao) {
    (void)transfer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;
    {
        uint16_t float16_val;
        canardDecodeScalar(transfer, *bit_ofs, 16, true, &float16_val);
        msg->vin_v = canardConvertFloat16ToNativeFloat(float16_val);
    }
    *bit_ofs += 16;

    {
        uint16_t float16_val;
        canardDecodeScalar(transfer, *bit_ofs, 16, true, &float16_val);
        msg->vout_v = canardConvertFloat16ToNativeFloat(float16_val);
    }
    *bit_ofs += 16;

    {
        uint16_t float16_val;
        canardDecodeScalar(transfer, *bit_ofs, 16, true, &float16_val);
        msg->current_a = canardConvertFloat16ToNativeFloat(float16_val);
    }
    *bit_ofs += 16;

    {
        uint16_t float16_val;
        canardDecodeScalar(transfer, *bit_ofs, 16, true, &float16_val);
        msg->v12_v = canardConvertFloat16ToNativeFloat(float16_val);
    }
    *bit_ofs += 16;

    {
        uint16_t float16_val;
        canardDecodeScalar(transfer, *bit_ofs, 16, true, &float16_val);
        msg->v5_v = canardConvertFloat16ToNativeFloat(float16_val);
    }
    *bit_ofs += 16;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->killed);
    *bit_ofs += 1;

    canardDecodeScalar(transfer, *bit_ofs, 2, false, &msg->restart_state);
    *bit_ofs += 2;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->overcurrent);
    *bit_ofs += 1;

    *bit_ofs += 4;

    return false; /* success */
}
#endif
#ifdef CANARD_DSDLC_TEST_BUILD
struct cuauv_PowerStatus sample_cuauv_PowerStatus_msg(void);
#endif
#ifdef __cplusplus
} // extern "C"

#ifdef DRONECAN_CXX_WRAPPERS
#include <canard/cxx_wrappers.h>
BROADCAST_MESSAGE_CXX_IFACE(cuauv_PowerStatus, CUAUV_POWERSTATUS_ID, CUAUV_POWERSTATUS_SIGNATURE, CUAUV_POWERSTATUS_MAX_SIZE);
#endif
#endif
