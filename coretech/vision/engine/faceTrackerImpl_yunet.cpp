#if FACE_TRACKER_PROVIDER == FACE_TRACKER_YUNET

#include "faceTrackerImpl_yunet.h"

#include "coretech/common/engine/jsonTools.h"
#include "coretech/common/engine/math/quad.h"
#include "coretech/vision/engine/camera.h"

#include "util/console/consoleInterface.h"
#include "util/fileUtils/fileUtils.h"
#include "util/logging/logging.h"

#include "opencv2/calib3d.hpp"

#include <future>

#define LOG_CHANNEL "FaceRecognizer"

namespace Anki {
namespace Vision {

  namespace FaceEnrollParams {
    CONSOLE_VAR(f32, kMinDetectionScoreForEnrollment,  "Vision.FaceTracker",  0.85f);
    CONSOLE_VAR(f32, kCloseDistanceBetweenEyesMin,  "Vision.FaceTracker",  64.f);
    CONSOLE_VAR(f32, kCloseDistanceBetweenEyesMax,  "Vision.FaceTracker",  128.f);
    CONSOLE_VAR(f32, kFarDistanceBetweenEyesMin,    "Vision.FaceTracker",  16.f);
    CONSOLE_VAR(f32, kFarDistanceBetweenEyesMax,    "Vision.FaceTracker",  32.f);
    CONSOLE_VAR(f32, kLookingStraightMaxAngle_deg,  "Vision.FaceTracker",  25.f);
    CONSOLE_VAR(f32, kLookingUpMinAngle_deg,        "Vision.FaceTracker",  25.f);
    CONSOLE_VAR(f32, kLookingUpMaxAngle_deg,        "Vision.FaceTracker",  45.f);
    CONSOLE_VAR(f32, kLookingDownMinAngle_deg,      "Vision.FaceTracker", -10.f);
    CONSOLE_VAR(f32, kLookingDownMaxAngle_deg,      "Vision.FaceTracker", -25.f);
  }

  namespace DetectParams {
    CONSOLE_VAR_RANGED(f32, kFaceDetectionThreshold,   "Vision.FaceDetectorCommon", 0.6f, 0.f, 1.f);
    CONSOLE_VAR_RANGED(f32, kRefineDetectionThreshold, "Vision.FaceDetectorCommon", 0.4f, 0.f, 1.f);
    CONSOLE_VAR_RANGED(s32, kMaxDetectedFaces,         "Vision.FaceDetectorCommon", 10, 1, 64);
    CONSOLE_VAR_RANGED(s32, kMinFaceSize,              "Vision.FaceDetectorCommon", 20, 8, 8192);
    CONSOLE_VAR_RANGED(f32, kTrackingMinIOU,           "Vision.FaceDetectorCommon", 0.35f, 0.f, 1.f);
    CONSOLE_VAR_RANGED(f32, kTrackingMaxAreaRatio,     "Vision.FaceDetectorCommon", 2.0f, 1.f, 100.f);
    CONSOLE_VAR_RANGED(s32, kMaxTrackAge_ms,           "Vision.FaceDetectorCommon", 1000, 0, 10000);
    CONSOLE_VAR_RANGED(s32, kNamedTrackReverify_ms,    "Vision.FaceDetectorCommon", 3000, 0, 60000);
    CONSOLE_VAR(bool,       kRefineLandmarks,          "Vision.FaceDetectorCommon", false);
  }

  CONSOLE_VAR(bool, kUseUndistortionForFacePose,  "Vision.FaceDetectorCommon", true);
  CONSOLE_VAR(bool, kAdjustEyeDistByYaw,          "Vision.FaceDetectorCommon", true);

  namespace {
    const char* const kDetectorModelFile = "yunet_trunk.tflite";
    const char* const kEmbedderModelFile = "mobilefacenet.tflite";

    const char* const kDefaultGpuCacheDir = "/data/data/com.anki.victor/cache/gpuCache";

