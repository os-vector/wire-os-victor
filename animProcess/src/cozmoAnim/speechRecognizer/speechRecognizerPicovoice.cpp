// picovoice porcupine 1.5.0

#include "speechRecognizerPicovoice.h"

#include "audioUtil/speechRecognizer.h"
#include "picovoice.h"
#include "pv_porcupine.h"
#include "util/logging/logging.h"

#include <algorithm>
#include <vector>
#include <mutex>

#include <chrono>
#include <thread>
#include <cstdlib>
#include <unistd.h>
#include <fstream>
#include <sstream>

#include <sys/stat.h>

const char* model_path   = "/anki/data/assets/cozmo_resources/assets/picovoice/porcupine_params.pv";
const char* custom_ppn   = "/data/data/com.anki.victor/persistent/picovoice/custom_1-5-0.ppn";
const char* default_ppn  = "/anki/data/assets/cozmo_resources/assets/picovoice/hey_vector.ppn";
const char* alexa_ppn    = "/anki/data/assets/cozmo_resources/assets/picovoice/alexa.ppn";
const char* sensitivity_path = "/data/data/com.anki.victor/persistent/picovoice/sensitivity2";
float default_sensitivity = 0.45f;
const uint32_t frame_length = 512;
const uint64_t keyword_length_samples = 16 * 800;

