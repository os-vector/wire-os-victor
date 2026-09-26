/**
* File: speechRecognizerSystem.h
*
* Author: Jordan Rivas
* Created: 10/23/2018
*
* Description: Speech Recognizer System handles high level speech features, such as, locale and multiple triggers
*
* Copyright: Anki, Inc. 2018
*
*/

#ifndef __AnimProcess_VictorAnim_SpeechRecognizerSystem_H_
#define __AnimProcess_VictorAnim_SpeechRecognizerSystem_H_

#include "audioUtil/audioDataTypes.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace Anki {
  namespace AudioUtil {
    class SpeechRecognizer;
    struct SpeechRecognizerCallbackInfo;
    struct SpeechRecognizerIgnoreReason;
  }
  namespace Vector {
    class Alexa;
    class AlexaPlaybackRecognizerComponent;
    namespace Anim {
      class AnimContext;
    }
    class NotchDetector;
    class SpeechRecognizerPicovoice;
  }
  namespace Util {
    class Locale;
  }
}

namespace Anki {
namespace Vector {

class SpeechRecognizerSystem
{
public:

  friend class AlexaPlaybackRecognizerComponent;

  SpeechRecognizerSystem(const Anim::AnimContext* context);
  
  ~SpeechRecognizerSystem();
  
  SpeechRecognizerSystem(const SpeechRecognizerSystem& other) = delete;
  SpeechRecognizerSystem& operator=(const SpeechRecognizerSystem& other) = delete;
  
  using TriggerWordDetectedCallback = std::function<void(const AudioUtil::SpeechRecognizerCallbackInfo& info)>;
  using AlexaTriggerWordDetectedCallback = std::function<void(const AudioUtil::SpeechRecognizerCallbackInfo& info,
                                                              const AudioUtil::SpeechRecognizerIgnoreReason& reason)>;
  
  // Init Vector trigger detector
  // Note: This always happens at boot
  void InitVector(TriggerWordDetectedCallback callback);

  // set whether the notch detector should be active (for alexa keyword only). When active,
  // alexa triggers get dropped if we detect a notch.
  void ToggleNotchDetector(bool active);
  
  // add 'raw' audio samples
  void UpdateNotch(const AudioUtil::AudioSample* audioChunk, unsigned int audioDataLen);

  // Update recognizer audio
  // NOTE: Always call from the same thread
  void Update(const AudioUtil::AudioSample * audioData, unsigned int audioDataLen, bool vadActive);
  
  void UpdateTriggerForLocale(const Util::Locale& newLocale);
  
  // Alexa Methods
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Alexa has been set active set current locale and callback for Alexa trigger recognitions
  void ActivateAlexa(const Util::Locale& locale, AlexaTriggerWordDetectedCallback callback);
  
  // Alexa has been disabled, turn off the "Alexa" recognizer
  void DisableAlexa();
  
  // Start/Stop playback recognizer when Alexa is in Speaking state
  void SetAlexaSpeakingState(bool isSpeaking);
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -


private:

  const Anim::AnimContext*                    _context = nullptr;
  std::unique_ptr<SpeechRecognizerPicovoice>  _victorTrigger;

  TriggerWordDetectedCallback                 _alexaTrigger;
  Alexa*                                      _alexaComponent = nullptr;
  bool                                        _isAlexaActive = false;
  std::atomic_bool                            _isAlexaLocaleEnabled{ false };

  std::unique_ptr<SpeechRecognizerPicovoice>  _alexaPlaybackTrigger;
  std::atomic_uint64_t                        _playbackTrigerSampleIdx{ 0 };
  std::atomic_bool                            _isDisableAlexaPending{ false };

  std::unique_ptr<AlexaPlaybackRecognizerComponent>   _alexaPlaybackRecognizerComponent;
  
  std::shared_ptr<NotchDetector>              _notchDetector;
  std::mutex                                  _notchMutex;
  bool                                        _notchDetectorActive = false;
  
  // Alexa Methods
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Init Alexa trigger detector
  // Note: This is done after Alex user has been authicated
  void InitAlexa(const AlexaTriggerWordDetectedCallback callback);

  // Init Alex playback trigger detector
  void InitAlexaPlayback(TriggerWordDetectedCallback callback);

  // Check Alexa component states to update _isAlexaActive flag
  void UpdateAlexaActiveState();
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
};


} // end namespace Vector
} // end namespace Anki

#endif // __AnimProcess_VictorAnim_SpeechRecognizerSystem_H_