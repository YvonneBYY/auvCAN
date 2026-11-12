#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <canard.h>


#define CUAUV_HULLSTATUS_MAX_SIZE 7
#define CUAUV_HULLSTATUS_SIGNATURE (0xF35C596294C74547ULL)
#define CUAUV_HULLSTATUS_ID 20621

#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
class cuauv_HullStatus_cxx_iface;
#endif

struct cuauv_HullStatus {
#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
    using cxx_iface = cuauv_HullStatus_cxx_iface;
#endif
    float hull_pressure_atm;
    float internal_temp_c;
    bool leak[4];
    bool pressure_sensor_ok;
};

#ifdef __cplusplus
extern "C"
{
#endif

uint32_t _cuauv_HullStatus_encode(struct cuauv_HullStatus* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
);
bool _cuauv_HullStatus_decode(const CanardRxTransfer* transfer, struct cuauv_HullStatus* msg);

static inline uint32_t cuauv_HullStatus_encode(struct cuauv_HullStatus* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {

    return _cuauv_HullStatus_encode(msg, buffer
#if CANARD_ENABLE_TAO_OPTION
    , tao
#endif
    );

}

static inline bool cuauv_HullStatus_decode(const CanardRxTransfer* transfer, struct cuauv_HullStatus* msg) {

    return _cuauv_HullStatus_decode(transfer, msg);

}

#if defined(CANARD_DSDLC_INTERNAL)
static inline void __cuauv_HullStatus_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_HullStatus* msg, bool tao);
static inline bool __cuauv_HullStatus_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_HullStatus* msg, bool tao);
void __cuauv_HullStatus_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_HullStatus* msg, bool tao) {
    (void)buffer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;

    canardEncodeScalar(buffer, *bit_ofs, 32, &msg->hull_pressure_atm);
    *bit_ofs += 32;
    {
        uint16_t float16_val = canardConvertNativeFloatToFloat16(msg->internal_temp_c);
        canardEncodeScalar(buffer, *bit_ofs, 16, &float16_val);
    }
    *bit_ofs += 16;
    for (size_t i=0; i < 4; i++) {
        canardEncodeScalar(buffer, *bit_ofs, 1, &msg->leak[i]);
        *bit_ofs += 1;
    }
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->pressure_sensor_ok);
    *bit_ofs += 1;
    *bit_ofs += 3;
}

/*
 decode cuauv_HullStatus, return true on failure, false on success
*/
bool __cuauv_HullStatus_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_HullStatus* msg, bool tao) {
    (void)transfer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;
    canardDecodeScalar(transfer, *bit_ofs, 32, true, &msg->hull_pressure_atm);
    *bit_ofs += 32;

    {
        uint16_t float16_val;
        canardDecodeScalar(transfer, *bit_ofs, 16, true, &float16_val);
        msg->internal_temp_c = canardConvertFloat16ToNativeFloat(float16_val);
    }
    *bit_ofs += 16;

    for (size_t i=0; i < 4; i++) {
        canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->leak[i]);
        *bit_ofs += 1;
    }

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->pressure_sensor_ok);
    *bit_ofs += 1;

    *bit_ofs += 3;

    return false; /* success */
}
#endif
#ifdef CANARD_DSDLC_TEST_BUILD
struct cuauv_HullStatus sample_cuauv_HullStatus_msg(void);
#endif
#ifdef __cplusplus
} // extern "C"

#ifdef DRONECAN_CXX_WRAPPERS
#include <canard/cxx_wrappers.h>
BROADCAST_MESSAGE_CXX_IFACE(cuauv_HullStatus, CUAUV_HULLSTATUS_ID, CUAUV_HULLSTATUS_SIGNATURE, CUAUV_HULLSTATUS_MAX_SIZE);
#endif
#endif
