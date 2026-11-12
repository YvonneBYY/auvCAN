#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <canard.h>


#define CUAUV_BATTERYDETAIL_MAX_SIZE 36
#define CUAUV_BATTERYDETAIL_SIGNATURE (0x5EE5F5E7FB47E792ULL)
#define CUAUV_BATTERYDETAIL_ID 20640

#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
class cuauv_BatteryDetail_cxx_iface;
#endif

struct cuauv_BatteryDetail {
#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
    using cxx_iface = cuauv_BatteryDetail_cxx_iface;
#endif
    float cell_v[4];
    float cell_temp_c[4];
    float fet_temp_c;
    uint16_t time_to_empty_min;
    uint16_t safety_status_hi;
    uint16_t safety_status_lo;
    uint16_t op_status_hi;
    uint16_t op_status_lo;
    uint16_t pf_status_hi;
    uint16_t pf_status_lo;
    uint16_t batt_status;
    bool chg_fet_on;
    bool dsg_fet_on;
    bool cell_balancing;
    bool is_charging;
    bool fully_charged;
    bool fully_discharged;
    bool sys_present;
    bool alarm_term_charge;
    bool alarm_term_discharge;
    bool alarm_rem_capacity;
    bool alarm_rem_time;
};

#ifdef __cplusplus
extern "C"
{
#endif

uint32_t _cuauv_BatteryDetail_encode(struct cuauv_BatteryDetail* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
);
bool _cuauv_BatteryDetail_decode(const CanardRxTransfer* transfer, struct cuauv_BatteryDetail* msg);

static inline uint32_t cuauv_BatteryDetail_encode(struct cuauv_BatteryDetail* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {

    return _cuauv_BatteryDetail_encode(msg, buffer
#if CANARD_ENABLE_TAO_OPTION
    , tao
#endif
    );

}

static inline bool cuauv_BatteryDetail_decode(const CanardRxTransfer* transfer, struct cuauv_BatteryDetail* msg) {

    return _cuauv_BatteryDetail_decode(transfer, msg);

}

#if defined(CANARD_DSDLC_INTERNAL)
static inline void __cuauv_BatteryDetail_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_BatteryDetail* msg, bool tao);
static inline bool __cuauv_BatteryDetail_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_BatteryDetail* msg, bool tao);
void __cuauv_BatteryDetail_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_BatteryDetail* msg, bool tao) {
    (void)buffer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;

    for (size_t i=0; i < 4; i++) {
        {
            uint16_t float16_val = canardConvertNativeFloatToFloat16(msg->cell_v[i]);
            canardEncodeScalar(buffer, *bit_ofs, 16, &float16_val);
        }
        *bit_ofs += 16;
    }
    for (size_t i=0; i < 4; i++) {
        {
            uint16_t float16_val = canardConvertNativeFloatToFloat16(msg->cell_temp_c[i]);
            canardEncodeScalar(buffer, *bit_ofs, 16, &float16_val);
        }
        *bit_ofs += 16;
    }
    {
        uint16_t float16_val = canardConvertNativeFloatToFloat16(msg->fet_temp_c);
        canardEncodeScalar(buffer, *bit_ofs, 16, &float16_val);
    }
    *bit_ofs += 16;
    canardEncodeScalar(buffer, *bit_ofs, 16, &msg->time_to_empty_min);
    *bit_ofs += 16;
    canardEncodeScalar(buffer, *bit_ofs, 16, &msg->safety_status_hi);
    *bit_ofs += 16;
    canardEncodeScalar(buffer, *bit_ofs, 16, &msg->safety_status_lo);
    *bit_ofs += 16;
    canardEncodeScalar(buffer, *bit_ofs, 16, &msg->op_status_hi);
    *bit_ofs += 16;
    canardEncodeScalar(buffer, *bit_ofs, 16, &msg->op_status_lo);
    *bit_ofs += 16;
    canardEncodeScalar(buffer, *bit_ofs, 16, &msg->pf_status_hi);
    *bit_ofs += 16;
    canardEncodeScalar(buffer, *bit_ofs, 16, &msg->pf_status_lo);
    *bit_ofs += 16;
    canardEncodeScalar(buffer, *bit_ofs, 16, &msg->batt_status);
    *bit_ofs += 16;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->chg_fet_on);
    *bit_ofs += 1;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->dsg_fet_on);
    *bit_ofs += 1;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->cell_balancing);
    *bit_ofs += 1;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->is_charging);
    *bit_ofs += 1;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->fully_charged);
    *bit_ofs += 1;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->fully_discharged);
    *bit_ofs += 1;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->sys_present);
    *bit_ofs += 1;
    *bit_ofs += 1;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->alarm_term_charge);
    *bit_ofs += 1;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->alarm_term_discharge);
    *bit_ofs += 1;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->alarm_rem_capacity);
    *bit_ofs += 1;
    canardEncodeScalar(buffer, *bit_ofs, 1, &msg->alarm_rem_time);
    *bit_ofs += 1;
    *bit_ofs += 4;
}

