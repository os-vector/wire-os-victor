/**
 * File: speechRecognizerSystem.cpp
 *
 * Author: Jordan Rivas
 * Created: 10/23/2018
 *
 * Description: Speech Recognizer System handles high level speech features, such as, locale and multiple triggers
 *
 * Copyright: Anki, Inc. 2018
 *
 */

#include "cozmoAnim/speechRecognizer/speechRecognizerSystem.h"

#include "audioUtil/speechRecognizer.h"
#include "cozmoAnim/alexa/alexa.h"
#include "cozmoAnim/alexa/media/alexaPlaybackRecognizerComponent.h"
#include "cozmoAnim/animContext.h"
#include "cozmoAnim/micData/micDataSystem.h"
#include "speechRecognizerPicovoice.h"                        // swapped in Picovoice
#include "cozmoAnim/micData/notchDetector.h"
#include "util/console/consoleInterface.h"
#include "util/environment/locale.h"
#include "util/logging/logging.h"

#include <fcntl.h>
#include <unistd.h>


namespace Anki {
namespace Vector {

// VIC-13319 remove
CONSOLE_VAR_EXTERN(bool, kAlexaEnabledInUK);
CONSOLE_VAR_EXTERN(bool, kAlexaEnabledInAU);

namespace {
#define LOG_CHANNEL "SpeechRecognizer"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Console Vars
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
#define CONSOLE_GROUP_ALEXA "SpeechRecognizer.Alexa"
#define CONSOLE_GROUP_ALEXA_PLAYBACK "SpeechRecognizer.AlexaPlayback"

CONSOLE_VAR_RANGED(float, kAlexaSensitivity, CONSOLE_GROUP_ALEXA, 0.5f, 0.0f, 1.0f);
CONSOLE_VAR(bool, kSaveRawMicInput, CONSOLE_GROUP_ALEXA, false);
// 0: don't run; 1: compute power as if _notchDetectorActive; 2: analyze power every tick
CONSOLE_VAR_RANGED(unsigned int, kForceRunNotchDetector, CONSOLE_GROUP_ALEXA, 0, 0, 2);

CONSOLE_VAR_RANGED(uint, kPlaybackRecognizerSampleCountThreshold, CONSOLE_GROUP_ALEXA_PLAYBACK, 5000, 1000, 10000);

bool AlexaLocaleEnabled(const Util::Locale& locale)
{
  if (locale.GetCountry() == Util::Locale::CountryISO2::US) {
    return true;
  }
  else if (locale.GetCountry() == Util::Locale::CountryISO2::GB) {
    return kAlexaEnabledInUK;
  }
  else if (locale.GetCountry() == Util::Locale::CountryISO2::AU) {
    return kAlexaEnabledInAU;
  }
  else {
    return false;
  }
}

} // namespace

SpeechRecognizerSystem::SpeechRecognizerSystem(const Anim::AnimContext* context)
: _context(context)
, _notchDetector(std::make_shared<NotchDetector>())
{
}

SpeechRecognizerSystem::~SpeechRecognizerSystem()
{
  if (_victorTrigger) {
    _victorTrigger->Stop();
  }

  // Best way to destroy Alexa recognizer and component
  DisableAlexa();
}

void SpeechRecognizerSystem::InitVector(TriggerWordDetectedCallback callback)
{
  if (_victorTrigger) {
    LOG_WARNING("SpeechRecognizerSystem.InitVector", "Victor Recognizer is already running");
    return;
  }

  _victorTrigger = std::make_unique<SpeechRecognizerPicovoice>();
  _victorTrigger->Init(true, true, kAlexaSensitivity);
  _victorTrigger->SetCallback([callback=std::move(callback), this](const AudioUtil::SpeechRecognizerCallbackInfo& info)
  {
    if (info.result != SpeechRecognizerPicovoice::kAlexaKeyword) {
      callback(info);
    }
    else if (_isAlexaActive && !_isDisableAlexaPending && _alexaTrigger) {
      _alexaTrigger(info);
    }
  });
}

void SpeechRecognizerSystem::ToggleNotchDetector(bool active)
{
  _notchDetectorActive = active;
}

void SpeechRecognizerSystem::UpdateNotch(const AudioUtil::AudioSample* audioChunk, unsigned int audioDataLen)
{
  {
    std::lock_guard<std::mutex> lg{_notchMutex};
    const bool analyzeSamples = _notchDetectorActive || (kForceRunNotchDetector != 0);
    _notchDetector->AddSamples(audioChunk, audioDataLen/MicData::kNumInputChannels, analyzeSamples);
    if ( kForceRunNotchDetector == 2 ) {
      _notchDetector->HasNotch();
    }
  }

  if( ANKI_DEV_CHEATS ) {
    static int pcmfd = -1;
    if( (pcmfd < 0) && kSaveRawMicInput ) {
      const auto path = "/data/data/com.anki.victor/cache/speechRecognizerRaw.pcm";
      pcmfd = open( path, O_CREAT|O_RDWR|O_TRUNC, 0644 );
    }

    if( pcmfd >= 0 ) {
      std::vector<short> toSave;
      toSave.resize(audioDataLen/MicData::kNumInputChannels);
      for( unsigned int i=0, idx=0; i<audioDataLen; i+=MicData::kNumInputChannels, ++idx ) {
        toSave[idx] = audioChunk[i];
      }
      (void) write( pcmfd, toSave.data(), toSave.size() * sizeof(short) );
      if( !kSaveRawMicInput ) {
        close( pcmfd );
        pcmfd = -1;
      }
    }
  }
}

void SpeechRecognizerSystem::Update(const AudioUtil::AudioSample * audioData, unsigned int audioDataLen, bool vadActive)
{
  if (_isAlexaActive) {
    if (!_isDisableAlexaPending) {
      _alexaComponent->AddMicrophoneSamples(audioData, audioDataLen);
    }
    else {
      _alexaTrigger = nullptr;
      UpdateAlexaActiveState();
      ASSERT_NAMED(!_isAlexaActive, "SpeechRecognizerSystem.DisableAlexa._isAlexaActive.IsTrue");
      _isDisableAlexaPending = false;
      LOG_INFO("SpeechRecognizerSystem.Update", "Alexa mic recognizer has been disabled");
    }
  }

  // Update recognizer
  if (_victorTrigger && (vadActive || _isAlexaActive)) {
    _victorTrigger->Update(audioData, audioDataLen);
  }
}

void SpeechRecognizerSystem::UpdateTriggerForLocale(const Util::Locale& newLocale)
{
  _isAlexaLocaleEnabled = AlexaLocaleEnabled(newLocale);
  UpdateAlexaActiveState();
}

void SpeechRecognizerSystem::ActivateAlexa(const Util::Locale& locale, AlexaTriggerWordDetectedCallback callback)
{
  if (_isAlexaActive) {
    LOG_WARNING("SpeechRecognizerSystem.ActivateAlexa",
                "Alexa is already active, must call DisableAlexa() to change state");
    return;
  }

  _alexaComponent = _context->GetAlexa();
  _isAlexaLocaleEnabled = AlexaLocaleEnabled(locale);

  InitAlexa(callback);

  _alexaPlaybackRecognizerComponent.reset(new AlexaPlaybackRecognizerComponent(_context, *this));

  const auto playbackRecognizerCallback = [this](const AudioUtil::SpeechRecognizerCallbackInfo& info)
  {
    _playbackTrigerSampleIdx = _alexaComponent->GetMicrophoneSampleIndex();
  };
  InitAlexaPlayback(playbackRecognizerCallback);

  if (!_alexaPlaybackRecognizerComponent->Init()) {
    _alexaPlaybackRecognizerComponent.reset();
    LOG_ERROR("SpeechRecognizerSystem.ActivateAlexa._alexaPlaybackRecognizerComponent.Init.Failed", "");
  }

  UpdateAlexaActiveState();
}

void SpeechRecognizerSystem::DisableAlexa()
{
  _isDisableAlexaPending = true;

  if (_alexaPlaybackRecognizerComponent) {
    _alexaPlaybackRecognizerComponent.reset();
  }

  if (_alexaPlaybackTrigger) {
    _alexaPlaybackTrigger->Stop();
    _alexaPlaybackTrigger.reset();
  }
}

void SpeechRecognizerSystem::SetAlexaSpeakingState(bool isSpeaking)
{
  if (_alexaPlaybackRecognizerComponent) {
    _alexaPlaybackRecognizerComponent->SetRecognizerActivate(isSpeaking);
  }
}

void SpeechRecognizerSystem::InitAlexa(const AlexaTriggerWordDetectedCallback callback)
{
  if (_alexaTrigger) {
    LOG_WARNING("SpeechRecognizerSystem.InitAlexa", "Alexa Recognizer is already running");
    return;
  }

  _alexaComponent = _context->GetAlexa();
  ASSERT_NAMED(_alexaComponent != nullptr, "SpeechRecognizerSystem.InitAlexa._context.GetAlexa.IsNull");

  _alexaTrigger = [callback=std::move(callback), this](const AudioUtil::SpeechRecognizerCallbackInfo& recognizerInfo)
  {
    AudioUtil::SpeechRecognizerCallbackInfo info = recognizerInfo;
    const uint64_t sampleOffset = _alexaComponent->GetMicrophoneSampleIndex() - _victorTrigger->GetSampleCount();
    info.startSampleIndex += sampleOffset;
    info.endSampleIndex += sampleOffset;
    info.startTime_ms = static_cast<int>(info.startSampleIndex / 16);
    info.endTime_ms = static_cast<int>(info.endSampleIndex / 16);

    AudioUtil::SpeechRecognizerIgnoreReason ignoreReason;
    if (_notchDetectorActive || kForceRunNotchDetector) {
      std::lock_guard<std::mutex> lg{_notchMutex};
      ignoreReason.notch = _notchDetector->HasNotch();
    }
    const auto diff = info.endSampleIndex - _playbackTrigerSampleIdx;
    ignoreReason.playback = (diff <= kPlaybackRecognizerSampleCountThreshold);

    if (ignoreReason) {
      LOG_INFO("SpeechRecognizerSystem.InitAlexaCallback.Ignored",
               "Alexa wake word contained a notch '%c' or playback recognizer '%c'"
               " samples between playback and user recognizers %llu samples | %llu ms",
               ignoreReason.notch ? 'Y' : 'N', ignoreReason.playback ? 'Y' : 'N', diff, (diff/16));
    }
    callback(info, ignoreReason);
  };
}

void SpeechRecognizerSystem::InitAlexaPlayback(TriggerWordDetectedCallback callback)
{
  if (_alexaPlaybackTrigger) {
    LOG_WARNING("SpeechRecognizerSystem.InitAlexaPlayback", "Alexa Playback Recognizer is already running");
    return;
  }

  _alexaPlaybackTrigger = std::make_unique<SpeechRecognizerPicovoice>();
  _alexaPlaybackTrigger->SetCallback(callback);
  _alexaPlaybackTrigger->Init(false, true, 1.0f);
}

void SpeechRecognizerSystem::UpdateAlexaActiveState()
{
  _isAlexaActive = (_alexaComponent != nullptr &&
                    _alexaTrigger &&
                    _isAlexaLocaleEnabled &&
                    _victorTrigger &&
                    _victorTrigger->HasKeyword(SpeechRecognizerPicovoice::kAlexaKeyword));
}

} // end namespace Vector
} // end namespace Anki