    namespace JsonKey {
      const char* FaceDetectionGroup = "FaceDetection";
      const char* UseGpu             = "UseGpu";
      const char* GpuCacheDir        = "GpuCacheDir";
      const char* NumThreads         = "NumThreads";
    }

    const f32 MinDistBetweenEyes_pixels = 6.f;

    const f32 DistanceBetweenEyes_mm = 62.f;

    const f32 kFaceModel_mm[kNumFaceLandmarks][3] = {
      {-32.f, -33.f,  35.f},
      { 32.f, -33.f,  35.f},
      {  0.f,   0.f,   0.f},
      {-27.f,  30.f,  30.f},
      { 27.f,  30.f,  30.f}
    };
  }

  static Vec3f GetTranslation(const Point2f& leftEye, const Point2f& rightEye, const f32 intraEyeDist,
                              const CameraCalibration& scaledCalib)
  {
    Point2f eyeMidPoint(leftEye);
    eyeMidPoint += rightEye;
    eyeMidPoint *= 0.5f;

    Vec3f ray(eyeMidPoint.x(), eyeMidPoint.y(), 1.f);
    ray = scaledCalib.GetInvCalibrationMatrix() * ray;
    ray.MakeUnitLength();

    ray *= scaledCalib.GetFocalLength_x() * DistanceBetweenEyes_mm / intraEyeDist;

    return ray;
  }

  static f32 ComputeIOU(const Rectangle<f32>& a, const Rectangle<f32>& b)
  {
    const cv::Rect2f rectA = a.get_CvRect_();
    const cv::Rect2f rectB = b.get_CvRect_();
    const f32 intersection = (rectA & rectB).area();
    const f32 unionArea    = rectA.area() + rectB.area() - intersection;
    return (unionArea > 0.f ? intersection/unionArea : 0.f);
  }

  FaceTracker::Impl::Impl(const Camera&        camera,
                          const std::string&   modelPath,
                          const Json::Value&   config)
  : _camera(camera)
  , _modelPath(modelPath)
  , _recognizer(config)
  {
    FaceNetConfig netConfig;
    netConfig.useGpu      = true;
    netConfig.gpuCacheDir = kDefaultGpuCacheDir;
    netConfig.numThreads  = 2;

    if(config.isMember(JsonKey::FaceDetectionGroup))
    {
      const Json::Value& detectionConfig = config[JsonKey::FaceDetectionGroup];
      JsonTools::GetValueOptional(detectionConfig, JsonKey::UseGpu,      netConfig.useGpu);
      JsonTools::GetValueOptional(detectionConfig, JsonKey::GpuCacheDir, netConfig.gpuCacheDir);
      JsonTools::GetValueOptional(detectionConfig, JsonKey::NumThreads,  netConfig.numThreads);
    }

    const std::string modelDir = Util::FileUtils::FullFilePath({modelPath, "dnn_models"});

    FaceNetConfig detectorConfig = netConfig;
    detectorConfig.modelPath = Util::FileUtils::FullFilePath({modelDir, kDetectorModelFile});

    FaceNetConfig embedderConfig = netConfig;
    embedderConfig.modelPath = Util::FileUtils::FullFilePath({modelDir, kEmbedderModelFile});

    std::future<Result> detectorInitResult = std::async(std::launch::async, [this, &detectorConfig]() {
      return _detector.Init(detectorConfig);
    });

    const Result recognizerInitResult = _recognizer.Init(embedderConfig);

    if(RESULT_OK != detectorInitResult.get())
    {
      LOG_ERROR("FaceTrackerImpl.Constructor.DetectorInitFailed", "%s",
                detectorConfig.modelPath.c_str());
      return;
    }

    if(RESULT_OK != recognizerInitResult)
    {
      LOG_ERROR("FaceTrackerImpl.Constructor.RecognizerInitFailed", "%s",
                embedderConfig.modelPath.c_str());
      return;
    }

    _isInitialized = true;

    LOG_INFO("FaceTrackerImpl.Constructor.Success",
             "Detector running on %s", (_detector.IsUsingGpu() ? "GPU" : "CPU"));
  }

