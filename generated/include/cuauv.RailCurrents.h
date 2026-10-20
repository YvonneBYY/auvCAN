#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <canard.h>


#define CUAUV_RAILCURRENTS_MAX_SIZE 30
#define CUAUV_RAILCURRENTS_SIGNATURE (0xEFC4B9B122BA5346ULL)
#define CUAUV_RAILCURRENTS_ID 20631

#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
class cuauv_RailCurrents_cxx_iface;
#endif

struct cuauv_RailCurrents {
#if defined(__cplusplus) && defined(DRONECAN_CXX_WRAPPERS)
    using cxx_iface = cuauv_RailCurrents_cxx_iface;
#endif
    float current_a[15];
};

#ifdef __cplusplus
extern "C"
{
#endif

uint32_t _cuauv_RailCurrents_encode(struct cuauv_RailCurrents* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
);
bool _cuauv_RailCurrents_decode(const CanardRxTransfer* transfer, struct cuauv_RailCurrents* msg);

static inline uint32_t cuauv_RailCurrents_encode(struct cuauv_RailCurrents* msg, uint8_t* buffer
#if CANARD_ENABLE_TAO_OPTION
    , bool tao
#endif
) {

    return _cuauv_RailCurrents_encode(msg, buffer
#if CANARD_ENABLE_TAO_OPTION
    , tao
#endif
    );

}

static inline bool cuauv_RailCurrents_decode(const CanardRxTransfer* transfer, struct cuauv_RailCurrents* msg) {

    return _cuauv_RailCurrents_decode(transfer, msg);

}

#if defined(CANARD_DSDLC_INTERNAL)
static inline void __cuauv_RailCurrents_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_RailCurrents* msg, bool tao);
static inline bool __cuauv_RailCurrents_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_RailCurrents* msg, bool tao);
void __cuauv_RailCurrents_encode(uint8_t* buffer, uint32_t* bit_ofs, struct cuauv_RailCurrents* msg, bool tao) {
    (void)buffer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;

    for (size_t i=0; i < 15; i++) {
        {
            uint16_t float16_val = canardConvertNativeFloatToFloat16(msg->current_a[i]);
            canardEncodeScalar(buffer, *bit_ofs, 16, &float16_val);
        }
        *bit_ofs += 16;
    }
}

/*
 decode cuauv_RailCurrents, return true on failure, false on success
*/
bool __cuauv_RailCurrents_decode(const CanardRxTransfer* transfer, uint32_t* bit_ofs, struct cuauv_RailCurrents* msg, bool tao) {
    (void)transfer;
    (void)bit_ofs;
    (void)msg;
    (void)tao;
    for (size_t i=0; i < 15; i++) {
        {
            uint16_t float16_val;
            canardDecodeScalar(transfer, *bit_ofs, 16, true, &float16_val);
            msg->current_a[i] = canardConvertFloat16ToNativeFloat(float16_val);
        }
        *bit_ofs += 16;
    }

    return false; /* success */
}
#endif
#ifdef CANARD_DSDLC_TEST_BUILD
struct cuauv_RailCurrents sample_cuauv_RailCurrents_msg(void);
#endif
#ifdef __cplusplus
} // extern "C"

#ifdef DRONECAN_CXX_WRAPPERS
#include <canard/cxx_wrappers.h>
BROADCAST_MESSAGE_CXX_IFACE(cuauv_RailCurrents, CUAUV_RAILCURRENTS_ID, CUAUV_RAILCURRENTS_SIGNATURE, CUAUV_RAILCURRENTS_MAX_SIZE);
#endif
#endif
