/**
* File: timerUtility.h
*
* Author: Kevin M. Karol
* Created: 2/5/18
*
* Description: Keep track of information relating to the user facing
* timer utility
*
* Copyright: Anki, Inc. 2018
*
**/

#include "engine/aiComponent/timerUtility.h"
#include "engine/aiComponent/timerUtilityDevFunctions.h"
#include "coretech/common/engine/utils/timer.h"

#include "util/console/consoleInterface.h"

#include <ctime>
#include <ratio>
#include <chrono>
#include <unistd.h>

namespace {
  constexpr const char* kTimerPath = "/data/timer";

  bool WriteTimerFile(int remaining_s) {
    FILE* f = fopen(kTimerPath, "w");
    if (!f) {
      return false;
    }
    const time_t now = time(nullptr);
    fprintf(f, "remaining_s=%d\nsaved_epoch_s=%ld\n", remaining_s, now);
    fclose(f);
    return true;
  }

  bool ReadTimerFile(int& outRemaining_s, time_t& outSavedEpoch) {
    FILE* f = fopen(kTimerPath, "r");
    if (!f) {
      return false;
    }

    int r = -1;
    long t = 0;
    const int scanned = fscanf(f, "remaining_s=%d\nsaved_epoch_s=%ld\n", &r, &t);
    fclose(f);

    if (scanned != 2 || r <= 0) {
      return false;
    }
    outRemaining_s = r;
    outSavedEpoch = static_cast<time_t>(t);
    return true;
  }

  void DeleteTimerFile() {
    remove(kTimerPath);
  }
}


namespace Anki {
namespace Vector {
  
namespace{
Anki::Vector::TimerUtility* sTimerUtility = nullptr;
}

#if ANKI_DEV_CHEATS
CONSOLE_VAR(u32, kAdvanceTimerSeconds,   "TimerUtility.AdvanceTimerSeconds", 60);
CONSOLE_VAR(u32, kAdvanceTimerAndAnticSeconds,   "TimerUtility.AdvanceTimerAndAnticSeconds", 60);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void AdvanceTimer(ConsoleFunctionContextRef context)
{ 
  AdvanceTimerBySeconds(kAdvanceTimerSeconds);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void AdvanceTimerAndAntic(ConsoleFunctionContextRef context)
{  
  AdvanceTimerBySeconds(kAdvanceTimerAndAnticSeconds);
  AdvanceAnticBySeconds(kAdvanceTimerAndAnticSeconds);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void AdvanceTimerBySeconds(int seconds)
{  
  if(sTimerUtility != nullptr){
    sTimerUtility->AdvanceTimeBySeconds(seconds);
  }
}



CONSOLE_FUNC(AdvanceTimer, "TimerUtility.AdvanceTimer");
CONSOLE_FUNC(AdvanceTimerAndAntic, "TimerUtility.AdvanceTimerAndAntic");
#endif // ANKI_DEV_CHEATS


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
int TimerHandle::GetSystemTime_s()
{
  using namespace std::chrono;
  auto time_s = duration_cast<seconds>(steady_clock::now().time_since_epoch()).count();
  return static_cast<int>(time_s);
}

void TimerUtility::PersistTimerIfNeeded()
{
  if(!_activeTimer) return;

  const int now_s = GetSystemTime_s();
  if(now_s - _lastPersistTime_s < 10) return;

  PersistTimerNow();
  _lastPersistTime_s = now_s;
}

void TimerUtility::PersistTimerNow()
{
  if(!_activeTimer) return;
  const int remaining = _activeTimer->GetTimeRemaining_s();
  if(remaining > 0){
    WriteTimerFile(remaining);
  }
}

void TimerUtility::ClearPersistedTimerFile()
{
  DeleteTimerFile();
}

void TimerUtility::LoadPersistedTimerIfAllowed()
{
  int remaining_s = 0;
  time_t savedEpoch = 0;

  if(!ReadTimerFile(remaining_s, savedEpoch)){
    DeleteTimerFile();
    return;
  }

  const time_t now = time(nullptr);

  tm local{};
  localtime_r(&now, &local);

  const bool isMaintenance = ( access( "/run/maintenance_reboot", F_OK ) != -1 );

  if(!isMaintenance){
    DeleteTimerFile();
    return;
  }

  constexpr int kMin = 1;
  constexpr int kMax = 24 * 60 * 60;
  if(remaining_s < kMin || remaining_s > kMax){
    DeleteTimerFile();
    return;
  }

  _activeTimer = std::make_shared<TimerHandle>(remaining_s);
  _restoredThisBoot = true;

  PersistTimerNow();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// TimerUtility
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
TimerUtility::TimerUtility()
: IDependencyManagedComponent<AIComponentID>(this, AIComponentID::TimerUtility){
  if( sTimerUtility != nullptr ) {
    PRINT_NAMED_WARNING("TimerUtility.Constructor.MultipleInstances","TimerUtility instance exists already");
  }
  sTimerUtility = this;
  LoadPersistedTimerIfAllowed();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
TimerUtility::~TimerUtility()
{
  sTimerUtility = nullptr;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
TimerUtility::SharedHandle TimerUtility::StartTimer(int timerLength_s)
{
  ANKI_VERIFY(_activeTimer == nullptr,
              "TimerUtility.StartTimer.TimerAlreadySet", 
              "Current design says we don't overwrite timers - remove this verify if that changes");
  _activeTimer = std::make_shared<TimerHandle>(timerLength_s);
  return _activeTimer;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TimerUtility::ClearTimer()
{
  _activeTimer.reset();
  ClearPersistedTimerFile();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
int TimerUtility::GetSystemTime_s() const
{
  using namespace std::chrono;
  auto time_s = duration_cast<seconds>(steady_clock::now().time_since_epoch()).count();
  return static_cast<int>(time_s);
}


} // namespace Vector
} // namespace Anki