  FaceTracker::Impl::~Impl()
  {
    _recognizer.Shutdown();
    _isInitialized = false;
  }

  void FaceTracker::Impl::Reset()
  {
    _allowedTrackedFaceID.clear();
    _tracks.clear();
    _recognizer.ClearAllTrackingData();
  }

  void FaceTracker::Impl::ClearAllowedTrackedFaces()
  {
    Reset();
  }

  void FaceTracker::Impl::AddAllowedTrackedFace(const FaceID_t faceID)
  {
    _allowedTrackedFaceID.insert(faceID);
  }

  void FaceTracker::Impl::SetRecognitionIsSynchronous(bool isSynchronous)
  {
    _recognizer.SetIsSynchronous(isSynchronous);
  }

  void FaceTracker::Impl::PruneStaleTracks(TimeStamp_t timestamp)
  {
    for(auto iter = _tracks.begin(); iter != _tracks.end(); )
    {
      if(timestamp > iter->lastSeenTime &&
         (timestamp - iter->lastSeenTime) > (TimeStamp_t)DetectParams::kMaxTrackAge_ms)
      {
        iter = _tracks.erase(iter);
      }
      else
      {
        ++iter;
      }
    }
  }

  FaceTracker::Impl::TrackingID_t FaceTracker::Impl::AssociateWithTrack(const Rectangle<f32>& rect,
                                                                        TimeStamp_t timestamp,
                                                                        bool& isBeingTracked)
  {
    Track* bestTrack = nullptr;
    f32 bestIOU = DetectParams::kTrackingMinIOU;

    for(auto & track : _tracks)
    {
      if(track.lastSeenTime == timestamp)
      {
        continue;
      }

      const f32 trackArea = track.rect.Area();
      const f32 detectArea = rect.Area();
      if(trackArea <= 0.f || detectArea <= 0.f)
      {
        continue;
      }

      const f32 areaRatio = std::max(trackArea/detectArea, detectArea/trackArea);
      if(areaRatio > DetectParams::kTrackingMaxAreaRatio)
      {
        continue;
      }

      const f32 iou = ComputeIOU(track.rect, rect);
      if(iou > bestIOU)
      {
        bestIOU = iou;
        bestTrack = &track;
      }
    }

    if(nullptr == bestTrack)
    {
      _tracks.emplace_back();
      bestTrack = &_tracks.back();
      bestTrack->id = _nextTrackingID++;
      if(_nextTrackingID <= 0)
      {
        _nextTrackingID = 1;
      }
      bestTrack->numFramesSeen = 0;
      bestTrack->lastRecognizedTime = 0;
    }

    isBeingTracked = (bestTrack->numFramesSeen > 0);

    bestTrack->rect = rect;
    bestTrack->lastSeenTime = timestamp;
    ++bestTrack->numFramesSeen;

    return bestTrack->id;
  }

  FaceTracker::Impl::Track* FaceTracker::Impl::FindTrack(TrackingID_t trackingID)
  {
    for(auto & track : _tracks)
    {
      if(track.id == trackingID)
      {
        return &track;
      }
    }
    return nullptr;
  }

  void FaceTracker::Impl::SetFaceFeatures(const FaceLandmarks& landmarks, TrackedFace& face) const
  {
    face.SetEyeCenters(Point2f(landmarks[0]), Point2f(landmarks[1]));

    face.SetFeature(TrackedFace::FeatureName::LeftEye,  {landmarks[0]}, {1});
    face.SetFeature(TrackedFace::FeatureName::RightEye, {landmarks[1]}, {1});
    face.SetFeature(TrackedFace::FeatureName::Nose,     {landmarks[2]}, {1});
    face.SetFeature(TrackedFace::FeatureName::UpperLip, {landmarks[3], landmarks[4]}, {1, 1});
  }

