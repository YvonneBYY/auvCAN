#define CANARD_DSDLC_INTERNAL
#include <cuauv.BatteryDetail.h>
#include <string.h>

#ifdef CANARD_DSDLC_TEST_BUILD
#include <test_helpers.h>
#endif

uint32_t _cuauv_BatteryDetail_encode(struct cuauv_BatteryDetail* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {
    uint32_t bit_ofs = 0;
    memset(buffer, 0, CUAUV_BATTERYDETAIL_MAX_SIZE);
    __cuauv_BatteryDetail_encode(buffer, &bit_ofs, msg,
#if CANARD_ENABLE_TAO_OPTION
    tao
#else
    true
#endif
    );
    return ((bit_ofs+7)/8);
}

/*
  return true if the decode is invalid
 */
bool _cuauv_BatteryDetail_decode(const CanardRxTransfer* transfer, struct cuauv_BatteryDetail* msg) {
#if CANARD_ENABLE_TAO_OPTION
    if (transfer->tao && (transfer->payload_len > CUAUV_BATTERYDETAIL_MAX_SIZE)) {
        return true; /* invalid payload length */
    }
#endif
    uint32_t bit_ofs = 0;
    if (__cuauv_BatteryDetail_decode(transfer, &bit_ofs, msg,
#if CANARD_ENABLE_TAO_OPTION
    transfer->tao
#else
    true
#endif
    )) {
        return true; /* invalid payload */
    }

    const uint32_t byte_len = (bit_ofs+7U)/8U;
#if CANARD_ENABLE_TAO_OPTION
    // if this could be CANFD then the dlc could indicating more bytes than
    // we actually have
    if (!transfer->tao) {
        return byte_len > transfer->payload_len;
    }
#endif
    return byte_len != transfer->payload_len;
}

#ifdef CANARD_DSDLC_TEST_BUILD
struct cuauv_BatteryDetail sample_cuauv_BatteryDetail_msg(void) {
    struct cuauv_BatteryDetail msg;

    for (size_t i=0; i < 4; i++) {
        msg.cell_v[i] = random_float16_val();
    }
    for (size_t i=0; i < 4; i++) {
        msg.cell_temp_c[i] = random_float16_val();
    }
    msg.fet_temp_c = random_float16_val();
    msg.time_to_empty_min = (uint16_t)random_bitlen_unsigned_val(16);
    msg.safety_status_hi = (uint16_t)random_bitlen_unsigned_val(16);
    msg.safety_status_lo = (uint16_t)random_bitlen_unsigned_val(16);
    msg.op_status_hi = (uint16_t)random_bitlen_unsigned_val(16);
    msg.op_status_lo = (uint16_t)random_bitlen_unsigned_val(16);
    msg.pf_status_hi = (uint16_t)random_bitlen_unsigned_val(16);
    msg.pf_status_lo = (uint16_t)random_bitlen_unsigned_val(16);
    msg.batt_status = (uint16_t)random_bitlen_unsigned_val(16);
    msg.chg_fet_on = (bool)random_bitlen_unsigned_val(1);
    msg.dsg_fet_on = (bool)random_bitlen_unsigned_val(1);
    msg.cell_balancing = (bool)random_bitlen_unsigned_val(1);
    msg.is_charging = (bool)random_bitlen_unsigned_val(1);
    msg.fully_charged = (bool)random_bitlen_unsigned_val(1);
    msg.fully_discharged = (bool)random_bitlen_unsigned_val(1);
    msg.sys_present = (bool)random_bitlen_unsigned_val(1);
    msg.alarm_term_charge = (bool)random_bitlen_unsigned_val(1);
    msg.alarm_term_discharge = (bool)random_bitlen_unsigned_val(1);
    msg.alarm_rem_capacity = (bool)random_bitlen_unsigned_val(1);
    msg.alarm_rem_time = (bool)random_bitlen_unsigned_val(1);
    return msg;
}
#endif