/*
 decode cuauv_BatteryDetail, return true on failure, false on success
*/
bool __cuauv_BatteryDetail_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_BatteryDetail* msg, bool tao) {
    (void)transfer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;
    for (size_t i=0; i < 4; i++) {
        {
            uint16_t float16_val;
            canardDecodeScalar(transfer, *bit_ofs, 16, true, &float16_val);
            msg->cell_v[i] = canardConvertFloat16ToNativeFloat(float16_val);
        }
        *bit_ofs += 16;
    }

    for (size_t i=0; i < 4; i++) {
        {
            uint16_t float16_val;
            canardDecodeScalar(transfer, *bit_ofs, 16, true, &float16_val);
            msg->cell_temp_c[i] = canardConvertFloat16ToNativeFloat(float16_val);
        }
        *bit_ofs += 16;
    }

    {
        uint16_t float16_val;
        canardDecodeScalar(transfer, *bit_ofs, 16, true, &float16_val);
        msg->fet_temp_c = canardConvertFloat16ToNativeFloat(float16_val);
    }
    *bit_ofs += 16;

    canardDecodeScalar(transfer, *bit_ofs, 16, false, &msg->time_to_empty_min);
    *bit_ofs += 16;

    canardDecodeScalar(transfer, *bit_ofs, 16, false, &msg->safety_status_hi);
    *bit_ofs += 16;

    canardDecodeScalar(transfer, *bit_ofs, 16, false, &msg->safety_status_lo);
    *bit_ofs += 16;

    canardDecodeScalar(transfer, *bit_ofs, 16, false, &msg->op_status_hi);
    *bit_ofs += 16;

    canardDecodeScalar(transfer, *bit_ofs, 16, false, &msg->op_status_lo);
    *bit_ofs += 16;

    canardDecodeScalar(transfer, *bit_ofs, 16, false, &msg->pf_status_hi);
    *bit_ofs += 16;

    canardDecodeScalar(transfer, *bit_ofs, 16, false, &msg->pf_status_lo);
    *bit_ofs += 16;

    canardDecodeScalar(transfer, *bit_ofs, 16, false, &msg->batt_status);
    *bit_ofs += 16;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->chg_fet_on);
    *bit_ofs += 1;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->dsg_fet_on);
    *bit_ofs += 1;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->cell_balancing);
    *bit_ofs += 1;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->is_charging);
    *bit_ofs += 1;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->fully_charged);
    *bit_ofs += 1;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->fully_discharged);
    *bit_ofs += 1;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->sys_present);
    *bit_ofs += 1;

    *bit_ofs += 1;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->alarm_term_charge);
    *bit_ofs += 1;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->alarm_term_discharge);
    *bit_ofs += 1;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->alarm_rem_capacity);
    *bit_ofs += 1;

    canardDecodeScalar(transfer, *bit_ofs, 1, false, &msg->alarm_rem_time);
    *bit_ofs += 1;

    *bit_ofs += 4;

    return false; /* success */
}
#endif
#ifdef CANARD_DSDLC_TEST_BUILD
struct cuauv_BatteryDetail sample_cuauv_BatteryDetail_msg(void);
#endif
#ifdef __cplusplus
} // extern "C"

#ifdef DRONECAN_CXX_WRAPPERS
#include <canard/cxx_wrappers.h>
BROADCAST_MESSAGE_CXX_IFACE(cuauv_BatteryDetail, CUAUV_BATTERYDETAIL_ID, CUAUV_BATTERYDETAIL_SIGNATURE, CUAUV_BATTERYDETAIL_MAX_SIZE);
#endif
#endif