  Result FaceTracker::Impl::SetFacePoseWithoutLandmarks(const s32 nrows, const s32 ncols,
                                                        TrackedFace& face, f32& intraEyeDist) const
  {
    auto const& rect = face.GetRect();
    DEV_ASSERT(rect.Area() > 0, "FaceTrackerImpl.SetFacePoseWithoutLandmarks.InvalidFaceRectangle");
    const Point2f leftEye( rect.GetXmid() - .25f*rect.GetWidth(),
                          rect.GetYmid() - .125f*rect.GetHeight() );
    const Point2f rightEye( rect.GetXmid() + .25f*rect.GetWidth(),
                           rect.GetYmid() - .125f*rect.GetHeight() );

    intraEyeDist = std::max((rightEye - leftEye).Length(), MinDistBetweenEyes_pixels);

    const CameraCalibration& scaledCalib = _camera.GetCalibration()->GetScaled(nrows, ncols);

    const Vec3f& T = GetTranslation(leftEye, rightEye, intraEyeDist, scaledCalib);
    Pose3d headPose = face.GetHeadPose();
    headPose.SetTranslation(T);
    headPose.SetParent(_camera.GetPose());
    face.SetHeadPose(headPose);

    Pose3d eyePose = face.GetEyePose();
    eyePose.SetTranslation(T);
    eyePose.SetParent(_camera.GetPose());
    face.SetEyePose(eyePose);

    face.SetIsFacingCamera(false);

    return RESULT_OK;
  }

