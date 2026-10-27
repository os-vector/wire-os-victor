#if FACE_TRACKER_PROVIDER == FACE_TRACKER_YUNET

#include "faceRecognizer_yunet.h"

#include "coretech/common/engine/jsonTools.h"
#include "coretech/vision/engine/compressedImage.h"

#include "util/console/consoleInterface.h"
#include "util/fileUtils/fileUtils.h"
#include "util/helpers/boundedWhile.h"
#include "util/logging/DAS.h"
#include "util/logging/logging.h"
#include "util/threading/threadPriority.h"

#include <fstream>
#include <iterator>

#define LOG_CHANNEL "FaceRecognizer"

namespace Anki {
namespace Vision {

static const u16 VersionNumber = Util::numeric_cast<u16>(FaceRecognitionConstants::EnrolledFaceStorageVersionNumber);
static const u16 VersionPrefix[2] = {0xFACE, VersionNumber};

CONSOLE_VAR_RANGED(s32, kFaceRecognitionThreshold, "Vision.FaceRecognition", 650, 0, 1000);

CONSOLE_VAR_RANGED(s32, kFaceVerificationThreshold, "Vision.FaceRecognition", 550, 0, 1000);

CONSOLE_VAR_RANGED(s32, kFaceRecognitionGuessThreshold, "Vision.FaceRecognition", 550, 0, 1000);

CONSOLE_VAR_RANGED(s32, kFaceEnrollmentUpdateThreshold, "Vision.FaceRecognition", 750, 0, 1000);

CONSOLE_VAR_RANGED(s32, kFaceVerifyFailuresBeforeDrop, "Vision.FaceRecognition", 3, 1, 20);

CONSOLE_VAR_RANGED(s32, kFaceRecognitionThresholdMarginForAdding, "Vision.FaceRecognition", 150, 0, 1000);

CONSOLE_VAR_RANGED(s32, kFaceRecognitionThresholdMarginForUsing2ndBest, "Vision.FaceRecognition", 50, 0, 1000);

CONSOLE_VAR(f32, kTimeBetweenFaceEnrollmentUpdates_sec, "Vision.FaceRecognition", 0.5f);

CONSOLE_VAR(bool, kGetEnrollmentTimeFromImageTimestamp, "Vision.FaceRecognition", false);

CONSOLE_VAR(bool, kEnableEnrollmentAfterFull, "Vision.FaceRecognition", false);

CONSOLE_VAR_RANGED(u8, kFaceRecMaxDebugResults, "Vision.FaceRecognition", 3, 2, 10);

CONSOLE_VAR(bool, kFaceRecognitionExtraDebug, "Vision.FaceRecognition", false);

CONSOLE_VAR(bool, kGatherDebugEnrollmentImages, "Vision.FaceRecognition", false);

namespace JsonKey
{
  const char* FaceRecognitionGroup = "FaceRecognition";
  const char* RunMode = "RunMode";
  const char* Synchronous = "synchronous";
  const char* Asynchronous = "asynchronous";

