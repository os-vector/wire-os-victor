#ifndef WWISE_SOFTFP_WRAPPER_H
#define WWISE_SOFTFP_WRAPPER_H

#include <cstdint>
#include <cstring>
#include <AK/SoundEngine/Common/AkTypes.h>
#include <AK/SoundEngine/Common/AkQueryParameters.h>

extern "C" {

AKRESULT wwiseShim_SetRTPCValue(
    AkRtpcID        in_rtpcID,
    uint32_t        in_valueBits,
    AkGameObjectID  in_gameObjectID,
    AkTimeMs        in_uValueChangeDuration,
    AkCurveInterpolation in_eFadeCurve,
    bool            in_bBypassInternalValueInterpolation);

AKRESULT wwiseShim_SetRTPCValueByPlayingID(
    AkRtpcID        in_rtpcID,
    uint32_t        in_valueBits,
    AkPlayingID     in_playingID,
    AkTimeMs        in_uValueChangeDuration,
    AkCurveInterpolation in_eFadeCurve,
    bool            in_bBypassInternalValueInterpolation);

AKRESULT wwiseShim_SetGameObjectOutputBusVolume(
    AkGameObjectID  in_emitterObjID,
    AkGameObjectID  in_listenerObjID,
    uint32_t        in_fControlValueBits);

AKRESULT wwiseShim_GetRTPCValue(
    AkRtpcID        in_rtpcID,
    AkGameObjectID  in_gameObjectID,
    AkPlayingID     in_playingID,
    uint32_t*       out_valueBits,
    AK::SoundEngine::Query::RTPCValue_type* io_rValueType);

}

inline uint32_t floatBitsToU32(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

inline float u32ToFloatBits(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

#endif