  Result FaceTracker::Impl::SetFacePoseFromLandmarks(const s32 nrows, const s32 ncols,
                                                     const FaceLandmarks& landmarks,
                                                     TrackedFace& face, f32& intraEyeDist) const
  {
    intraEyeDist = 0.f;

    if(!ANKI_VERIFY(_camera.IsCalibrated(), "FaceTrackerImpl.SetFacePoseFromLandmarks.CameraNotCalibrated", ""))
    {
      return RESULT_FAIL;
    }

    auto const& calib = _camera.GetCalibration()->GetScaled(nrows, ncols);

    cv::Matx<f32,3,3> K = calib.GetCalibrationMatrix().get_CvMatx_();
    const std::vector<f32>& distCoeffs = calib.GetDistortionCoeffs();

    std::vector<cv::Point2f> distortedPoints;
    distortedPoints.reserve(kNumFaceLandmarks);
    for(auto const& landmark : landmarks)
    {
      distortedPoints.emplace_back(landmark.x(), landmark.y());
    }

    std::vector<cv::Point2f> undistortedPoints(distortedPoints);
    if(kUseUndistortionForFacePose)
    {
      try
      {
        cv::undistortPoints(distortedPoints, undistortedPoints, K, distCoeffs, cv::noArray(), K);
      }
      catch(const cv::Exception& e)
      {
        LOG_ERROR("FaceTrackerImpl.SetFacePoseFromLandmarks.UndistortFailed",
                  "OpenCV Error: %s", e.what());
        return RESULT_FAIL;
      }
    }

    std::vector<cv::Point3f> modelPoints;
    modelPoints.reserve(kNumFaceLandmarks);
    for(s32 i = 0; i < kNumFaceLandmarks; ++i)
    {
      modelPoints.emplace_back(kFaceModel_mm[i][0], kFaceModel_mm[i][1], kFaceModel_mm[i][2]);
    }

    cv::Vec3d rotationVector, translationVector;
    bool solved = false;
    try
    {
      solved = cv::solvePnP(modelPoints, undistortedPoints, K, cv::noArray(),
                            rotationVector, translationVector, false, cv::SOLVEPNP_EPNP);
    }
    catch(const cv::Exception& e)
    {
      LOG_WARNING("FaceTrackerImpl.SetFacePoseFromLandmarks.SolvePnPFailed",
                  "OpenCV Error: %s", e.what());
      solved = false;
    }

    Radians roll_rad(0.f), pitch_rad(0.f), yaw_rad(0.f);
    if(solved)
    {
      cv::Matx33d R;
      cv::Rodrigues(rotationVector, R);

      const f64 sinYaw = std::min(1., std::max(-1., R(0,2)));
      yaw_rad   = (f32)std::asin(sinYaw);
      pitch_rad = (f32)-std::atan2(-R(1,2), R(2,2));
      roll_rad  = (f32)std::atan2(-R(0,1), R(0,0));
    }

    face.SetHeadOrientation(roll_rad, pitch_rad, yaw_rad);

    const f32 maxAngle_deg = FaceEnrollParams::kLookingStraightMaxAngle_deg;
    face.SetIsFacingCamera(solved &&
                           std::abs(roll_rad.getDegrees())  <= maxAngle_deg &&
                           std::abs(pitch_rad.getDegrees()) <= maxAngle_deg &&
                           std::abs(yaw_rad.getDegrees())   <= maxAngle_deg);

    if(_detectGaze)
    {
      face.SetGaze(yaw_rad.getDegrees(), pitch_rad.getDegrees());
      face.SetBlinkAmount(0.f, 0.f);
    }

    const Point2f leftEye(undistortedPoints[0].x, undistortedPoints[0].y);
    const Point2f rightEye(undistortedPoints[1].x, undistortedPoints[1].y);
    intraEyeDist = std::max((leftEye-rightEye).Length(), MinDistBetweenEyes_pixels);

    if(kAdjustEyeDistByYaw)
    {
      const f32 yawAdjFrac = std::cos(face.GetHeadYaw().ToFloat());

      if(!Util::IsNearZero(yawAdjFrac))
      {
        intraEyeDist /= yawAdjFrac;
      }
    }

    const Vec3f& T = GetTranslation(leftEye, rightEye, intraEyeDist, calib);
    Pose3d headPose = face.GetHeadPose();
    headPose.SetTranslation(T);

    const RotationMatrix3d faceRotation(-face.GetHeadPitch(), face.GetHeadRoll(), face.GetHeadYaw());
    headPose.SetRotation(headPose.GetRotation() * faceRotation);

    headPose.SetParent(_camera.GetPose());
    face.SetHeadPose(headPose);

    Pose3d eyePose = face.GetEyePose();
    eyePose.SetTranslation(T);
    const Gaze& gaze = face.GetGaze();
    Radians upDown_rad(DEG_TO_RAD(gaze.upDown_deg));
    Radians leftRight_rad(DEG_TO_RAD(gaze.leftRight_deg));
    const RotationMatrix3d eyeRotation(-upDown_rad, 0.f, leftRight_rad);
    eyePose.SetRotation(eyePose.GetRotation() * eyeRotation);
    eyePose.SetParent(_camera.GetPose());
    face.SetEyePose(eyePose);

    return RESULT_OK;
  }

  bool FaceTracker::Impl::DetectEyeContact(const TrackedFace& face,
                                           const TimeStamp_t& timeStamp)
  {
    DEV_ASSERT(face.IsTranslationSet(), "FaceTrackerImpl.DetectEyeContact.FaceTranslationNotSet");
    auto& entry = _facesEyeContact[face.GetID()];
    entry.Update(face, timeStamp);

    bool eyeContact = false;
    if (entry.GetExpired(timeStamp))
    {
      _facesEyeContact.erase(face.GetID());
    }
    else
    {
      eyeContact = entry.IsMakingEyeContact();
    }
    return eyeContact;
  }

  bool FaceTracker::Impl::IsEnrollable(const FaceDetection& detection, const TrackedFace& face,
                                       const f32 intraEyeDist) const
  {
    return (detection.score >= FaceEnrollParams::kMinDetectionScoreForEnrollment &&
            face.IsFacingCamera() &&
            intraEyeDist >= FaceEnrollParams::kFarDistanceBetweenEyesMin);
  }

