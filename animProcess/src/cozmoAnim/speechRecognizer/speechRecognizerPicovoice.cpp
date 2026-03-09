// picovoice porcupine 1.5.0

#include "speechRecognizerPicovoice.h"

#include "audioUtil/speechRecognizer.h"
#include "picovoice.h"
#include "pv_porcupine.h"
#include "util/logging/logging.h"

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
const char* sensitivity_path = "/data/data/com.anki.victor/persistent/picovoice/sensitivity2";
float default_sensitivity = 0.45f;

namespace Anki {
namespace Vector {

#define LOG_CHANNEL "SpeechRecognizer"

struct SpeechRecognizerPicovoice::SpeechRecognizerPicovoiceData
{
  std::vector<AudioUtil::AudioSample> audioBuffer;
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

bool SpeechRecognizerPicovoice::Init()
{
  std::lock_guard<std::recursive_mutex> lock(_impl->recogMutex);

  // pid_t pid = fork();
  // if (pid < 0) {
  //   LOG_ERROR("SpeechRecognizerPicovoice.Init", "Failed to fork process for pv_server");
  //   return false;
  // } else if (pid == 0) {
  //   execl("/anki/bin/pv_server", "pv_server", (char*)nullptr);
  //   LOG_ERROR("SpeechRecognizerPicovoice.Init", "Failed to exec pv_server");
  //   std::exit(EXIT_FAILURE);
  // } else {
  //   const char* socketPath = "/dev/socket/_anim_pv_wakeword_";
  //   int maxRetries = 100;
  //   int retries = 0;
  //   while (retries < maxRetries) {
  //     if (access(socketPath, F_OK) == 0) {
  //       std::this_thread::sleep_for(std::chrono::milliseconds(300));
  //       break;
  //     }
  //     std::this_thread::sleep_for(std::chrono::milliseconds(50));
  //     ++retries;
  //   }

  //   if (retries == maxRetries) {
  //     LOG_ERROR("SpeechRecognizerPicovoice.Init", "Timeout waiting for pv_server to create socket");
  //     return false;
  //   }
  // }

  const char* ppn_to_use = default_ppn;
  struct stat st{};

  _impl->audioBuffer.reserve(512 * 2);

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


  LOG_INFO("SpeechRecognizerPicovoice.Init", "Using sensitivity: %.4f", sensitivity);
  pv_status_t status = pv_porcupine_init(model_path, ppn_to_use, sensitivity, &_impl->pvObj);

  if (status != PV_STATUS_SUCCESS) {
      if (ppn_to_use == custom_ppn) {
          LOG_INFO("SpeechRecognizerPicovoice.Init", "loading default pv model");
          ppn_to_use = default_ppn;
          status = pv_porcupine_init(model_path, ppn_to_use, sensitivity, &_impl->pvObj);
      }
  }

  if (status != PV_STATUS_SUCCESS) {
      LOG_ERROR("SpeechRecognizerPicovoice.Init", "error setting up recognizer :(");
      return false;
  }

  LOG_INFO("SpeechRecognizerPicovoice.Init", "Picovoice set up successfully!");
  _impl->disabled = false;

  return true;
}

void SpeechRecognizerPicovoice::Update(const AudioUtil::AudioSample* audioData, unsigned int audioDataLen)
{
    std::lock_guard<std::recursive_mutex> lock(_impl->recogMutex);

    if (_impl->disabled)
    {
        return;
    }
    uint32_t frameLength = 512;

    _impl->audioBuffer.insert(_impl->audioBuffer.end(), audioData, audioData + audioDataLen);

    while (_impl->audioBuffer.size() >= frameLength)
    {
        std::vector<AudioUtil::AudioSample> frame(_impl->audioBuffer.begin(),
                                                  _impl->audioBuffer.begin() + frameLength);

        bool result = false;
        if (pv_porcupine_process(_impl->pvObj, frame.data(), &result) != PV_STATUS_SUCCESS) {
            LOG_ERROR("SpeechRecognizerPicovoice.Update", "pv process error");
            return;
        }
        if (result) {
            AudioUtil::SpeechRecognizerCallbackInfo info{
                .result = "Wake word detected",
                .startTime_ms = 0,
                .endTime_ms = 0,
                .score = 0.0f
            };
            DoCallback(info);
        }
        _impl->audioBuffer.erase(_impl->audioBuffer.begin(),
                                 _impl->audioBuffer.begin() + frameLength);
    }
}

void SpeechRecognizerPicovoice::Reset()
{
  std::lock_guard<std::recursive_mutex> lock(_impl->recogMutex);
  _impl->reset = true;
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