namespace Anki {
namespace Vector {

#define LOG_CHANNEL "SpeechRecognizer"

struct SpeechRecognizerPicovoice::SpeechRecognizerPicovoiceData
{
  std::vector<AudioUtil::AudioSample> audioBuffer;
  std::vector<std::string> keywords;
  uint64_t sampleCount = 0;
  pv_porcupine_object_t* pvObj = nullptr;
  std::recursive_mutex recogMutex;
  bool disabled = true;
  bool reset = false;
};

SpeechRecognizerPicovoice::SpeechRecognizerPicovoice()
  : _impl(new SpeechRecognizerPicovoiceData())
{
}

SpeechRecognizerPicovoice::~SpeechRecognizerPicovoice()
{
  if (_impl->pvObj)
  {
    pv_porcupine_delete(_impl->pvObj);
    _impl->pvObj = nullptr;
  }
}

SpeechRecognizerPicovoice::SpeechRecognizerPicovoice(SpeechRecognizerPicovoice&& other)
  : AudioUtil::SpeechRecognizer(std::move(other)),
    _impl(std::move(other._impl))
{
}

SpeechRecognizerPicovoice& SpeechRecognizerPicovoice::operator=(SpeechRecognizerPicovoice&& other)
{
  AudioUtil::SpeechRecognizer::operator=(std::move(other));
  _impl = std::move(other._impl);
  return *this;
}

bool SpeechRecognizerPicovoice::Init(bool useHeyVector, bool useAlexa, float alexaSensitivity)
{
  std::lock_guard<std::recursive_mutex> lock(_impl->recogMutex);

  const char* ppn_to_use = default_ppn;
  struct stat st{};

  _impl->audioBuffer.reserve(frame_length * 2);

  if (_impl->pvObj)
  {
    pv_porcupine_delete(_impl->pvObj);
    _impl->pvObj = nullptr;
  }

  if (stat(custom_ppn, &st) == 0) {
      ppn_to_use = custom_ppn;
  }

  float sensitivity = default_sensitivity;

  std::ifstream sensFile(sensitivity_path);
  if (sensFile.is_open()) {
    std::string line;
    if (std::getline(sensFile, line)) {
      std::istringstream iss(line);
      float val = 0.0f;
      if ((iss >> val) && val >= 0.0f && val <= 1.0f) {
        sensitivity = val;
      }
    }
  }


  std::vector<const char*> ppns;
  std::vector<float> sensitivities;
  _impl->keywords.clear();
  if (useHeyVector) {
    ppns.push_back(ppn_to_use);
    sensitivities.push_back(sensitivity);
    _impl->keywords.push_back(kHeyVectorKeyword);
  }
  if (useAlexa) {
    ppns.push_back(alexa_ppn);
    sensitivities.push_back(alexaSensitivity);
    _impl->keywords.push_back(kAlexaKeyword);
  }

  const auto init = [&]() {
    return pv_porcupine_multiple_keywords_init(model_path, (int) ppns.size(), ppns.data(), sensitivities.data(),
                                               &_impl->pvObj);
  };

  LOG_INFO("SpeechRecognizerPicovoice.Init", "Using sensitivity: %.4f, alexa sensitivity: %.4f",
           sensitivity, alexaSensitivity);
  pv_status_t status = init();

  if (status != PV_STATUS_SUCCESS) {
      if (useHeyVector && ppn_to_use == custom_ppn) {
          LOG_INFO("SpeechRecognizerPicovoice.Init", "loading default pv model");
          ppns.front() = default_ppn;
          status = init();
      }
  }

  if (status != PV_STATUS_SUCCESS) {
      if (useHeyVector && useAlexa) {
          LOG_WARNING("SpeechRecognizerPicovoice.Init", "loading without alexa keyword");
          ppns.pop_back();
          sensitivities.pop_back();
          _impl->keywords.pop_back();
          status = init();
      }
  }

  if (status != PV_STATUS_SUCCESS) {
      LOG_ERROR("SpeechRecognizerPicovoice.Init", "error setting up recognizer :(");
      _impl->pvObj = nullptr;
      _impl->keywords.clear();
      return false;
  }

  LOG_INFO("SpeechRecognizerPicovoice.Init", "Picovoice set up successfully!");
  _impl->disabled = false;

  return true;
}

void SpeechRecognizerPicovoice::Update(const AudioUtil::AudioSample* audioData, unsigned int audioDataLen)
{
    std::lock_guard<std::recursive_mutex> lock(_impl->recogMutex);

    if (_impl->disabled || _impl->pvObj == nullptr)
    {
        return;
    }

    _impl->audioBuffer.insert(_impl->audioBuffer.end(), audioData, audioData + audioDataLen);
    _impl->sampleCount += audioDataLen;

    while (_impl->audioBuffer.size() >= frame_length)
    {
        int keywordIndex = -1;
        if (pv_porcupine_multiple_keywords_process(_impl->pvObj, _impl->audioBuffer.data(), &keywordIndex) != PV_STATUS_SUCCESS) {
            LOG_ERROR("SpeechRecognizerPicovoice.Update", "pv process error");
            return;
        }
        if (keywordIndex >= 0 && keywordIndex < (int) _impl->keywords.size()) {
            const uint64_t endSampleIdx = _impl->sampleCount - (_impl->audioBuffer.size() - frame_length);
            const uint64_t beginSampleIdx = endSampleIdx - std::min(endSampleIdx, keyword_length_samples);
            AudioUtil::SpeechRecognizerCallbackInfo info{
                .result = _impl->keywords[keywordIndex],
                .startTime_ms = static_cast<int>(beginSampleIdx / 16),
                .endTime_ms = static_cast<int>(endSampleIdx / 16),
                .startSampleIndex = beginSampleIdx,
                .endSampleIndex = endSampleIdx,
                .score = 0.0f
            };
            DoCallback(info);
        }
        _impl->audioBuffer.erase(_impl->audioBuffer.begin(),
                                 _impl->audioBuffer.begin() + frame_length);
    }
}

void SpeechRecognizerPicovoice::Reset()
{
  std::lock_guard<std::recursive_mutex> lock(_impl->recogMutex);
  _impl->reset = true;
}

bool SpeechRecognizerPicovoice::HasKeyword(const std::string& keyword) const
{
  std::lock_guard<std::recursive_mutex> lock(_impl->recogMutex);
  return std::find(_impl->keywords.begin(), _impl->keywords.end(), keyword) != _impl->keywords.end();
}

uint64_t SpeechRecognizerPicovoice::GetSampleCount() const
{
  std::lock_guard<std::recursive_mutex> lock(_impl->recogMutex);
  return _impl->sampleCount;
}

void SpeechRecognizerPicovoice::StartInternal()
{
  _impl->disabled = false;
}

void SpeechRecognizerPicovoice::StopInternal()
{
  _impl->disabled = true;
}

} // end namespace Vector
} // end namespace Anki