  Result FaceTracker::Impl::Update(const Vision::Image& frameOrig,
                                   const float cropFactor,
                                   std::list<TrackedFace>& faces,
                                   std::list<UpdatedFaceID>& updatedIDs,
                                   DebugImageList<CompressedImage>& debugImages)
  {
    if(!_isInitialized) {
      LOG_ERROR("FaceTrackerImpl.Update.NotInitialized", "");
      return RESULT_FAIL;
    }

    DEV_ASSERT(frameOrig.IsContinuous(), "FaceTrackerImpl.Update.NonContinuousImage");

    const s32 nWidth  = frameOrig.GetNumCols();
    const s32 nHeight = frameOrig.GetNumRows();
    const TimeStamp_t timestamp = frameOrig.GetTimestamp();

    std::vector<FaceDetection> detections;
    Tic("FaceDetect");
    const Result detectResult = _detector.Detect(frameOrig, cropFactor,
                                                 DetectParams::kFaceDetectionThreshold, detections);
    Toc("FaceDetect");

    if(RESULT_OK != detectResult) {
      LOG_WARNING("FaceTrackerImpl.Update.DetectFailed", "");
      return RESULT_FAIL;
    }

    PruneStaleTracks(timestamp);

    std::vector<TrackingID_t> trackingIDs;
    std::vector<bool> isBeingTracked;
    std::vector<bool> doRecognize;
    std::vector<bool> isVerifyOnly;

    for(auto iter = detections.begin(); iter != detections.end(); )
    {
      if(iter->rect.GetWidth() < DetectParams::kMinFaceSize ||
         iter->rect.GetHeight() < DetectParams::kMinFaceSize ||
         (s32)trackingIDs.size() >= DetectParams::kMaxDetectedFaces)
      {
        iter = detections.erase(iter);
        continue;
      }

      bool tracked = false;
      const TrackingID_t trackingID = AssociateWithTrack(iter->rect, timestamp, tracked);
      trackingIDs.push_back(trackingID);
      isBeingTracked.push_back(tracked);

      const bool isKnown = _recognizer.HasName(trackingID);
      const bool isEnrollmentTrackID = (_recognizer.GetEnrollmentTrackID() == trackingID);
      const bool verifyOnly = isKnown && !isEnrollmentTrackID;

      bool recognize = true;
      if(verifyOnly)
      {
        const Track* track = FindTrack(trackingID);
        const TimeStamp_t sinceRecognized = ((track != nullptr && timestamp > track->lastRecognizedTime) ?
                                             (timestamp - track->lastRecognizedTime) : 0);
        recognize = (sinceRecognized >= (TimeStamp_t)DetectParams::kNamedTrackReverify_ms);
      }

      doRecognize.push_back(recognize);
      isVerifyOnly.push_back(verifyOnly);

      ++iter;
    }

    for(size_t iDetection = 0; iDetection < detections.size(); ++iDetection)
    {
      const FaceDetection& detection = detections[iDetection];
      const TrackingID_t trackingID  = trackingIDs[iDetection];

      if (HaveAllowedTrackedFaces())
      {
        FaceID_t faceID;
        if (_recognizer.GetFaceIDFromTrackingID(trackingID, faceID))
        {
          if (_allowedTrackedFaceID.count(faceID) == 0)
          {
            continue;
          }
        }
      }

      faces.emplace_back();
      TrackedFace& face = faces.back();

      face.SetIsBeingTracked(isBeingTracked[iDetection]);
      face.SetRect(Rectangle<f32>(detection.rect));
      face.SetTimeStamp(timestamp);

      const bool isVerification = isVerifyOnly[iDetection];
      const bool doRecognition = _isRecognitionEnabled && doRecognize[iDetection];

      FaceLandmarks landmarks = detection.landmarks;
      if(DetectParams::kRefineLandmarks)
      {
        Tic("RefineLandmarks");
        _detector.Refine(frameOrig, detection.rect,
                         DetectParams::kRefineDetectionThreshold, landmarks);
        Toc("RefineLandmarks");
      }

      SetFaceFeatures(landmarks, face);

      f32 intraEyeDist = -1.f;
      if(RESULT_OK != SetFacePoseFromLandmarks(nHeight, nWidth, landmarks, face, intraEyeDist))
      {
        SetFacePoseWithoutLandmarks(nHeight, nWidth, face, intraEyeDist);
      }
      else if(_detectGaze)
      {
        face.SetEyeContact(DetectEyeContact(face, timestamp));
      }

      if(doRecognition)
      {
        const bool enrollable = IsEnrollable(detection, face, intraEyeDist);
        bool enableEnrollment = enrollable && !isVerification;

        if(enableEnrollment && HaveAllowedTrackedFaces())
        {
          FaceID_t faceID;
          if (_recognizer.GetFaceIDFromTrackingID(trackingID, faceID))
          {
            enableEnrollment &= (faceID == _recognizer.GetEnrollmentID());
          }
        }

        const bool accepted = _recognizer.SetNextFaceToRecognize(frameOrig, trackingID, detection.rect,
                                                                 landmarks, enableEnrollment, isVerification);
        if(accepted)
        {
          Track* track = FindTrack(trackingID);
          if(nullptr != track)
          {
            track->lastRecognizedTime = timestamp;
          }
        }
      }

      s32 enrollmentCompleted = 0;
      auto recognitionData = _recognizer.GetRecognitionData(trackingID, enrollmentCompleted, debugImages);

      face.SetBestGuessName(_recognizer.GetBestGuessNameForTrackingID(trackingID));

      if(recognitionData.WasFaceIDJustUpdated())
      {
        UpdatedFaceID update{
          .oldID   = (recognitionData.GetPreviousFaceID() == UnknownFaceID ?
                      -trackingID : recognitionData.GetPreviousFaceID()),
          .newID   = recognitionData.GetFaceID(),
          .newName = recognitionData.GetName()
        };

        if(_allowedTrackedFaceID.count(update.oldID)>0)
        {
          LOG_DEBUG("FaceTrackerImpl.Update.UpdatingAllowedTrackedFaceIDs",
                    "Remove %d, Add %d", update.oldID, update.newID);
          _allowedTrackedFaceID.erase(update.oldID);
          _allowedTrackedFaceID.insert(update.newID);
        }

        updatedIDs.push_back(std::move(update));
      }

      if(recognitionData.GetFaceID() != UnknownFaceID &&
         recognitionData.GetTrackingID() != recognitionData.GetPreviousTrackingID())
      {
        UpdatedFaceID update{
          .oldID   = -recognitionData.GetTrackingID(),
          .newID   = recognitionData.GetFaceID(),
          .newName = recognitionData.GetName()
        };

        if(updatedIDs.empty() ||
           (update.oldID != updatedIDs.back().oldID &&
            update.newID != updatedIDs.back().newID))
        {
          updatedIDs.push_back(std::move(update));
        }
      }

      face.SetScore(recognitionData.GetScore());
      if(UnknownFaceID == recognitionData.GetFaceID()) {
        DEV_ASSERT(trackingID > 0, "FaceTrackerImpl.Update.InvalidTrackerID");
        face.SetID(-trackingID);
      } else {
        face.SetID(recognitionData.GetFaceID());
        face.SetName(recognitionData.GetName());
        face.SetNumEnrollments(enrollmentCompleted);

        face.SetRecognitionDebugInfo(recognitionData.GetDebugMatchingInfo());
      }
    }

    return RESULT_OK;
  }