  const char* PerformanceLoggingGroup = "PerformanceLogging";
  const char* TimeBetweenInfoPrints = "TimeBetweenProfilerInfoPrints_sec";
  const char* TimeBetweenDasEvents = "TimeBetweenProfilerDasLogs_sec";
}

inline static s64 GetSecondsSince(std::chrono::time_point<std::chrono::system_clock> now, EnrolledFaceEntry::Time t)
{
  using namespace std::chrono;
  const s64 secondsSince = Util::numeric_cast<s64>(duration_cast<seconds>(now - t).count());
  return secondsSince;
}

template<template<class ...> class Container_T>
inline void EmplaceLoadedKnownFace(const EnrolledFaceEntry& entry,
                                   Container_T<LoadedKnownFace>& faces,
                                   const char* debugLabel = nullptr)
{
  using namespace std::chrono;

  auto const nowTime = system_clock::now();
  const auto lastSeenTime    = entry.FindLastSeenTime();
  const auto secSinceEpoch   = duration_cast<seconds>(lastSeenTime.time_since_epoch()).count();
  const s64 secSinceEnrolled = GetSecondsSince(nowTime, entry.GetEnrollmentTime());
  const s64 secSinceUpdated  = GetSecondsSince(nowTime, entry.GetLastUpdateTime());
  const s64 secSinceSeen     = GetSecondsSince(nowTime, lastSeenTime);

  faces.emplace_back( LoadedKnownFace(secSinceEnrolled,
                                      secSinceUpdated,
                                      secSinceSeen,
                                      secSinceEpoch,
                                      entry.GetFaceID(),
                                      entry.GetName()) );

  if( debugLabel != nullptr ) {
    LOG_INFO(debugLabel,
             "User '%s' with ID=%d. Seconds since: Enrolled=%lld Updated=%lld Seen=%lld",
             entry.GetName().piiGuardedString(),
             entry.GetFaceID(), secSinceEnrolled, secSinceUpdated, secSinceSeen);
  }
}

FaceRecognizer::FaceRecognizer(const Json::Value& config)
{
  if(config.isMember(JsonKey::FaceRecognitionGroup))
  {
    const Json::Value& recognitionConfig = config[JsonKey::FaceRecognitionGroup];

    std::string runModeStr;
    if(JsonTools::GetValueOptional(recognitionConfig, JsonKey::RunMode, runModeStr)) {
      if(runModeStr == JsonKey::Asynchronous) {
        _isRunningAsync = true;
      } else if(runModeStr == JsonKey::Synchronous) {
        _isRunningAsync = false;
      } else {
        DEV_ASSERT(false, "FaceRecognizer.Constructor.BadRunMode");
      }
    }
  } else {
    LOG_WARNING("FaceRecognizer.Constructor.NoFaceRecParameters",
                "Did not find '%s' group in config", JsonKey::FaceRecognitionGroup);
  }

  LOG_INFO("FaceRecognizer.Constructor.RunMode",
           "Running in %s mode",
           (_isRunningAsync ? JsonKey::Asynchronous : JsonKey::Synchronous));

  f32 timeBetweenProfilerInfoPrints_sec = 5.f;
  f32 timeBetweenProfilerDasLogs_sec = 60.f;

  if(config.isMember(JsonKey::PerformanceLoggingGroup))
  {
    const Json::Value& performanceConfig = config[JsonKey::PerformanceLoggingGroup];
    if(!JsonTools::GetValueOptional(performanceConfig, JsonKey::TimeBetweenInfoPrints,
                                    timeBetweenProfilerInfoPrints_sec))
    {
      LOG_WARNING("FaceRecognizer.Constructor.MissingJsonField", "%s.%s",
                  JsonKey::PerformanceLoggingGroup, JsonKey::TimeBetweenInfoPrints);
    }

    if(!JsonTools::GetValueOptional(performanceConfig, JsonKey::TimeBetweenDasEvents,
                                    timeBetweenProfilerDasLogs_sec))
    {
      LOG_WARNING("FaceRecognizer.Constructor.MissingJsonField", "%s.%s",
                  JsonKey::PerformanceLoggingGroup, JsonKey::TimeBetweenDasEvents);
    }
  } else {
    LOG_WARNING("FaceRecognizer.Constructor.NoPerfLoggingParameters",
                "Did not find '%s' group in config", JsonKey::PerformanceLoggingGroup);
  }

  Profiler::SetPrintFrequency(Util::SecToMilliSec(timeBetweenProfilerInfoPrints_sec));
  Profiler::SetDasLogFrequency(Util::SecToMilliSec(timeBetweenProfilerDasLogs_sec));
  Profiler::SetPrintChannelName("FaceRecognizer");
  Profiler::SetProfileGroupName("FaceRecognizer.Profiler");
}

FaceRecognizer::~FaceRecognizer()
{
  Shutdown();
}

Result FaceRecognizer::Init(const FaceNetConfig& netConfig)
{
  const Result result = _embedder.Init(netConfig);
  if(RESULT_OK != result) {
    LOG_ERROR("FaceRecognizer.Init.EmbedderInitFailed", "%s", netConfig.modelPath.c_str());
    return result;
  }

  LOG_INFO("FaceRecognizer.Init.Success", "%d-d embeddings, gpu=%d",
           _embedder.GetEmbeddingSize(), (s32)_embedder.IsUsingGpu());

  _trackingID = UnknownFaceID;
  _isInitialized = true;

  if(_isRunningAsync) {
    StartThread();
  }

  return RESULT_OK;
}

Result FaceRecognizer::Shutdown()
{
  StopThread();
  _album.Clear();
  return RESULT_OK;
}

void FaceRecognizer::StartThread()
{
  if(_isInitialized)
  {
    if(_isRunningAsync)
    {
      StopThread();
    }

    _isRunningAsync = true;

    _featureExtractionThread = std::thread(&FaceRecognizer::Run, this);
  }
  else
  {
    LOG_WARNING("FaceRecognizer.StartThread.NotInitialized", "");
  }
}

void FaceRecognizer::StopThread()
{
  if(_isRunningAsync)
  {
    {
      std::unique_lock<std::mutex> lock{_mutex};
      _isRunningAsync = false;
    }
    _newImageCondition.notify_all();

    if(_featureExtractionThread.joinable()) {
      _featureExtractionThread.join();
    }
  }
}

void FaceRecognizer::SetIsSynchronous(bool shouldRunSynchronous)
{
  if(shouldRunSynchronous && _isRunningAsync)
  {
    LOG_INFO("FaceRecognizer.SetSynchronousMode.SwitchToSynchronous", "");
    StopThread();
  }
  else if(!shouldRunSynchronous && !_isRunningAsync)
  {
    LOG_INFO("FaceRecognizer.SetSynchronousMode.SwitchToAsynchronous", "");
    if(_isInitialized)
    {
      StartThread();
    }
  }
}

Result FaceRecognizer::SanityCheckBookkeeping(const FaceAlbum& album,
                                              const EnrollmentData& enrollmentData,
                                              const AlbumEntryToFaceID& albumEntryToFaceID)
{
  const s32 numEntries = album.GetRegisteredUserNum();
  if(numEntries != (s32)albumEntryToFaceID.size()) {
    LOG_ERROR("FaceRecognizer.SanityCheckBookkeeping.NumAlbumEntriesMismatch",
              "AlbumNumEntries=%d, AlbumEntryToFaceIDSize=%zu",
              numEntries, albumEntryToFaceID.size());
    return RESULT_FAIL;
  }

  for(auto & albumEntry : albumEntryToFaceID)
  {
    if(!album.IsRegistered(albumEntry.first, 0)) {
      LOG_ERROR("FaceRecognizer.SanityCheckBookkeeping.AlbumEntryNotRegistered",
                "AlbumEntry=%d", albumEntry.first);
      return RESULT_FAIL;
    }
  }

  for(auto & enrollData : enrollmentData)
  {
    for(auto & albumEntryPair : enrollData.second.GetAlbumEntries())
    {
      const AlbumEntryID_t albumEntry = albumEntryPair.first;

      auto iter = albumEntryToFaceID.find(albumEntry);
      if(iter == albumEntryToFaceID.end()) {
        LOG_ERROR("FaceRecognizer.SanityCheckBookkeeping.MissingAlbumEntry",
                  "AlbumEntry %d for FaceID %d does not exist in albumEntryToFaceID LUT",
                  albumEntry, enrollData.first);
        return RESULT_FAIL;
      }

      if(iter->second != enrollData.first) {
        LOG_ERROR("FaceRecognizer.SanityCheckBookkeeping.LookupTablesOutOfSync",
                  "AlbumEntryToFaceID[%d] = FaceID %d instead of %d",
                  iter->first, iter->second, enrollData.first);
        return RESULT_FAIL;
      }
    }
  }

  return RESULT_OK;
}

Result FaceRecognizer::UpdateRecognitionData(const FaceID_t recognizedID,
                                             const RecognitionScore score)
{
  FaceID_t faceID = UnknownFaceID;
  auto iter = _trackingToFaceID.find(_trackingID);
  if(iter != _trackingToFaceID.end()) {
    faceID = iter->second;
  }

  if(UnknownFaceID == recognizedID) {

  } else if(UnknownFaceID == faceID) {
    LOG_DEBUG("UpdateRecognitionData.RecognizedNewTrackingID",
              "Tracking ID=%d recognized as FaceID=%d",
              -_trackingID, recognizedID);

    faceID = recognizedID;

  } else if(faceID != recognizedID) {
    auto faceIDenrollData = _enrollmentData.find(faceID);
    if(faceIDenrollData == _enrollmentData.end()) {
      faceID = recognizedID;
    } else {

      auto recIDenrollData  = _enrollmentData.find(recognizedID);
      DEV_ASSERT(recIDenrollData  != _enrollmentData.end(),
                 "FaceRecognizer.UpdateRecognitionData.MissingEnrollmentData");

      FaceID_t mergeTo = Vision::UnknownFaceID, mergeFrom = Vision::UnknownFaceID;

      if(false == recIDenrollData->second.IsForThisSessionOnly() &&
         true  == faceIDenrollData->second.IsForThisSessionOnly())
      {
        mergeFrom = faceID;
        mergeTo   = recognizedID;
      }
      else if(true  == recIDenrollData->second.IsForThisSessionOnly() &&
              false == faceIDenrollData->second.IsForThisSessionOnly())
      {
        mergeFrom = recognizedID;
        mergeTo   = faceID;
      }
      else if(false == recIDenrollData->second.IsForThisSessionOnly() &&
              false == faceIDenrollData->second.IsForThisSessionOnly())
      {
        DEV_ASSERT(!faceIDenrollData->second.GetName().empty() &&
                   !recIDenrollData->second.GetName().empty(),
                   "FaceRecognizer.UpdateRecognitionData.PermanentIDsWithNoNames");

        LOG_WARNING("FaceRecognizer.UpdateRecognitionData.ConfusedTwoNamedIDs",
                    "While tracking face %d with ID=%d (%s), recognized as ID=%d (%s). Not merging!",
                    -_trackingID, faceID,
                    faceIDenrollData->second.GetName().piiGuardedString(),
                    recognizedID,
                    recIDenrollData->second.GetName().piiGuardedString());

        RemoveTrackingID(_trackingID);
        faceID = recognizedID;
      }
      else {
        DEV_ASSERT(faceIDenrollData->second.IsForThisSessionOnly() &&
                   recIDenrollData->second.IsForThisSessionOnly(),
                   "FaceRecognizer.UpdateRecognitionData.BothIDsNotSessionOnly");

        if(faceIDenrollData->second.GetEnrollmentTime() <= recIDenrollData->second.GetEnrollmentTime())
        {
          mergeFrom = recognizedID;
          mergeTo   = faceID;
        } else {
          mergeFrom = faceID;
          mergeTo   = recognizedID;
        }

        if(kFaceRecognitionExtraDebug)
        {
          LOG_INFO("UpdateRecognitionData.MergeBasedOnEnrollmentTime",
                   "Merging ID=%d into ID=%d (%d enrolled at %s, %d enrolled at %s)",
                   mergeFrom,  mergeTo, faceID,
                   EnrolledFaceEntry::GetTimeString(faceIDenrollData->second.GetEnrollmentTime()).c_str(),
                   recognizedID,
                   EnrolledFaceEntry::GetTimeString(recIDenrollData->second.GetEnrollmentTime()).c_str());
        }
      }

      if(mergeFrom != Vision::UnknownFaceID && mergeTo != Vision::UnknownFaceID)
      {
        const bool isMergingAllowed = IsMergingAllowed(mergeTo);
        if(mergeFrom == recognizedID || isMergingAllowed)
        {
          LOG_INFO("UpdateRecognitionData.MergingFaces",
                   "Tracking %d: merging ID=%d into ID=%d (merging allowed=%d)",
                   -_trackingID, mergeFrom, mergeTo, isMergingAllowed);

          Result mergeResult = MergeFaces(mergeTo, mergeFrom);

          if(RESULT_OK != mergeResult) {
            LOG_WARNING("FaceRecognizer.UpdateRecognitionData.MergeFail",
                        "Trying to merge %d with %d", faceID, recognizedID);
          }
        }
        else if(mergeFrom == _enrollmentID)
        {
          LOG_INFO("UpdateRecognitionData.UpdateEnrollmentID",
                   "Updating enrollment ID %d->%d while to tracking %d (not merging)",
                   _enrollmentID, mergeTo, -_trackingID);
          _enrollmentID = mergeTo;
        } else if(kFaceRecognitionExtraDebug) {
          LOG_INFO("UpdateRecognitionData.NotMergingTrackedFace",
                   "Enrollment disabled: not merging tracked face ID=%d into recognized ID=%d",
                   faceID, recognizedID);
        }

        faceID = mergeTo;
      }
    }

  } else {
    DEV_ASSERT(faceID == recognizedID, "FaceRecognizer.UpdateRecognitionData.UnexpectedRecognizedID");
  }

  if(UnknownFaceID != recognizedID) {
    _trackingToFaceID[_trackingID] = faceID;
    _enrollmentData[faceID].SetScore( score );
  }

  return RESULT_OK;
}

bool FaceRecognizer::HasRecognitionData(TrackingID_t forTrackingID) const
{
  const bool haveEntry = (_trackingToFaceID.find(forTrackingID) != _trackingToFaceID.end());
  return haveEntry;
}

bool FaceRecognizer::HasName(TrackingID_t forTrackingID) const
{
  auto iter = _trackingToFaceID.find(forTrackingID);
  if(iter == _trackingToFaceID.end())
  {
    return false;
  }

  const FaceID_t faceID = iter->second;
  if(!ANKI_VERIFY(faceID != UnknownFaceID, "FaceRecognizer.HasName.TrackedFaceWithUnknownID", ""))
  {
    return false;
  }

  auto enrollIter = _enrollmentData.find(faceID);

  if(!ANKI_VERIFY(enrollIter != _enrollmentData.end(), "FaceRecognizer.HasName.TrackedFaceWithNoEnrollData", ""))
  {
    return false;
  }

  return !enrollIter->second.IsForThisSessionOnly();
}

EnrolledFaceEntry FaceRecognizer::GetRecognitionData(TrackingID_t forTrackingID, s32& enrollmentCountReached,
                                                     DebugImageList<CompressedImage>& debugImages)
{
  if(_shouldClearAllTrackingData)
  {
    ClearAllTrackingDataInternal();
    _shouldClearAllTrackingData = false;
  }

  if(ProcessingState::FeaturesReady == _state)
  {
    if(_trackingID == UnknownFaceID)
    {
      DEV_ASSERT(_isRunningAsync, "FaceRecognizer.GetRecognitionData.InvalidTrackIDinSyncMode");
      LOG_INFO("GetRecognitionData.DroppingFeaturesComputedWhileClearing", "");
    }
    else if(_isEnrollmentCancelled)
    {
      LOG_INFO("GetRecognitionData.DroppingFeaturesComputedWhileCancelled", "");
      _isEnrollmentCancelled = false;
    }
    else if(_isVerification)
    {
      VerifyTrackedFace();
    }
    else
    {
      FaceID_t recognizedID = UnknownFaceID;
      RecognitionScore score = 0;
      Result result = RecognizeFace(recognizedID, score, debugImages);

      if(RESULT_OK == result)
      {
        result = UpdateRecognitionData(recognizedID, score);
        if(RESULT_OK != result)
        {
          LOG_ERROR("FaceRecognizer.GetRecognitionData.UpdateRecognitionDataFailed", "");
        }
      }
      else
      {
        LOG_ERROR("FaceRecognizer.GetRecognitionData.RecognizeFaceFailed", "");
      }

      if(ANKI_DEVELOPER_CODE)
      {
        const Result sanityResult = SanityCheckBookkeeping(_album,
                                                           _enrollmentData,
                                                           _albumEntryToFaceID);
        DEV_ASSERT(sanityResult == RESULT_OK, "FaceRecognizer.GetRecognitionData.SanityCheckFailed");
      }
    }

    _mutex.lock();
    _state = ProcessingState::Idle;
    _mutex.unlock();
  }

  EnrolledFaceEntry entryToReturn;
  enrollmentCountReached = 0;

  auto iter = _trackingToFaceID.find(forTrackingID);
  if(iter != _trackingToFaceID.end()) {
    const FaceID_t faceID = iter->second;
    DEV_ASSERT(faceID != UnknownFaceID, "FaceRecognizer.GetRecognitionData.TrackedFaceWithUnknownID");
    auto enrollIter = _enrollmentData.find(faceID);

    DEV_ASSERT(enrollIter != _enrollmentData.end(), "FaceRecognizer.GetRecognitionData.TrackedFaceWithNoEnrollData");

    auto & enrolledEntry = enrollIter->second;
    if(_enrollmentID != UnknownFaceID &&
       enrolledEntry.WasFaceIDJustUpdated() &&
       _enrollmentID == enrolledEntry.GetPreviousFaceID())
    {
      if(kFaceRecognitionExtraDebug) {
        LOG_INFO("GetRecognitionData.UpdatingEnrollmentID",
                 "Old:%d -> New:%d", _enrollmentID, enrolledEntry.GetFaceID());
      }
      _enrollmentID = enrolledEntry.GetFaceID();
    }

    entryToReturn = enrolledEntry;
    if(_enrollmentCount == 0 && _origEnrollmentCount > 0 && _enrollmentID == entryToReturn.GetFaceID()) {
      if(kFaceRecognitionExtraDebug) {
        LOG_INFO("GetRecognitionData.EnrollmentCountReached",
                 "Count=%d", _origEnrollmentCount);
      }

      const size_t numAlbumEntries = entryToReturn.GetAlbumEntries().size();
      DASMSG(vision_face_enrollment_count_reached,
             "vision.face_enrollment_count_reached",
             "This will be shown when the most number of faces we support is shown");
      DASMSG_SET(i1, numAlbumEntries, "The current num of entries");
      DASMSG_SET(i2, _enrollmentID, "The enrollment ID");
      DASMSG_SEND();

      enrollmentCountReached = _origEnrollmentCount;
      _origEnrollmentCount = 0;
    }

    enrolledEntry.UpdatePreviousIDs();
  }

  return entryToReturn;
}

void FaceRecognizer::RemoveTrackingID(TrackingID_t trackerID)
{
  auto iter = _trackingToFaceID.find(trackerID);
  if(iter != _trackingToFaceID.end())
  {
    const FaceID_t faceID = iter->second;
    auto enrollIter = _enrollmentData.find(faceID);
    if(enrollIter != _enrollmentData.end())
    {
      enrollIter->second.ClearTrackingID();
    }
    _trackingToFaceID.erase(iter);
  }

  _trackingIDtoVerifyFailures.erase(trackerID);
  _trackingIDtoBestGuessName.erase(trackerID);
}

void FaceRecognizer::ClearAllTrackingData()
{
  _shouldClearAllTrackingData = true;
}

void FaceRecognizer::ClearAllTrackingDataInternal()
{
  for(auto & enrollData : _enrollmentData)
  {
    enrollData.second.ClearTrackingID();
  }
  _trackingToFaceID.clear();
  _trackingIDtoVerifyFailures.clear();
  _trackingIDtoBestGuessName.clear();

  if(_isRunningAsync)
  {
    _trackingID = UnknownFaceID;
  }
}

void FaceRecognizer::Run()
{
  Anki::Util::SetThreadName(pthread_self(), "FaceRecognizer");

  while(_isRunningAsync)
  {
    _mutex.lock();
    const bool anythingToDo = ProcessingState::HasNewImage == _state;
    _mutex.unlock();

    if(anythingToDo) {
      ExtractFeatures();
    }

    {
      std::unique_lock<std::mutex> lock{_mutex};
      _newImageCondition.wait(lock, [this]{ return (_state == ProcessingState::HasNewImage) || !_isRunningAsync; });
    }
  }

  LOG_WARNING("FaceRecognizer.Run.ThreadHasStopped",
              "FaceRecognizer thread has stopped. No longer running asynchronously");
}

void FaceRecognizer::ExtractFeatures()
{
  DEV_ASSERT(ProcessingState::HasNewImage == _state, "FaceRecognizer.ExtractFeatures.ShouldBeInHasNewImageState");

  _mutex.lock();
  _state = ProcessingState::ExtractingFeatures;
  _mutex.unlock();

  Tic("ComputeEmbedding");
  const Result result = _embedder.ComputeEmbedding(_img, _landmarks, _feature);
  Toc("ComputeEmbedding");

  ProcessingState newState = ProcessingState::FeaturesReady;

  if(RESULT_OK != result) {
    LOG_WARNING("FaceRecognizer.ExtractFeatures.EmbeddingFailed", "Going back to Idle state");
    newState = ProcessingState::Idle;
  }

  _mutex.lock();
  _state = newState;
  _mutex.unlock();
}

void FaceRecognizer::VerifyTrackedFace()
{
  if(_feature.empty()) {
    return;
  }

  auto trackIter = _trackingToFaceID.find(_trackingID);
  if(trackIter == _trackingToFaceID.end()) {
    return;
  }

  const FaceID_t claimedID = trackIter->second;

  auto enrollIter = _enrollmentData.find(claimedID);
  if(enrollIter == _enrollmentData.end()) {
    LOG_WARNING("FaceRecognizer.VerifyTrackedFace.MissingEnrollData",
                "TrackID:%d claims FaceID:%d", -_trackingID, claimedID);
    RemoveTrackingID(_trackingID);
    return;
  }

  RecognitionScore bestScore = 0;
  for(auto const& albumEntryPair : enrollIter->second.GetAlbumEntries())
  {
    bestScore = std::max(bestScore, _album.Verify(_feature, albumEntryPair.first));
  }

  if(bestScore < kFaceVerificationThreshold)
  {
    const s32 numFailures = ++_trackingIDtoVerifyFailures[_trackingID];
    if(numFailures < kFaceVerifyFailuresBeforeDrop)
    {
      LOG_INFO("FaceRecognizer.VerifyTrackedFace.VerifyFailed",
               "TrackID:%d vs FaceID:%d score:%d < %d, failure %d of %d",
               -_trackingID, claimedID, bestScore, kFaceVerificationThreshold,
               numFailures, kFaceVerifyFailuresBeforeDrop);
      return;
    }

    LOG_INFO("FaceRecognizer.VerifyTrackedFace.IdentityLost",
             "TrackID:%d no longer matches FaceID:%d (score:%d < %d), dropping association",
             -_trackingID, claimedID, bestScore, kFaceVerificationThreshold);
    RemoveTrackingID(_trackingID);
  }
  else
  {
    _trackingIDtoVerifyFailures.erase(_trackingID);
    enrollIter->second.SetScore(bestScore);
  }
}

FaceRecognizer::AlbumEntryID_t FaceRecognizer::GetNextAlbumEntryToUse()
{
  const AlbumEntryID_t failureEntry = EnrolledFaceEntry::UnknownAlbumEntryID;

  const s32 numEntriesInAlbum = _album.GetRegisteredUserNum();

  if(numEntriesInAlbum >= kMaxTotalAlbumEntries)
  {
    FaceID_t oldestSessionOnlyID = UnknownFaceID;
    EnrolledFaceEntry::Time oldestUpdateTime(std::chrono::seconds::max());
    for(auto & enrollData : _enrollmentData) {
      if(enrollData.second.IsForThisSessionOnly() && enrollData.second.GetLastUpdateTime() < oldestUpdateTime) {
        oldestSessionOnlyID = enrollData.second.GetFaceID();
        oldestUpdateTime    = enrollData.second.GetLastUpdateTime();
      }
    }

    if(UnknownFaceID == oldestSessionOnlyID)
    {
      LOG_ERROR("FaceRecognizer.GetNextAlbumEntryToUse.TooManyUsers",
                "Already have %zu users, could not add another", _enrollmentData.size());
      return failureEntry;
    }

    LOG_INFO("GetNextAlbumEntryToUse.ReplacingOldestSessionOnlyUser",
             "Session-only face %d not updated since %s and will be replaced.",
             oldestSessionOnlyID, EnrolledFaceEntry::GetTimeString(oldestUpdateTime).c_str());

    auto & albumEntries = _enrollmentData.at(oldestSessionOnlyID).GetAlbumEntries();

    DEV_ASSERT(!albumEntries.empty(), "FaceRecognizer.GetNextAlbumEntryToUse.OldestFaceHasNoEntries");

    _nextAlbumEntry = albumEntries.begin()->first;

    Result removeResult = RemoveUser(oldestSessionOnlyID);

    if(RESULT_OK != removeResult)
    {
      LOG_WARNING("FaceRecognizer.GetNextAlbumEntryToUse.RemoveOldestUserFailed",
                  "Attempting to remove %d", oldestSessionOnlyID);
    }

    if(_album.IsRegistered(_nextAlbumEntry, 0))
    {
      LOG_WARNING("FaceRecognizer.GetNextAlbumEntryToUse.FirstFreedEntryStillRegistered",
                  "Entry:%d", _nextAlbumEntry);
      return failureEntry;
    }
  }
  else
  {
    s32 tries = 0;
    while(_album.IsRegistered(_nextAlbumEntry, 0) && tries < kMaxTotalAlbumEntries)
    {
      ++_nextAlbumEntry;
      if(_nextAlbumEntry >= kMaxTotalAlbumEntries) {
        _nextAlbumEntry = 0;
      }
      ++tries;
    }

    if(tries >= kMaxTotalAlbumEntries) {
      LOG_WARNING("FaceRecognizer.GetNextAlbumEntryToUse.NoIDsAvailable",
                  "Could not find free space in the album to use for a new entry");
      return failureEntry;
    }
  }

  return _nextAlbumEntry;
}

FaceID_t FaceRecognizer::GetNextFaceID()
{
  while(_enrollmentData.find(_nextFaceID) != _enrollmentData.end())
  {
    ++_nextFaceID;
    if(_nextFaceID == UnknownFaceID)
    {
      ++_nextFaceID;
    }
  }

  return _nextFaceID;
}

Result FaceRecognizer::RegisterNewUser(const Feature& feature, FaceID_t& faceID)
{
  DEV_ASSERT(ProcessingState::FeaturesReady == _state, "FaceRecognizer.RegisterNewUser.FeaturesShouldBeReady");

  AlbumEntryID_t albumEntry = GetNextAlbumEntryToUse();
  faceID = GetNextFaceID();

  if(albumEntry < 0)
  {
    LOG_WARNING("FaceRecognizer.RegisterNewUser.NoAlbumEntriesAvailable", "");
    return RESULT_FAIL;
  }

  if(RESULT_OK != _album.RegisterData(feature, albumEntry, 0)) {
    LOG_WARNING("FaceRecognizer.RegisterNewUser.RegisterDataFailed",
                "Failed trying to register album entry %d", albumEntry);
    return RESULT_FAIL;
  }

  const EnrolledFaceEntry::Time enrollmentTime = (kGetEnrollmentTimeFromImageTimestamp ?
                                                  EnrolledFaceEntry::Time(std::chrono::milliseconds(_img.GetTimestamp())) :
                                                  std::chrono::system_clock::now());
  EnrolledFaceEntry enrollData(faceID, enrollmentTime);
  enrollData.SetTrackingID(_trackingID);
  enrollData.AddOrUpdateAlbumEntry(albumEntry, enrollmentTime, true);

  _albumEntryToFaceID[albumEntry] = faceID;

  if(_enrollmentCount > 0) {
    --_enrollmentCount;
  }

  if(_forceNewEnrollment)
  {
    enrollData.SetPreviousFaceID(_enrollmentID);
  }

  _enrollmentData.emplace(faceID, std::move(enrollData));

  if(kGatherDebugEnrollmentImages) {
    SetEnrollmentImage(albumEntry, 0);
  }

  LOG_INFO("RegisterNewUser.Success", "Added user with ID %d to album", faceID);

  return RESULT_OK;
}

void FaceRecognizer::CancelExistingEnrollment()
{
  if(_enrollmentID != UnknownFaceID && _enrollmentCount > 0)
  {
    LOG_INFO("FaceRecognizer.CancelExistingEnrollment",
             "Cancelling enrollment of FaceID %d", _enrollmentID);
    _isEnrollmentCancelled = true;

    auto enrollDataIter = _enrollmentData.find(_enrollmentID);
    if(enrollDataIter == _enrollmentData.end())
    {
      LOG_WARNING("FaceRecognizer.CancelExistingEnrollment.NoEnrollmentDataToErase",
                  "enrollmentID=%d", _enrollmentID);
      return;
    }

    const auto& albumEntries = enrollDataIter->second.GetAlbumEntries();
    if(albumEntries.empty())
    {
      LOG_ERROR("FaceRecognizer.CancelExistingEnrollment.EmptyAlbumEntries",
                "Erasing %s enrollmentID=%d",
                (enrollDataIter->second.IsForThisSessionOnly() ? "session-only" : "named"),
                _enrollmentID);
      EraseFace(_enrollmentID);
      return;
    }

    if(albumEntries.size() == 1)
    {
      return;
    }

    auto albumEntryIter = albumEntries.begin();
    AlbumEntryID_t mostRecentAlbumEntryID = albumEntryIter->first;
    EnrolledFaceEntry::Time mostRecentTime = albumEntryIter->second;
    ++albumEntryIter;
    while(albumEntryIter != albumEntries.end())
    {
      if(albumEntryIter->second > mostRecentTime)
      {
        mostRecentTime = albumEntryIter->second;
        mostRecentAlbumEntryID = albumEntryIter->first;
      }
      ++albumEntryIter;
    }

    enrollDataIter->second.RemoveAlbumEntry(mostRecentAlbumEntryID);
    _albumEntryToFaceID.erase(mostRecentAlbumEntryID);
    _album.ClearUser(mostRecentAlbumEntryID);

    LOG_INFO("FaceRecognizer.CancelExistingEnrollment.RemoveAlbumEntry",
             "Removed AlbumEntry %d from Face %d, %zu entries remain",
             mostRecentAlbumEntryID, _enrollmentID, enrollDataIter->second.GetNumAlbumEntries());
  }
}

void FaceRecognizer::SetAllowedEnrollments(s32 N, FaceID_t forFaceID, bool forceNewID)
{
  if(forFaceID == UnknownFaceID)
  {
    CancelExistingEnrollment();
  }

  _enrollmentCount = N;
  _origEnrollmentCount = N;
  _enrollmentID = forFaceID;

  if(_enrollmentID == UnknownFaceID)
  {
    DEV_ASSERT(!forceNewID, "FaceRecognizer.SetAllowedEnrollments.CannotForceNewIDwithUnknownID");
    _enrollmentTrackID = UnknownFaceID;
  }
  else
  {
    auto enrollDataIter = _enrollmentData.find(_enrollmentID);
    if(enrollDataIter == _enrollmentData.end()) {
      LOG_WARNING("FaceRecognizer.SetAllowedEnrollments.NoEnrollmentData",
                  "No data for enrollmentID=%d", _enrollmentID);
      _enrollmentTrackID = UnknownFaceID;
    } else {
      _enrollmentTrackID = enrollDataIter->second.GetTrackingID();
    }
  }

  _forceNewEnrollment = false;
  if(forceNewID)
  {
    DEV_ASSERT(N > 0, "FaceRecognizer.SetAllowedEnrollments.BadNforForceNewID");

    if(ANKI_VERIFY(_enrollmentTrackID != UnknownFaceID,
                   "FaceRecognizer.SetAllowedEnrollments.ForceWithNoTrackingID",
                   "No tracking ID found for FaceID:%d, cannot enable forced new ID",
                   _enrollmentID))
    {
      _forceNewEnrollment = true;
    }
  }
}

bool FaceRecognizer::SetNextFaceToRecognize(const Vision::Image& img,
                                            TrackingID_t trackingID,
                                            const Rectangle<f32>& faceRect,
                                            const FaceLandmarks& landmarks,
                                            const bool enableEnrollment,
                                            const bool isVerification)
{
  const bool anythingToDo = (enableEnrollment || !_enrollmentData.empty());

  if(ProcessingState::Idle == _state && anythingToDo)
  {
    _mutex.lock();

    img.CopyTo(_img);
    _img.SetTimestamp(img.GetTimestamp());

    _trackingID = trackingID;
    _faceRect   = faceRect;
    _landmarks  = landmarks;

    _isEnrollmentEnabled = enableEnrollment;
    _isVerification      = isVerification;

    _state = ProcessingState::HasNewImage;
    _mutex.unlock();
    _newImageCondition.notify_all();

    if(!_isRunningAsync) {
      ExtractFeatures();
    }

    return true;
  }

  return false;
}

Result FaceRecognizer::UpdateExistingAlbumEntry(AlbumEntryID_t albumEntry, const Feature& feature,
                                                RecognitionScore score)
{
  DEV_ASSERT(ProcessingState::FeaturesReady == _state, "FaceRecognizer.UpdateExistingAlbumEntry.FeaturesShouldBeReady");

  Result result = RESULT_OK;

  const EnrolledFaceEntry::Time updateTime = (kGetEnrollmentTimeFromImageTimestamp ?
                                              EnrolledFaceEntry::Time(std::chrono::milliseconds(_img.GetTimestamp())) :
                                              std::chrono::system_clock::now());

  auto albumToFaceIter = _albumEntryToFaceID.find(albumEntry);
  DEV_ASSERT_MSG(albumToFaceIter != _albumEntryToFaceID.end(),
                 "FaceRecognizer.UpdateExistingAlbumEntry.MissingAlbumToFaceEntry",
                 "AlbumEntry:%d", albumEntry);
  const FaceID_t faceID = albumToFaceIter->second;

  auto enrollDataIter = _enrollmentData.find(faceID);
  DEV_ASSERT_MSG(enrollDataIter != _enrollmentData.end(),
                 "FaceRecognizer.UpdateExistingAlbumEntry.MissingEnrollmentStatus",
                 "FaceID:%d", faceID);

  auto & enrollData = enrollDataIter->second;

  enrollData.SetAlbumEntryLastSeenTime(albumEntry, updateTime);

  if(_trackingID != enrollData.GetTrackingID()) {
    LOG_INFO("UpdateExistingAlbumEntry.UpdateTrackID",
             "Update trackID for face %d: %d -> %d",
             faceID, -enrollData.GetTrackingID(), -_trackingID);
    _trackingToFaceID.erase(enrollData.GetTrackingID());
    enrollData.SetTrackingID(_trackingID);
  }

  const s32 numDataStored = _album.GetRegisteredUserDataNum(albumEntry);

  auto const timeSinceLastUpdate = std::chrono::duration_cast<std::chrono::seconds>(updateTime - enrollData.GetLastUpdateTime());

  auto const timeBetweenEnrollments = std::chrono::seconds(Util::numeric_cast<s64>(std::round(kTimeBetweenFaceEnrollmentUpdates_sec)));

  const bool isEnrollmentEnabled = _enrollmentCount != 0 && (_enrollmentID == UnknownFaceID ||
                                                             _enrollmentID == faceID);

  const bool isNamedFace = !enrollData.GetName().empty();
  const bool hasEnrollSpaceLeft = numDataStored < kMaxEnrollDataPerAlbumEntry;
  const bool haveEnrollmentCountAndID = _enrollmentCount > 0 && _enrollmentID != UnknownFaceID;
  const bool enrollEvenIfFull = !isNamedFace || kEnableEnrollmentAfterFull || haveEnrollmentCountAndID;
  const bool isTimeToEnroll = timeSinceLastUpdate.count() >= timeBetweenEnrollments.count();

  if(isEnrollmentEnabled && isTimeToEnroll && (hasEnrollSpaceLeft || enrollEvenIfFull))
  {
    DEV_ASSERT(numDataStored > 0, "FaceRecognizer.UpdateExistingAlbumEntry.BadNumData");

    AlbumEntryID_t entryToReplace = EnrolledFaceEntry::UnknownAlbumEntryID;
    if(hasEnrollSpaceLeft)
    {
      entryToReplace = numDataStored;
      if(RESULT_OK != _album.RegisterData(feature, albumEntry, entryToReplace))
      {
        LOG_WARNING("FaceRecognizer.UpdateExistingUser.FailedToRegisterNewEntry",
                    "AlbumEntry:%d Data:%d for FaceID:%d",
                    albumEntry, entryToReplace, faceID);
        return RESULT_FAIL;
      }

      if(kFaceRecognitionExtraDebug) {
        LOG_INFO("UpdateExistingUser",
                 "Adding Data %d to AlbumEntry %d of %zu for FaceID %d",
                 entryToReplace, albumEntry, enrollData.GetAlbumEntries().size(), faceID);
      }
    }
    else
    {
      const RecognitionScore newEntryScore = _album.Verify(feature, albumEntry);

      RecognitionScore lowestScoreBelowNewEntry = newEntryScore;
      for(s32 iData = 0; iData < kMaxEnrollDataPerAlbumEntry; ++iData)
      {
        Feature existingFeature;
        if(!_album.GetFeature(albumEntry, iData, existingFeature)) {
          continue;
        }

        _album.ClearData(albumEntry, iData);

        const RecognitionScore currentEntryScore = _album.Verify(existingFeature, albumEntry);

        if(currentEntryScore < lowestScoreBelowNewEntry) {
          entryToReplace = iData;
          lowestScoreBelowNewEntry = currentEntryScore;
        }

        if(RESULT_OK != _album.RegisterData(existingFeature, albumEntry, iData)) {
          LOG_ERROR("FaceRecognizer.UpdateExistingAlbumEntry.ReRegisterDataFailed",
                    "albumEntry:%d dataEntry:%d", albumEntry, iData);
          return RESULT_FAIL;
        }

        if(kFaceRecognitionExtraDebug) {
          LOG_INFO("UpdateExistingUser.EvaluatingAlbumEntryClusteredness",
                   "Data:%d in AlbumEntry:%d got score:%d (vs. possible addition with score:%d)",
                   iData, albumEntry, currentEntryScore, newEntryScore);
        }
      }

      if(EnrolledFaceEntry::UnknownAlbumEntryID != entryToReplace)
      {
        if(RESULT_OK != _album.RegisterData(feature, albumEntry, entryToReplace)) {
          LOG_ERROR("FaceRecognizer.UpdateExistingAlbumEntry.RegisterReplacementDataFailed",
                    "albumEntry:%d dataEntry:%d", albumEntry, entryToReplace);
          return RESULT_FAIL;
        }

        if(kFaceRecognitionExtraDebug) {
          LOG_INFO("UpdateExistingUser.ReplacedDataEntry",
                   "Replaced Data:%d in AlbumEntry:%d with new feature (score:%d)",
                   entryToReplace, albumEntry, lowestScoreBelowNewEntry);
        }
      }
    }

    const bool isSessionOnly = (_enrollmentID != enrollData.GetFaceID());
    enrollData.AddOrUpdateAlbumEntry(albumEntry, updateTime, isSessionOnly);

    if(kGatherDebugEnrollmentImages && entryToReplace != EnrolledFaceEntry::UnknownAlbumEntryID) {
      SetEnrollmentImage(albumEntry, entryToReplace);
    }

    if(_enrollmentCount > 0) {
      --_enrollmentCount;
    }
  }
  else
  {
    LOG_DEBUG("FaceRecognizer.UpdateExistingAlbumEntry.NotAddingEnrollmentEntry",
              "FaceID:%d isEnabled:%d isNamed:%d hasSpaceLeft:%d haveCountAndID:%d enrollIfFull:%d isTime:%d",
              faceID, isEnrollmentEnabled, isNamedFace, hasEnrollSpaceLeft,
              haveEnrollmentCountAndID, enrollEvenIfFull, isTimeToEnroll);
  }

  return result;
}

void FaceRecognizer::SetEnrollmentImage(AlbumEntryID_t albumEntry, s32 dataEntry)
{
  if(dataEntry < 0 || dataEntry >= kMaxEnrollDataPerAlbumEntry) {
    return;
  }

  Rectangle<s32> detectionRect((s32)std::round(_faceRect.GetX()),
                               (s32)std::round(_faceRect.GetY()),
                               (s32)std::round(_faceRect.GetWidth()),
                               (s32)std::round(_faceRect.GetHeight()));

  if(detectionRect.GetWidth() <= 0 || detectionRect.GetHeight() <= 0) {
    return;
  }

  _img.GetROI(detectionRect).CopyTo(_enrollmentImages[albumEntry][dataEntry]);
  _enrollmentImages[albumEntry][dataEntry].SetTimestamp(_img.GetTimestamp());
}

FaceRecognizer::EnrollmentData::iterator FaceRecognizer::RemoveUser(EnrollmentData::iterator userIter)
{
  _trackingToFaceID.erase(userIter->second.GetTrackingID());

  const FaceID_t faceID = userIter->first;

  for(auto & albumEntryPair : userIter->second.GetAlbumEntries())
  {
    const AlbumEntryID_t albumEntry = albumEntryPair.first;

    _albumEntryToFaceID.erase(albumEntry);

    _album.ClearUser(albumEntry);

    if(kGatherDebugEnrollmentImages)
    {
      _enrollmentImages.erase(albumEntry);
    }
  }

  if(ANKI_DEVELOPER_CODE)
  {
    for(auto iter = _trackingToFaceID.begin(); iter != _trackingToFaceID.end(); )
    {
      if(iter->second == faceID) {
        LOG_WARNING("FaceRecognizer.RemoveUserHelper.StaleTrackingToFaceID",
                    "TrackID %d still maps to FaceID %d",
                    iter->first, iter->second);
        iter = _trackingToFaceID.erase(iter);
      } else {
        ++iter;
      }
    }
    for(auto iter = _albumEntryToFaceID.begin(); iter != _albumEntryToFaceID.end(); )
    {
      if(iter->second == faceID) {
        LOG_WARNING("FaceRecognizer.RemoveUserHelper.StaleAlbumEntryToFaceID",
                    "AlbumEntry %d still maps to FaceID %d",
                    iter->first, iter->second);
        iter = _albumEntryToFaceID.erase(iter);
      } else {
        ++iter;
      }
    }
  }

  return _enrollmentData.erase(userIter);
}

Result FaceRecognizer::RemoveUser(FaceID_t faceID)
{
  auto userIter = _enrollmentData.find(faceID);
  if(userIter != _enrollmentData.end())
  {
    RemoveUser(userIter);
  } else {
    LOG_INFO("RemoveUser.UserDoesNotExist", "FaceID=%d", faceID);
  }

  return RESULT_OK;
}

FaceID_t FaceRecognizer::GetFaceIDforAlbumEntry(AlbumEntryID_t albumEntry) const
{
  auto iter = _albumEntryToFaceID.find(albumEntry);
  if(iter == _albumEntryToFaceID.end())
  {
    LOG_ERROR("FaceRecognizer.GetFaceIDforAlbumEntry.MissingEntry",
              "AlbumEntry:%d", albumEntry);
    return UnknownFaceID;
  }

  return iter->second;
}

void FaceRecognizer::AddDebugInfo(FaceID_t matchedID, RecognitionScore score,
                                  std::list<FaceRecognitionMatch>& newDebugInfo) const
{
  auto iter = _enrollmentData.find(matchedID);
  newDebugInfo.emplace_back(FaceRecognitionMatch{
    .name = (iter == _enrollmentData.end() ? std::string("") : iter->second.GetName().asString()),
    .matchedID = matchedID,
    .score  = score,
  });
}

Result FaceRecognizer::RecognizeFace(FaceID_t& faceID, RecognitionScore& recognitionScore,
                                     DebugImageList<CompressedImage>& debugImages)
{
  faceID = UnknownFaceID;
  recognitionScore = 0;

  DEV_ASSERT(ProcessingState::FeaturesReady == _state,
             "FaceRecognizer.RecognizerFace.FeaturesShouldBeReady");

  if(_feature.empty()) {
    LOG_WARNING("FaceRecognizer.RecognizeFace.NoFeature", "");
    return RESULT_OK;
  }

  const s32 numUsersInAlbum = _album.GetRegisteredUserNum();

  const bool haveEnrollmentCountsLeft = _enrollmentCount != 0;
  const bool canEnrollAnyFace         = (_enrollmentID == UnknownFaceID);

  const s32 kMaxScore = FaceAlbum::kMaxScore;

  if(numUsersInAlbum == 0 && haveEnrollmentCountsLeft && canEnrollAnyFace)
  {
    LOG_INFO("RecognizeFace.AddingFirstUser",
             "Adding first user to empty album");

    Result lastResult = RegisterNewUser(_feature, faceID);
    if(RESULT_OK != lastResult) {
      LOG_WARNING("FaceRecognizer.RecognizeFace.FailedToRegisterFirstUser",
                  "FaceID:%d", faceID);
      return lastResult;
    }

    recognitionScore = kMaxScore;
    return RESULT_OK;
  }

  if(!canEnrollAnyFace && _forceNewEnrollment &&
     haveEnrollmentCountsLeft &&
     _trackingID == _enrollmentTrackID)
  {
    recognitionScore = kMaxScore;

    if(_enrollmentCount == _origEnrollmentCount)
    {
      LOG_INFO("RecognizeFace.ForceAddingNewFaceID",
               "Adding new user to empty album, while enrolling track ID:%d",
               _enrollmentTrackID);

      Result result = RegisterNewUser(_feature, faceID);
      if(RESULT_OK != result) {
        LOG_WARNING("FaceRecognizer.RecognizeFace.FailedToRegisterNewUser",
                    "TrackingID:%d FaceID:%d", _trackingID, faceID);
        return result;
      }

      _enrollmentID = faceID;
      _trackingToFaceID[_trackingID] = faceID;
    }
    else
    {
      LOG_INFO("RecognizeFace.ForceUpdatingNewFaceID",
               "Updating forced enrollment ID:%d", _enrollmentID);

      auto enrollDataIter = _enrollmentData.find(_enrollmentID);
      if(ANKI_VERIFY(enrollDataIter != _enrollmentData.end(),
                     "FaceRecognizer.RecognizeFace.MissingEnrollDataForForcedUpdate",
                     "EnrollmentID:%d", _enrollmentID))
      {
        const auto& albumEntries = enrollDataIter->second.GetAlbumEntries();
        DEV_ASSERT(albumEntries.size() == 1, "FaceRecognizer.RecognizeFace.ExpectingOneAlbumEntry");

        const AlbumEntryID_t albumEntry = albumEntries.begin()->first;
        Result result = UpdateExistingAlbumEntry(albumEntry, _feature, recognitionScore);
        if(RESULT_OK != result)
        {
          LOG_WARNING("FaceRecognizer.RecognizeFace.ForceUpdatingExistingAlbumEntryFailed",
                      "EnrollmentID:%d AlbumEntry:%d", _enrollmentID, albumEntry);
        }
      }
    }

    return RESULT_OK;
  }

  s32 resultNum = 0;
  const s32 kMaxIdentifyResults = 10;
  std::vector<AlbumEntryID_t> matchingAlbumEntries(kMaxIdentifyResults);
  std::vector<RecognitionScore> scores(kMaxIdentifyResults);
  Tic("Identify");
  _album.Identify(_feature, kMaxIdentifyResults, matchingAlbumEntries, scores, resultNum);
  Toc("Identify");

  UpdateBestGuessName(matchingAlbumEntries, scores, resultNum);

  const bool foundMatchAboveThreshold = (resultNum > 0) && (scores[0] > kFaceRecognitionThreshold);
  if(foundMatchAboveThreshold)
  {
    s32 matchIndex = 0;
    FaceID_t matchingID = GetFaceIDforAlbumEntry(matchingAlbumEntries[0]);
    if(matchingID == UnknownFaceID) {
      LOG_WARNING("FaceRecognizer.RecognizeFace.FailedToGetTopFaceID",
                  "MatchingAlbumEntry=%d", matchingAlbumEntries[0]);
      return RESULT_FAIL;
    }

    RecognitionScore matchingScore = scores[0];

    auto matchIter = _enrollmentData.find(matchingID);

    if(matchIter == _enrollmentData.end()) {
      LOG_WARNING("FaceRecognizer.RecognizeFace.MissingTopMatchEnrollmentData",
                  "ID:%d", matchingID);
      return RESULT_FAIL;
    }

    if(kFaceRecognitionExtraDebug)
    {
      std::list<FaceRecognitionMatch> newDebugInfo{
        FaceRecognitionMatch{
          .name = matchIter->second.GetName(),
          .matchedID = matchingID,
          .score = matchingScore,
        }
      };
      s32 iResult = matchIndex+1;
      BOUNDED_WHILE(kMaxIdentifyResults, iResult < resultNum && newDebugInfo.size() < kFaceRecMaxDebugResults)
      {
        auto const matchedID = GetFaceIDforAlbumEntry(matchingAlbumEntries[iResult]);
        if(matchedID != newDebugInfo.back().matchedID)
        {
          AddDebugInfo(matchedID, scores[iResult], newDebugInfo);
        }
        ++iResult;
      }

      matchIter->second.SetDebugMatchingInfo(std::move(newDebugInfo));
    }

    bool shouldUpdateAlbumEntry = true;

    const bool haveMoreResults = resultNum >= 2;
    const bool isTopMatchSessionOnly = matchIter->second.IsForThisSessionOnly();
    const bool isTopMatchBeingEnrolled = matchingID == _enrollmentID;

    if(haveMoreResults && isTopMatchSessionOnly && !isTopMatchBeingEnrolled)
    {
      FaceID_t nextMatchingID = UnknownFaceID;
      s32 nextIndex = 1;
      do {
        nextMatchingID = GetFaceIDforAlbumEntry(matchingAlbumEntries[nextIndex]);

        auto nextMatchIter = _enrollmentData.find(nextMatchingID);

        if(nextMatchIter == _enrollmentData.end())
        {
          LOG_WARNING("FaceRecognizer.RecognizeFace.Missing2ndMatchEnrollmentData",
                      "ID:%d", nextMatchingID);
          nextMatchingID = UnknownFaceID;
          break;
        }

        if(nextMatchIter->second.IsForThisSessionOnly())
        {
          nextMatchingID = UnknownFaceID;
        }
        else
        {
          const bool nextScoreHighEnough = (scores[nextIndex] > kFaceRecognitionThreshold - kFaceRecognitionThresholdMarginForUsing2ndBest);

          if(nextScoreHighEnough)
          {
            const s32 lowThreshold = std::min(scores[nextIndex] - kFaceRecognitionThresholdMarginForUsing2ndBest,
                                              kFaceRecognitionThreshold - 2*kFaceRecognitionThresholdMarginForUsing2ndBest);

            FaceID_t nextNextID = Vision::UnknownFaceID;
            bool nextNextScoreLowEnough = true;
            s32 nextNextIndex = nextIndex + 1;
            BOUNDED_WHILE(kMaxIdentifyResults, nextNextIndex < resultNum && scores[nextNextIndex] > lowThreshold)
            {
              nextNextID = GetFaceIDforAlbumEntry(matchingAlbumEntries[nextNextIndex]);
              if(nextNextID != nextMatchingID)
              {
                auto nextNextIter = _enrollmentData.find(nextNextID);
                if(nextNextIter == _enrollmentData.end())
                {
                  LOG_WARNING("FaceRecognizer.RecognizeFace.Missing3rdMatchEnrollmentData",
                              "ID:%d", nextNextID);
                }
                else if(!nextNextIter->second.IsForThisSessionOnly())
                {
                  nextNextScoreLowEnough = false;
                  break;
                }
              }

              ++nextNextIndex;
            }

            if(nextNextScoreLowEnough)
            {
              LOG_INFO("RecognizeFace.UsingLowerRankedMatch",
                       "Top match (AlbumEntry:%d ID:%d Score:%d) is session-only. "
                       "Match at index %d (AlbumEntry:%d ID:%d Score:%d) is named ('%s'). Using it and merging.",
                       matchingAlbumEntries[matchIndex], matchingID, matchingScore,
                       nextIndex, matchingAlbumEntries[nextIndex], nextMatchingID, scores[nextIndex],
                       nextMatchIter->second.GetName().piiGuardedString());

              DASMSG(vision.face_recognition.using_lower_ranked_match,
                     "vision.face_recognition.using_lower_ranked_match",
                     "A session-only face was the top match, but we are using a lower-ranked named match instead");
              DASMSG_SET(i1, matchingID, "Top matching ID");
              DASMSG_SET(i2, matchingScore, "Top matching score");
              DASMSG_SET(i3, nextMatchingID, "Next matching ID");
              DASMSG_SET(i4, scores[nextIndex], "Score of nextIndex (=scores[nextIndex])");
              DASMSG_SET(s1, std::to_string(nextNextID), "Next next ID");
              DASMSG_SET(s2, std::to_string(nextNextID == Vision::UnknownFaceID ? -1 : scores[nextNextIndex]),
                         "Score of nextNextIndex (or -1 if nextNextID is unknown)");
              DASMSG_SEND();

              Result mergeResult = MergeFaces(nextMatchingID, matchingID);
              if(RESULT_OK != mergeResult) {
                LOG_WARNING("FaceRecognizer.RecognizeFace.MergeFacesFailed", "Merging %d into %d",
                            matchingID, nextMatchingID);
              }

              DEV_ASSERT(nextMatchIter != _enrollmentData.end(),
                         "FaceRecognizer.RecognizeFace.NextMatchIterNotSet");

              matchingAlbumEntries[matchIndex] = matchingAlbumEntries[nextIndex];

              matchingID    = nextMatchingID;
              matchingScore = scores[nextIndex];
              matchIndex    = nextIndex;
              matchIter     = nextMatchIter;

              const AlbumEntryID_t recognizedAlbumEntryID = matchingAlbumEntries[matchIndex];
              auto const& matchIterAlbumEntries = matchIter->second.GetAlbumEntries();
              if(matchIterAlbumEntries.find(recognizedAlbumEntryID) == matchIterAlbumEntries.end())
              {
                shouldUpdateAlbumEntry = false;

                DEV_ASSERT_MSG(_albumEntryToFaceID.find(recognizedAlbumEntryID) == _albumEntryToFaceID.end(),
                               "FaceRecognizer.RecognizeFace.UnexpectedAlbumEntryToFaceID",
                               "matchIndex:%d albumEntry:%d still maps to faceID:%d",
                               matchIndex, recognizedAlbumEntryID,
                               _albumEntryToFaceID.at(recognizedAlbumEntryID));
              }
            }
          }

          break;
        }

        ++nextIndex;

      } while(nextIndex < resultNum);
    }

    faceID = matchingID;
    recognitionScore = matchingScore;

    if(shouldUpdateAlbumEntry && (matchingScore >= kFaceEnrollmentUpdateThreshold))
    {
      Result result = UpdateExistingAlbumEntry(matchingAlbumEntries[matchIndex], _feature, recognitionScore);
      if(RESULT_OK != result) {
        LOG_WARNING("FaceRecognizer.RecognizeFace.UpdatingExistingAlbumEntryFailed",
                    "albumEntry:%d", matchingAlbumEntries[matchIndex]);
      }
    }

    if(_enrollmentID == faceID && _enrollmentTrackID != _trackingID)
    {
      LOG_INFO("RecognizeFace.SetEnrollmentTrackID",
               "EnrollmentID:%d EnrollmentTrackID:%d (was %d)",
               _enrollmentID, -_trackingID, -_enrollmentTrackID);
      _enrollmentTrackID = _trackingID;
    }
  }
  else if(_isEnrollmentEnabled && haveEnrollmentCountsLeft)
  {
    const bool enrollingSpecificFace  = (_enrollmentID != UnknownFaceID);
    const bool isTrackingEnrollmentID = (_trackingID == _enrollmentTrackID);

    if(enrollingSpecificFace && isTrackingEnrollmentID)
    {
      DEV_ASSERT(_trackingID != UnknownFaceID, "FaceRecognizer.RecognizeFace.BadDetectionID");

      recognitionScore = 0;
      for(s32 iResult = 0; iResult < resultNum; ++iResult)
      {
        if(GetFaceIDforAlbumEntry(matchingAlbumEntries[iResult]) == _enrollmentID)
        {
          recognitionScore = scores[iResult];
          Result result = UpdateExistingAlbumEntry(matchingAlbumEntries[iResult], _feature, recognitionScore);
          if(RESULT_OK != result) {
            return result;
          }

          LOG_INFO("RecognizeFace.UpdatingEnrollmentFace",
                   "Did not recognize enrollmentID:%d, updating with score %d based on enrolledTrackID:%d",
                   _enrollmentID, recognitionScore, _enrollmentTrackID);

          break;
        }
      }

      faceID = _enrollmentID;
    }
    else if(canEnrollAnyFace)
    {
      if(resultNum == 0 || scores[0] < kFaceRecognitionThreshold-kFaceRecognitionThresholdMarginForAdding)
      {
        LOG_INFO("RecognizeFace.AddingNewUser", "Observed new person. Adding to album.");
        Result lastResult = RegisterNewUser(_feature, faceID);
        if(RESULT_OK != lastResult) {
          return lastResult;
        }
        recognitionScore = kMaxScore;
      }
    }
  }

  return RESULT_OK;
}

void FaceRecognizer::UpdateBestGuessName(const std::vector<AlbumEntryID_t>& matchingAlbumEntries,
                                         const std::vector<RecognitionScore>& scores,
                                         const int resultNum)
{
  _trackingIDtoBestGuessName.erase(_trackingID);
  s32 iResult = 0;
  while(iResult < resultNum && scores[iResult] > kFaceRecognitionGuessThreshold)
  {
    FaceID_t matchingID = GetFaceIDforAlbumEntry(matchingAlbumEntries[iResult]);
    auto matchIter = _enrollmentData.find(matchingID);
    if(matchIter != _enrollmentData.end() && !matchIter->second.IsForThisSessionOnly())
    {
      _trackingIDtoBestGuessName[_trackingID] = matchIter->second.GetName();
      break;
    }
    ++iResult;
  }
}

bool FaceRecognizer::IsMergingAllowed(FaceID_t toFaceID) const
{
  const bool enrollingThisFace   = (_enrollmentID == toFaceID);
  const bool enrollingAnyFace    = (_enrollmentID == UnknownFaceID);
  const bool haveEnrollmentsLeft = (_enrollmentCount != 0);

  const bool isAllowed = (_isEnrollmentEnabled &&
                          haveEnrollmentsLeft &&
                          (enrollingThisFace || enrollingAnyFace));

  return isAllowed;
}

Result FaceRecognizer::MergeFaces(FaceID_t keepID, FaceID_t mergeID)
{
  if(keepID == mergeID)
  {
    LOG_INFO("FaceRecognizer.MergeFaces.NothingToDo",
             "keepID=mergeID=%d", keepID);
    return RESULT_OK;
  }

  auto keepIter = _enrollmentData.find(keepID);
  if(keepIter == _enrollmentData.end()) {
    LOG_WARNING("FaceRecognizer.MergeFaces.MissingEnrollDataForKeepID",
                "KeepID:%d", keepID);
    return RESULT_FAIL;
  }

  auto mergeIter = _enrollmentData.find(mergeID);
  if(mergeIter == _enrollmentData.end()) {
    LOG_WARNING("FaceRecognizer.MergeFaces.MissingEnrollDataForMergeID",
                "MergeID:%d", mergeID);
    return RESULT_FAIL;
  }

  std::vector<AlbumEntryID_t> entriesRemovedFromKeep;
  Result mergeResult = keepIter->second.MergeWith(mergeIter->second, kMaxAlbumEntriesPerFace, entriesRemovedFromKeep);
  if(RESULT_OK != mergeResult) {
    LOG_WARNING("FaceRecognizer.MergeFaces.MergeFailed",
                "Merging faceID:%d with %d",
                keepIter->first, mergeIter->first);
    return mergeResult;
  }

  for(auto & albumEntryPair : keepIter->second.GetAlbumEntries())
  {
    _albumEntryToFaceID[albumEntryPair.first] = keepID;
  }

  for(AlbumEntryID_t removedEntry : entriesRemovedFromKeep)
  {
    _albumEntryToFaceID.erase(removedEntry);
    _album.ClearUser(removedEntry);
  }

  for(auto & albumEntryPair : mergeIter->second.GetAlbumEntries())
  {
    auto iter = _albumEntryToFaceID.find(albumEntryPair.first);
    if(iter != _albumEntryToFaceID.end() && iter->second == mergeID)
    {
      _albumEntryToFaceID.erase(iter);
      _album.ClearUser(albumEntryPair.first);
    }
  }
  mergeIter->second.ClearAlbumEntries();

  Result removeResult = RemoveUser(mergeID);

  if(kFaceRecognitionExtraDebug) {
    LOG_INFO("MergeFaces.Success",
             "Merged FaceID %d into %d. Kept %zu album entries.",
             mergeID, keepID, keepIter->second.GetAlbumEntries().size());
  }

  return removeResult;
}

Result FaceRecognizer::GetSerializedAlbum(std::vector<u8>& serializedAlbum) const
{
  serializedAlbum.clear();

  if(!_isInitialized) {
    LOG_WARNING("FaceRecognizer.GetSerializedAlbum.NotInitialized", "");
    return RESULT_FAIL;
  }

  FaceAlbum tempAlbum(kMaxTotalAlbumEntries, kMaxEnrollDataPerAlbumEntry);

  s32 permanentIdCount = 0;
  for(auto const& enrollData : _enrollmentData)
  {
    if(permanentIdCount >= kMaxNamedFacesInAlbum)
    {
      LOG_WARNING("FaceRecognizer.GetSerializedAlbum.MaxNumFacesReached",
                  "Can't save more than %d faces",
                  kMaxNamedFacesInAlbum);
      break;
    }

    if(enrollData.second.IsForThisSessionOnly()) {
      continue;
    }

    for(auto & albumEntryPair : enrollData.second.GetAlbumEntries())
    {
      const AlbumEntryID_t albumEntry = albumEntryPair.first;

      for(s32 iData = 0; iData < kMaxEnrollDataPerAlbumEntry; ++iData)
      {
        Feature feature;
        if(!_album.GetFeature(albumEntry, iData, feature)) {
          continue;
        }

        if(RESULT_OK != tempAlbum.RegisterData(feature, albumEntry, iData)) {
          LOG_WARNING("FaceRecognizer.GetSerializedAlbum.RegisterFail",
                      "Could not register temp feature for face:%d albumEntry:%d data:%d",
                      enrollData.first, albumEntry, iData);
          return RESULT_FAIL;
        }
      }
    }
    ++permanentIdCount;
  }

  if(permanentIdCount > 0)
  {
    const Result result = tempAlbum.Serialize(serializedAlbum);
    if(RESULT_OK != result) {
      LOG_WARNING("FaceRecognizer.GetSerializedAlbum.SerializeFail", "");
      return result;
    }

    LOG_INFO("GetSerializedAlbum.AlbumSize",
             "Album with %d permanent faces = %zu bytes (%.1f bytes/person)",
             permanentIdCount, serializedAlbum.size(),
             (f32)serializedAlbum.size()/(f32)permanentIdCount);
  }
  else
  {
    LOG_INFO("GetSerializedAlbum.NoNamedIDs", "");
  }

  return RESULT_OK;
}

s32 FaceRecognizer::GetNumNamedFaces() const
{
  s32 namedCount = 0;
  for(auto const& enrollData : _enrollmentData)
  {
    const bool isNamed = !enrollData.second.IsForThisSessionOnly();
    if(isNamed)
    {
      ++namedCount;
    }
  }

  return namedCount;
}

bool FaceRecognizer::CanAddNamedFace() const
{
  const s32 numNamedFaces = GetNumNamedFaces();
  return (numNamedFaces < kMaxNamedFacesInAlbum);
}

Result FaceRecognizer::AssignNameToID(FaceID_t faceID, const std::string& name, FaceID_t mergeWithID)
{
  auto iterToRename = _enrollmentData.end();

  if(mergeWithID != UnknownFaceID)
  {
    iterToRename = _enrollmentData.find(mergeWithID);

    if(iterToRename == _enrollmentData.end())
    {
      LOG_WARNING("FaceRecognizer.AssignNameToID.InvalidMergeWithID",
                  "No enrollment data for mergeWithID=%d", mergeWithID);
      return RESULT_FAIL;
    }

    if(iterToRename->second.IsForThisSessionOnly())
    {
      LOG_WARNING("FaceRecognizer.AssignNameToID.SessionOnlyMergeWithID",
                  "MergeWithID must be for a named face, not session-only");
      return RESULT_FAIL;
    }

    Result lastResult = MergeFaces(mergeWithID, faceID);
    if(RESULT_OK != lastResult) {
      LOG_WARNING("FaceRecognizer.AssignNameToID.MergeFailed",
                  "Merging faceID:%d into mergeWithID:%d",
                  faceID, mergeWithID);
      return lastResult;
    }
  }
  else
  {
    if(!CanAddNamedFace())
    {
      LOG_ERROR("FaceRecognizer.AssignNameToID.TooManyNamedFaces",
                "Already have %d named faces", kMaxNamedFacesInAlbum);
      return RESULT_FAIL;
    }
    iterToRename = _enrollmentData.find(faceID);
  }

  if(iterToRename != _enrollmentData.end())
  {
    iterToRename->second.SetName( name );
  }
  else
  {
    LOG_WARNING("FaceRecognizer.AssignNameToID.InvalidID",
                "Unknown ID %d, ignoring name %s",
                faceID, Util::HidePersonallyIdentifiableInfo(name.c_str()));

    return RESULT_FAIL;
  }

  return RESULT_OK;
}

Result FaceRecognizer::EraseFace(FaceID_t faceID)
{
  auto enrollIter = _enrollmentData.find(faceID);
  if(enrollIter != _enrollmentData.end())
  {
    Result removeResult = RemoveUser(enrollIter->first);
    if(RESULT_OK != removeResult) {
      LOG_WARNING("FaceRecognizer.EraseName.RemoveUserFailed", "ID=%d", enrollIter->first);
    }
    return removeResult;
  }
  else {
    LOG_WARNING("FaceRecognizer.EraseFace.NotFound",
                "Did not find a record for ID=%d", faceID);
    return RESULT_FAIL;
  }
}

std::vector<Vision::LoadedKnownFace> FaceRecognizer::GetEnrolledNames() const
{
  std::vector<LoadedKnownFace> ret;
  ret.reserve( _enrollmentData.size() );
  for( const auto& entry : _enrollmentData ) {
    if( !entry.second.GetName().empty() ) {
      EmplaceLoadedKnownFace(entry.second, ret);
    }
  }
  return ret;
}

void FaceRecognizer::EraseAllFaces()
{
  for(auto enrollIter=_enrollmentData.begin(); enrollIter!=_enrollmentData.end(); )
  {
    enrollIter = RemoveUser(enrollIter);
  }

  _enrollmentData.clear();
  _albumEntryToFaceID.clear();
  _album.Clear();

  LOG_INFO("EraseAllFaces.Complete", "");
}

Result FaceRecognizer::RenameFace(FaceID_t faceID, const std::string& oldName, const std::string& newName,
                                  Vision::RobotRenamedEnrolledFace& renamedFace)
{
  auto enrollIter = _enrollmentData.find(faceID);
  if(enrollIter != _enrollmentData.end())
  {
    if(enrollIter->second.GetName() == oldName)
    {
      enrollIter->second.SetName( newName );

      LOG_INFO("FaceRecognizer.RenameFace.Success", "Renamed ID=%d from '%s' to '%s'",
               faceID, Util::HidePersonallyIdentifiableInfo(oldName.c_str()),
               enrollIter->second.GetName().piiGuardedString());

      Vision::RobotRenamedEnrolledFace temp(enrollIter->second.GetFaceID(),
                                            enrollIter->second.GetName());
      std::swap(temp, renamedFace);

      return RESULT_OK;
    }
    else
    {
      LOG_WARNING("FaceRecognizer.RenameFace.OldNameMismatch",
                  "OldName '%s' does not match stored name '%s' for ID=%d",
                  Util::HidePersonallyIdentifiableInfo(oldName.c_str()),
                  enrollIter->second.GetName().piiGuardedString(),
                  faceID);

      return RESULT_FAIL;
    }
  }
  else
  {
    LOG_WARNING("FaceRecognizer.RenameFace.InvalidID", "No record for ID=%d", faceID);
    return RESULT_FAIL;
  }
}

Result FaceRecognizer::GetSerializedData(std::vector<u8>& albumData,
                                         std::vector<u8>& enrollData)
{
  Result lastResult = GetSerializedAlbum(albumData);

  if(RESULT_OK != lastResult) {
    LOG_WARNING("FaceRecognizer.GetSerializedData.GetSerializedAlbumFail", "");
  } else if(!albumData.empty()) {
    lastResult = GetSerializedEnrollData(enrollData);
    if(RESULT_OK != lastResult) {
      LOG_WARNING("FaceRecognizer.GetSerializedData.GetSerializedEnrollDataFail", "");
    }
  }

  return lastResult;
}

Result FaceRecognizer::SetSerializedData(const std::vector<u8>& albumData,
                                         const std::vector<u8>& enrollData,
                                         std::list<LoadedKnownFace>& loadedFaces)
{
  FaceAlbum loadedAlbum(kMaxTotalAlbumEntries, kMaxEnrollDataPerAlbumEntry);
  EnrollmentData loadedEnrollmentData;

  Result lastResult = loadedAlbum.Deserialize(albumData);

  if(RESULT_OK != lastResult) {
    LOG_WARNING("FaceRecognizer.SetSerializedData.SetSerializedAlbumFail", "");
    return lastResult;
  }

  FaceID_t loadedNextFaceID = UnknownFaceID;
  lastResult = SetSerializedEnrollData(enrollData, loadedEnrollmentData, loadedNextFaceID);
  if(RESULT_OK != lastResult) {
    LOG_WARNING("FaceRecognizer.SetSerializedData.SetSerializedEnrollDataFail", "");
    return lastResult;
  }

  lastResult = UseLoadedAlbumAndEnrollData(loadedAlbum, loadedEnrollmentData);

  if(RESULT_OK == lastResult)
  {
    LOG_INFO("SetSerializedData.NewNextFaceID", "Setting next FaceID=%d", loadedNextFaceID);

    _nextFaceID = loadedNextFaceID;

    for(auto & entry : _enrollmentData)
    {
      const auto & faceID = entry.second.GetFaceID();

      const size_t numAlbumEntries = entry.second.GetAlbumEntries().size();
      LOG_INFO("robot.vision.loaded_face_enrollment_entry",
               "numAlbumEntries %zu, faceID %d",
               numAlbumEntries,
               faceID);

      DASMSG(vision.face_recognition.loaded_face_enrollment_entry,
             "vision.face_recognition.loaded_face_enrollment_entry",
             "We have loaded a face enrollment entry");
      DASMSG_SET(i1, faceID, "Face ID");
      DASMSG_SET(i2, numAlbumEntries, "Number of album entries");
      DASMSG_SEND();

      EmplaceLoadedKnownFace(entry.second, loadedFaces, "SetSerializedData.AddedEnrollmentDataEntry");
    }
  }

  return lastResult;
}

Result FaceRecognizer::UseLoadedAlbumAndEnrollData(FaceAlbum& loadedAlbumData,
                                                   EnrollmentData& loadedEnrollmentData)
{
  AlbumEntryToFaceID loadedAlbumEntryToFaceID;
  for(auto & enrollData : loadedEnrollmentData)
  {
    for(auto & albumEntry : enrollData.second.GetAlbumEntries())
    {
      loadedAlbumEntryToFaceID[albumEntry.first] = enrollData.first;
    }
  }

  Result lastResult = SanityCheckBookkeeping(loadedAlbumData,
                                             loadedEnrollmentData,
                                             loadedAlbumEntryToFaceID);

  if(RESULT_OK == lastResult)
  {
    std::swap(loadedAlbumData, _album);
    std::swap(loadedEnrollmentData, _enrollmentData);
    std::swap(loadedAlbumEntryToFaceID, _albumEntryToFaceID);

    _trackingToFaceID.clear();
    _trackingIDtoBestGuessName.clear();

    LOG_INFO("UseLoadedAlbumAndEnrollData.Success",
             "Loaded album and enroll data passed sanity checks (%zu entries)",
             _enrollmentData.size());
  }

  return lastResult;
}

Result FaceRecognizer::GetSerializedEnrollData(std::vector<u8>& serializedEnrollData)
{
  serializedEnrollData.clear();

  if(!_enrollmentData.empty())
  {
    const u8 * VersionPrefixU8 = (u8*)VersionPrefix;
    std::copy(VersionPrefixU8, VersionPrefixU8+4, std::back_inserter(serializedEnrollData));

    LOG_INFO("GetSerializedEnrollData.AddedVersionData",
             "Added to front: %04X%04X",
             ((u16*)serializedEnrollData.data())[0],
             ((u16*)serializedEnrollData.data())[1]);

    const u8* faceIdU8 = (u8*)(&_nextFaceID);
    std::copy(faceIdU8, faceIdU8+sizeof(FaceID_t), std::back_inserter(serializedEnrollData));

    for(auto & enrollData : _enrollmentData)
    {
      if(false == enrollData.second.IsForThisSessionOnly()) {
        enrollData.second.Serialize(serializedEnrollData);
      }
    }
  }

  return RESULT_OK;
}

Result FaceRecognizer::SetSerializedEnrollData(const std::vector<u8>& serializedEnrollData,
                                               EnrollmentData& newEnrollmentData,
                                               FaceID_t& newNextFaceID)
{
  const size_t kMinSize = sizeof(VersionPrefix) + sizeof(FaceID_t);
  if(serializedEnrollData.size() < kMinSize) {
    LOG_WARNING("FaceRecognizer.SetSerializedEnrollData.TooShortForVersion",
                "Data is not even %zu bytes long to read version and nextFaceID",
                kMinSize);
    return RESULT_FAIL;
  }

  const u32 incomingVersion = ((u32*)serializedEnrollData.data())[0];
  const u32 correctVersion  = ((u32*)VersionPrefix)[0];

  if(incomingVersion != correctVersion)
  {
    LOG_WARNING("FaceRecognizer.SetSerializedEnrollData.VersionPrefixMismatch",
                "Expected: %04X%04X, Incoming: %04X%04X",
                VersionPrefix[0], VersionPrefix[1],
                ((u16*)serializedEnrollData.data())[0],
                ((u16*)serializedEnrollData.data())[1]);

    return RESULT_FAIL;
  }

  LOG_INFO("SetSerializedEnrollData.MatchedVersionPrefix",
           "Got correct prefix: %04X%04X", VersionPrefix[0], VersionPrefix[1]);

  EnrolledFaceEntry entry;

  size_t startIndex = sizeof(VersionPrefix);

  newNextFaceID = ((FaceID_t*)(serializedEnrollData.data() + startIndex))[0];

  startIndex += sizeof(FaceID_t);

  while(startIndex < serializedEnrollData.size() - 3)
  {
    Result lastResult = entry.Deserialize(serializedEnrollData, startIndex);
    if(RESULT_OK != lastResult) {
      LOG_WARNING("FaceRecognizer.SetSerializedEnrollData.DeserializeFail",
                  "Failed to deserialize EnrolledFaceEntry at start=%zu (from %zu-length buffer)",
                  startIndex, serializedEnrollData.size());
      return RESULT_FAIL;
    }

    newEnrollmentData[entry.GetFaceID()] = entry;
  }

  LOG_INFO("SetSerializedEnrollData.Complete",
           "Deserialized %zu entries from buffer", newEnrollmentData.size());

  return RESULT_OK;
}

Result FaceRecognizer::SaveAlbum(const std::string &albumName)
{
  Result result = RESULT_OK;

  std::vector<u8> serializedAlbum;
  result = GetSerializedAlbum(serializedAlbum);
  if(RESULT_OK != result) {
    return result;
  }

  if(serializedAlbum.empty()) {
    LOG_INFO("FaceRecognizer.SaveAlbum.EmptyAlbum",
             "No serialized data returned from private implementation; removing folder");
    Util::FileUtils::RemoveDirectory(albumName);
    return result;
  }

  if(false == Util::FileUtils::CreateDirectory(albumName, false, true)) {
    LOG_WARNING("FaceRecognizer.SaveAlbum.DirCreationFail",
                "Tried to create: %s", albumName.c_str());
    result = RESULT_FAIL;
  } else {

    const std::string dataFilename(albumName + "/data.bin");
    std::fstream fs;
    fs.open(dataFilename, std::ios::binary | std::ios::out);
    if(!fs.is_open()) {
      LOG_WARNING("FaceRecognizer.SaveAlbum.FileOpenFail", "Filename: %s", dataFilename.c_str());
      result = RESULT_FAIL;
    } else {

      fs.write((const char*)&(serializedAlbum[0]), serializedAlbum.size());
      fs.close();

      if((fs.rdstate() & std::ios::badbit) || (fs.rdstate() & std::ios::failbit)) {
        LOG_WARNING("FaceRecognizer.SaveAlbum.FileWriteFail", "Filename: %s", dataFilename.c_str());
        result = RESULT_FAIL;
      } else {
        Json::Value json;
        for(auto & enrollData : _enrollmentData)
        {
          if(false == enrollData.second.IsForThisSessionOnly())
          {
            Json::Value entry;
            enrollData.second.FillJson(entry);
            json[std::to_string(enrollData.first)] = std::move(entry);
          }
        }

        const std::string enrollDataFilename(albumName + "/enrollData.json");
        Json::FastWriter writer;
        fs.open(enrollDataFilename, std::ios::out);
        if (!fs.is_open()) {
          LOG_WARNING("FaceRecognizer.SaveAlbum.EnrollDataFileOpenFail", "");
          result = RESULT_FAIL;
        } else {
          fs << writer.write(json);
          fs.close();
        }
      }
    }
  }

  return result;
}

Result FaceRecognizer::LoadAlbum(const std::string& albumName,
                                 std::list<LoadedKnownFace>& namesAndIDs)
{
  if(!_isInitialized) {
    LOG_ERROR("FaceRecognizer.LoadAlbum.NotInitialized", "");
    return RESULT_FAIL;
  }

  EraseAllFaces();

  if(!Util::FileUtils::DirectoryExists(albumName))
  {
    LOG_INFO("FaceRecognizer.LoadAlbum.DoesNotExist",
             "Album %s does not exist yet: no faces enrolled yet",
             albumName.c_str());

    return RESULT_OK;
  }

  const std::string dataFilename(Util::FileUtils::FullFilePath({albumName, "data.bin"}));
  std::ifstream fs(dataFilename, std::ios::in | std::ios::binary);
  if(!fs.is_open()) {
    LOG_WARNING("FaceRecognizer.LoadAlbum.FileOpenFail", "Filename: %s", dataFilename.c_str());
    return RESULT_FAIL;
  }

  fs.unsetf(std::ios::skipws);

  fs.seekg(0, std::ios::end);
  auto fileLength = fs.tellg();
  fs.seekg(0, std::ios::beg);

  std::vector<u8> serializedAlbum;
  serializedAlbum.reserve((size_t)fileLength);

  serializedAlbum.insert(serializedAlbum.begin(),
                         std::istream_iterator<u8>(fs),
                         std::istream_iterator<u8>());
  fs.close();

  if((std::streamoff)serializedAlbum.size() != fileLength) {
    LOG_WARNING("FaceRecognizer.LoadAlbum.FileReadFail", "Filename: %s", dataFilename.c_str());
    return RESULT_FAIL;
  }

  FaceAlbum loadedAlbum(kMaxTotalAlbumEntries, kMaxEnrollDataPerAlbumEntry);
  EnrollmentData loadedEnrollmentData;

  Result result = loadedAlbum.Deserialize(serializedAlbum);
  if(RESULT_OK != result) {
    return result;
  }

  Json::Value json;
  const std::string namesFilename(albumName + "/enrollData.json");
  std::ifstream jsonFile(namesFilename);
  Json::Reader reader;
  const bool success = reader.parse(jsonFile, json);
  jsonFile.close();
  if(!success) {
    LOG_WARNING("FaceRecognizer.LoadAlbum.EnrollDataFileReadFail", "");
    return RESULT_FAIL;
  }

  for(auto & idStr : json.getMemberNames()) {
    FaceID_t faceID = std::stoi(idStr);
    if(!json.isMember(idStr)) {
      LOG_WARNING("FaceRecognizer.LoadAlbum.BadFaceIdString",
                  "Could not find member for string %s with value %d",
                  idStr.c_str(), faceID);
      return RESULT_FAIL;
    }

    EnrolledFaceEntry entry(faceID, json[idStr]);
    EmplaceLoadedKnownFace(entry, namesAndIDs, "LoadAlbum.LoadedEnrollmentData");

    loadedEnrollmentData[faceID] = std::move(entry);
  }

  return UseLoadedAlbumAndEnrollData(loadedAlbum, loadedEnrollmentData);
}

bool FaceRecognizer::GetFaceIDFromTrackingID(const TrackingID_t trackingID, FaceID_t& faceID) const
{
  auto iter = _trackingToFaceID.find(trackingID);
  if(iter == _trackingToFaceID.end()) {
    return false;
  } else {
    faceID = iter->second;
    return true;
  }
}

std::string FaceRecognizer::GetBestGuessNameForTrackingID(const TrackingID_t trackingID) const
{
  auto iter = _trackingIDtoBestGuessName.find(trackingID);
  if(iter == _trackingIDtoBestGuessName.end())
  {
    return "";
  }
  else
  {
    return iter->second;
  }
}

bool FaceRecognizer::GetLandmarksFromFace(const TrackedFace& face, FaceLandmarks& landmarks)
{
  const auto& leftEye   = face.GetFeature(TrackedFace::FeatureName::LeftEye);
  const auto& rightEye  = face.GetFeature(TrackedFace::FeatureName::RightEye);
  const auto& nose      = face.GetFeature(TrackedFace::FeatureName::Nose);
  const auto& mouth     = face.GetFeature(TrackedFace::FeatureName::UpperLip);

  if(leftEye.empty() || rightEye.empty() || nose.empty() || mouth.size() < 2) {
    return false;
  }

  landmarks[0] = leftEye.front();
  landmarks[1] = rightEye.front();
  landmarks[2] = nose.front();
  landmarks[3] = mouth.front();
  landmarks[4] = mouth.back();

  return true;
}

Result FaceRecognizer::ComputeFeaturesFromFace(const Image& img, const TrackedFace& face, Feature& feature) const
{
  FaceLandmarks landmarks;
  if(!GetLandmarksFromFace(face, landmarks))
  {
    LOG_WARNING("FaceRecognizer.ComputeFeaturesFromFace.NoLandmarks", "");
    return RESULT_FAIL;
  }

  return _embedder.ComputeEmbedding(img, landmarks, feature);
}

#if ANKI_DEVELOPER_CODE
Result FaceRecognizer::DevAddFaceToAlbum(const Image& img, const TrackedFace& face, int albumEntry)
{
  Feature feature;
  const Result result = ComputeFeaturesFromFace(img, face, feature);
  if(RESULT_OK != result)
  {
    LOG_ERROR("FaceRecognizer.DevAddFaceToAlbum.ComputeFeaturesFailed", "");
    return RESULT_FAIL;
  }

  if(albumEntry >= _album.GetMaxUsers())
  {
    LOG_ERROR("FaceRecognizer.DevAddFaceToAlbum.AlbumFull", "MaxUsers:%d AlbumEntry:%d",
              _album.GetMaxUsers(), albumEntry);
    return RESULT_FAIL;
  }

  const s32 numUserData = _album.GetRegisteredUserDataNum(albumEntry);
  if(numUserData >= _album.GetMaxDataPerUser())
  {
    LOG_ERROR("FaceRecognizer.DevAddFaceToAlbum.UserFull", "AlbumEntry:%d MaxData:%d UserData:%d",
              albumEntry, _album.GetMaxDataPerUser(), numUserData);
    return RESULT_FAIL;
  }

  return _album.RegisterData(feature, albumEntry, numUserData);
}

Result FaceRecognizer::DevFindFaceInAlbum(const Image& img, const TrackedFace& face, int& albumEntry, float& score) const
{
  std::vector<std::pair<int,float>> matches;
  const Result result = DevFindFaceInAlbum(img, face, 1, matches);
  if(RESULT_OK == result && !matches.empty())
  {
    const auto& firstMatch = matches.front();
    albumEntry = firstMatch.first;
    score = firstMatch.second;
  }

  return result;
}

Result FaceRecognizer::DevFindFaceInAlbum(const Image& img, const TrackedFace& face, const int maxMatches,
                                          std::vector<std::pair<int, float>>& matches) const
{
  Feature feature;
  const Result result = ComputeFeaturesFromFace(img, face, feature);
  if(RESULT_OK != result)
  {
    LOG_ERROR("FaceRecognizer.FindFaceInAlbum.ComputeFeaturesFailed", "");
    return RESULT_FAIL;
  }

  std::vector<s32> userIDs(maxMatches);
  std::vector<s32> rawScores(maxMatches);
  s32 resultNum = 0;
  _album.Identify(feature, maxMatches, userIDs, rawScores, resultNum);

  matches.clear();
  matches.reserve(resultNum);
  for(s32 i = 0; i < resultNum; ++i)
  {
    matches.emplace_back(userIDs[i], (float)rawScores[i] * 0.001f);
  }

  return RESULT_OK;
}

float FaceRecognizer::DevComputePairwiseMatchScore(int faceID1, int faceID2) const
{
  Feature feature;
  if(!_album.GetFeature(faceID1, 0, feature))
  {
    LOG_ERROR("FaceRecognizer.DevComputePairwiseMatchScore.GetFeatureFail", "AlbumEntry:%d", faceID1);
    return -1.f;
  }

  return (float)_album.Verify(feature, faceID2) * 0.001f;
}

float FaceRecognizer::DevComputePairwiseMatchScore(int faceID1, const Image& img2, const TrackedFace& face2) const
{
  Feature feature;
  if(RESULT_OK != ComputeFeaturesFromFace(img2, face2, feature))
  {
    LOG_ERROR("FaceRecognizer.DevComputePairwiseMatchScore.ComputeFeaturesFailed", "");
    return -1.f;
  }

  return (float)_album.Verify(feature, faceID1) * 0.001f;
}

float FaceRecognizer::DevComputePairwiseMatchScore(const Image& img1, const TrackedFace& face1,
                                                   const Image& img2, const TrackedFace& face2)
{
  Feature feature1, feature2;
  if(RESULT_OK != ComputeFeaturesFromFace(img1, face1, feature1) ||
     RESULT_OK != ComputeFeaturesFromFace(img2, face2, feature2))
  {
    LOG_ERROR("FaceRecognizer.DevComputePairwiseMatchScore.ComputeFeaturesFailed", "");
    return 0.f;
  }

  return (float)FaceAlbum::ComputeScore(feature1, feature2) * 0.001f;
}
#endif

#if ANKI_DEV_CHEATS
void FaceRecognizer::SaveAllRecognitionImages(const std::string& imagePathPrefix)
{
  for (const auto& albumEntry: _enrollmentImages) {
    const AlbumEntryID_t entryId = albumEntry.first;
    for (const auto& enrollmentImage: albumEntry.second) {
      if(enrollmentImage.IsEmpty()) {
        continue;
      }
      const std::string fullFilename = imagePathPrefix + "_" + std::to_string(_albumEntryToFaceID[entryId])
                                       + "_" + std::to_string(entryId) + "_" +
                                       std::to_string(enrollmentImage.GetTimestamp()) + ".jpg";
      const Result result = enrollmentImage.Save(fullFilename);
      if (result != RESULT_OK) {
        LOG_ERROR("FaceRecognizer.SaveAllRecognitionImages.FailedToSave",
                  "Failed to save file:%s.", fullFilename.c_str());
      }
    }
  }
}

void FaceRecognizer::DeleteAllRecognitionImages()
{
  _enrollmentImages.clear();
}
#endif

}
}

#endif
