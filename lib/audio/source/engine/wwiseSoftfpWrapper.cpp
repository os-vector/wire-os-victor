#include <cstring>
#include <cstdint>
#include <AK/SoundEngine/Common/AkSoundEngine.h>
#include <AK/SoundEngine/Common/AkQueryParameters.h>

extern "C" {

AKRESULT wwiseShim_SetRTPCValue(
    AkRtpcID        in_rtpcID,
    uint32_t        in_valueBits, // float bits
    AkGameObjectID  in_gameObjectID,
    AkTimeMs        in_uValueChangeDuration,
    AkCurveInterpolation in_eFadeCurve,
    bool            in_bBypassInternalValueInterpolation)
{
    AkRtpcValue val;
    std::memcpy(&val, &in_valueBits, sizeof(val));
    return AK::SoundEngine::SetRTPCValue(
        in_rtpcID, val, in_gameObjectID,
        in_uValueChangeDuration, in_eFadeCurve,
        in_bBypassInternalValueInterpolation);
}

AKRESULT wwiseShim_SetRTPCValueByPlayingID(
    AkRtpcID        in_rtpcID,
    uint32_t        in_valueBits, // float bits
    AkPlayingID     in_playingID,
    AkTimeMs        in_uValueChangeDuration,
    AkCurveInterpolation in_eFadeCurve,
    bool            in_bBypassInternalValueInterpolation)
{
    AkRtpcValue val;
    std::memcpy(&val, &in_valueBits, sizeof(val));
    return AK::SoundEngine::SetRTPCValueByPlayingID(
        in_rtpcID, val, in_playingID,
        in_uValueChangeDuration, in_eFadeCurve,
        in_bBypassInternalValueInterpolation);
}

AKRESULT wwiseShim_SetGameObjectOutputBusVolume(
    AkGameObjectID  in_emitterObjID,
    AkGameObjectID  in_listenerObjID,
    uint32_t        in_fControlValueBits) // float bits
{
    AkReal32 val;
    std::memcpy(&val, &in_fControlValueBits, sizeof(val));
    return AK::SoundEngine::SetGameObjectOutputBusVolume(
        in_emitterObjID, in_listenerObjID, val);
}

AKRESULT wwiseShim_GetRTPCValue(
    AkRtpcID        in_rtpcID,
    AkGameObjectID  in_gameObjectID,
    AkPlayingID     in_playingID,
    uint32_t*       out_valueBits, // float bits out
    AK::SoundEngine::Query::RTPCValue_type* io_rValueType)
{
    AkRtpcValue val = 0.0f;
    AKRESULT result = AK::SoundEngine::Query::GetRTPCValue(
        in_rtpcID, in_gameObjectID, in_playingID, val, *io_rValueType);
    std::memcpy(out_valueBits, &val, sizeof(val));
    return result;
}

}