  bool FaceTracker::Impl::CanAddNamedFace() const
  {
    return _recognizer.CanAddNamedFace();
  }

  Result FaceTracker::Impl::AssignNameToID(FaceID_t faceID, const std::string& name, FaceID_t mergeWithID)
  {
    return _recognizer.AssignNameToID(faceID, name, mergeWithID);
  }

  Result FaceTracker::Impl::EraseFace(FaceID_t faceID)
  {
    return _recognizer.EraseFace(faceID);
  }

  void FaceTracker::Impl::EraseAllFaces()
  {
    _recognizer.EraseAllFaces();
  }

  Result FaceTracker::Impl::SaveAlbum(const std::string& albumName)
  {
    return _recognizer.SaveAlbum(albumName);
  }

  Result FaceTracker::Impl::RenameFace(FaceID_t faceID, const std::string& oldName, const std::string& newName,
                                       Vision::RobotRenamedEnrolledFace& renamedFace)
  {
    return _recognizer.RenameFace(faceID, oldName, newName, renamedFace);
  }

  Result FaceTracker::Impl::LoadAlbum(const std::string& albumName, std::list<LoadedKnownFace>& loadedFaces)
  {
    if(!_isInitialized) {
      LOG_ERROR("FaceTrackerImpl.LoadAlbum.NotInitialized", "");
      return RESULT_FAIL;
    }

    return _recognizer.LoadAlbum(albumName, loadedFaces);
  }

  std::vector<LoadedKnownFace> FaceTracker::Impl::GetEnrolledNames() const
  {
    return _recognizer.GetEnrolledNames();
  }

  float FaceTracker::Impl::GetMinEyeDistanceForEnrollment()
  {
    return FaceEnrollParams::kFarDistanceBetweenEyesMin;
  }

  void FaceTracker::Impl::SetFaceEnrollmentMode(Vision::FaceID_t forFaceID,
                                                s32 numEnrollments,
                                                bool forceNewID)
  {
    _recognizer.SetAllowedEnrollments(numEnrollments, forFaceID, forceNewID);
  }

  Result FaceTracker::Impl::GetSerializedData(std::vector<u8>& albumData,
                                              std::vector<u8>& enrollData)
  {
    return _recognizer.GetSerializedData(albumData, enrollData);
  }

  Result FaceTracker::Impl::SetSerializedData(const std::vector<u8>& albumData,
                                              const std::vector<u8>& enrollData,
                                              std::list<LoadedKnownFace>& loadedFaces)
  {
    return _recognizer.SetSerializedData(albumData, enrollData, loadedFaces);
  }

#if ANKI_DEVELOPER_CODE
  Result FaceTracker::Impl::DevAddFaceToAlbum(const Image& img, const TrackedFace& face, int albumEntry)
  {
    return _recognizer.DevAddFaceToAlbum(img, face, albumEntry);
  }

  Result FaceTracker::Impl::DevFindFaceInAlbum(const Image& img, const TrackedFace& face,
                                               int& albumEntry, float& score) const
  {
    return _recognizer.DevFindFaceInAlbum(img, face, albumEntry, score);
  }

  Result FaceTracker::Impl::DevFindFaceInAlbum(const Image& img, const TrackedFace& face, const int maxMatches,
                                               std::vector<std::pair<int, float>>& matches) const
  {
    return _recognizer.DevFindFaceInAlbum(img, face, maxMatches, matches);
  }

  float FaceTracker::Impl::DevComputePairwiseMatchScore(int faceID1, int faceID2) const
  {
    return _recognizer.DevComputePairwiseMatchScore(faceID1, faceID2);
  }

  float FaceTracker::Impl::DevComputePairwiseMatchScore(int faceID1, const Image& img2, const TrackedFace& face2) const
  {
    return _recognizer.DevComputePairwiseMatchScore(faceID1, img2, face2);
  }
#endif

#if ANKI_DEV_CHEATS
  void FaceTracker::Impl::SaveAllRecognitionImages(const std::string& imagePathPrefix)
  {
    _recognizer.SaveAllRecognitionImages(imagePathPrefix);
  }

  void FaceTracker::Impl::DeleteAllRecognitionImages()
  {
    _recognizer.DeleteAllRecognitionImages();
  }
#endif

}
}

#endif
