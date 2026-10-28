#include "cannedAnimLib/proceduralFace/proceduralFaceDrawer.h"
#include "cannedAnimLib/proceduralFace/scanlineDistorter.h"

#include "coretech/common/shared/array2d.h"
#include "coretech/common/shared/math/matrix.h"
#include "coretech/common/engine/math/quad.h"
#include "coretech/common/shared/math/rotation.h"

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "util/console/consoleInterface.h"
#include "util/cpuProfiler/cpuProfiler.h"
#include "util/math/math.h"
#include "util/math/numericCast.h"
#include "util/random/randomGenerator.h"
#include <mutex>

// switch std::round() to macro for easier change
// switched from (x) to std::round(x) as it was noticed edges were jittering when
// the roundness of corners was animated (VIC-3930). Keeping as a macro in-case
// there's a faster way to round()/cast().
#define ROUND(x) std::round(x)

// switch Util::numeric_cast_clamped() to macro for easier change
#define NUMERIC_CAST_CLAMPED(x) (u8)(x) // Util::numeric_cast_clamped<u8>(x)

namespace Anki {
namespace Vector {

  static std::mutex gCustomEyeMtx;

  #define CONSOLE_GROUP "Face.ParameterizedFace"

  enum class Filter {
    None           = 0,
    BoxFilter      = 1,
    GaussianFilter = 2,
  };

  enum class EyeShape : u8 {
    Rounded = 0,
    Heptagon = 1,
  };

  // Clockwise, in eye-local coordinates. A flat top, clipped upper corners,
  // vertical sides and a pointed base give the outline seven edges. Deeper
  // corner cuts keep the facets visible after smoothing at display resolution.
  static const std::array<cv::Point2f, 7> kHeptagonOutline{{
    { 0.45f, -1.f },
    { 1.f, -0.4f },
    { 1.f,  0.5f },
    { 0.f,  1.f },
    {-1.f,  0.5f },
    {-1.f, -0.4f },
    {-0.45f, -1.f },
  }};

  CONSOLE_VAR_ENUM(u8,      kProcFace_EyeShape,                   CONSOLE_GROUP, static_cast<u8>(EyeShape::Heptagon), "Rounded,Heptagon");
  CONSOLE_VAR_ENUM(u8,      kProcFace_LineType,                   CONSOLE_GROUP, IsXray() ? 2 : 1, "Line_4,Line_8,Line_AA"); // Only affects OpenCV drawing, not post-smoothing
  CONSOLE_VAR_ENUM(u8,      kProcFace_InterpolationType,          CONSOLE_GROUP, 1, "Nearest,Linear,Cubic,Area,Lanczos,LinearExact,Max,WarpFillOutliers");
  CONSOLE_VAR_RANGED(s32,   kProcFace_EllipseDelta,               CONSOLE_GROUP, IsXray() ? 15 : 10, 1, 90);
  CONSOLE_VAR_RANGED(f32,   kProcFace_EyeLightnessMultiplier,     CONSOLE_GROUP, 1.f, 0.f, 2.f);

  CONSOLE_VAR(bool,         kProcFace_HotspotRender,              CONSOLE_GROUP, true); // Render glow
  CONSOLE_VAR_RANGED(f32,   kProcFace_HotspotFalloff,             CONSOLE_GROUP, 0.48f, 0.05f, 1.f);
 
  CONSOLE_VAR(bool,         kProcFace_EnableAntiAliasing,         CONSOLE_GROUP, true);
  CONSOLE_VAR_RANGED(s32,   kProcFace_AntiAliasingSize,           CONSOLE_GROUP, IsXray() ? 2 : 3, 0, 15); // full image antialiasing (3 will use NEON)
  CONSOLE_VAR_ENUM(uint8_t, kProcFace_AntiAliasingFilter,         CONSOLE_GROUP, (uint8_t)Filter::BoxFilter, "None,Box,Gaussian");
  CONSOLE_VAR_RANGED(f32,   kProcFace_AntiAliasingSigmaFraction,  CONSOLE_GROUP, IsXray() ? 0.3f : 0.5f, 0.0f, 1.0f);
  CONSOLE_VAR(bool, kProcFace_CustomEyes, CONSOLE_GROUP, false);
  CONSOLE_VAR_RANGED(f32, kProcFace_CustomEyeOpacity, CONSOLE_GROUP, 0.8f, 0.f, 1.f);
  CONSOLE_VAR_ENUM(u8, kProcFace_FlavorOfGay, CONSOLE_GROUP, 0, "Lesbian,Gay,Bi,Trans,Pan,Frog,All,Galaxy,Custom");
  static void LOOK_LoadFaceOverlay(ConsoleFunctionContextRef context)
  {
    ProceduralFaceDrawer::LoadCustomEyePNG();
  }
  CONSOLE_FUNC(LOOK_LoadFaceOverlay, CONSOLE_GROUP);


#if PROCEDURALFACE_GLOW_FEATURE
  CONSOLE_VAR_RANGED(f32, kProcFace_GlowSizeMultiplier,           CONSOLE_GROUP, 1.f, 0.f, 1.f);
  CONSOLE_VAR_RANGED(f32, kProcFace_GlowLightnessMultiplier,      CONSOLE_GROUP, 1.f, 0.f, 10.f);
  CONSOLE_VAR_ENUM(uint8_t, kProcFace_GlowFilter,                 CONSOLE_GROUP, (uint8_t)Filter::BoxFilter, "None,Box,Gaussian,Box (NEON code; size 3)");
#endif

#if PROCEDURALFACE_SCANLINE_FEATURE
  CONSOLE_VAR(bool,                         kProcFace_Scanlines,              CONSOLE_GROUP, false);
  CONSOLE_VAR_RANGED(ProceduralFace::Value, kProcFace_DefaultScanlineOpacity, CONSOLE_GROUP, 1.f, 0.f, 1.f);
#endif

#if PROCEDURALFACE_NOISE_FEATURE
  static const s32 kNumNoiseImages = 7;

  CONSOLE_VAR_RANGED(s32, kProcFace_NoiseNumFrames,              CONSOLE_GROUP, 5, 0, kNumNoiseImages);
  CONSOLE_VAR_RANGED(f32, kProcFace_NoiseMinLightness,           CONSOLE_GROUP, IsXray() ? 0.98f : 0.92f, 0.f, 2.f); // replaces kProcFace_NoiseFraction
  CONSOLE_VAR_RANGED(f32, kProcFace_NoiseMaxLightness,           CONSOLE_GROUP, IsXray() ? 1.05f : 1.14f, 0.f, 2.f);

  CONSOLE_VAR_EXTERN(s32, kProcFace_NoiseNumFrames);
#else
  static const s32 kProcFace_NoiseNumFrames = 0;
#endif

  #undef CONSOLE_GROUP

  // Initialize static vars

#if PROCEDURALFACE_GLOW_FEATURE
  Vision::Image ProceduralFaceDrawer::_glowImg;
#endif
  Vision::Image ProceduralFaceDrawer::_eyeShape; // temporary working surface, I8

  struct ProceduralFaceDrawer::FaceCache ProceduralFaceDrawer::_faceCache;

  Rectangle<f32> ProceduralFaceDrawer::_leftBBox;
  Rectangle<f32> ProceduralFaceDrawer::_rightBBox;
  s32 ProceduralFaceDrawer::_faceColMin;
  s32 ProceduralFaceDrawer::_faceColMax;
  s32 ProceduralFaceDrawer::_faceRowMin;
  s32 ProceduralFaceDrawer::_faceRowMax;

  Vision::ImageRGB565 ProceduralFaceDrawer::_customEyeOverlay;
  bool ProceduralFaceDrawer::_hasCustomEyes = false;
  Vision::Image ProceduralFaceDrawer::_customEyeAlpha;

  Matrix_3x3f ProceduralFaceDrawer::GetTransformationMatrix(f32 angleDeg, f32 scaleX, f32 scaleY,
                                                            f32 tX, f32 tY, f32 x0, f32 y0)
  {
    //
    // Create a 2x3 warp matrix which incorporates scale, rotation, and translation
    //    W = R * [scale_x    0   ] * [x - x0] + [x0] + [tx]
    //            [   0    scale_y]   [y - y0] + [y0] + [ty]
    //
    // So a given point gets scaled (first!) and then rotated around the given center
    // (x0,y0)and then translated by (tx,ty).
    //
    // Note: can't use cv::getRotationMatrix2D, b/c it only incorporates one
    // scale factor, not separate scaling in x and y. Otherwise, this is
    // exactly the same thing
    //
    f32 cosAngle = 1, sinAngle = 0;
    if(angleDeg != 0) {
      const f32 angleRad = DEG_TO_RAD(angleDeg);
      cosAngle = std::cos(angleRad);
      sinAngle = std::sin(angleRad);
    }

    const f32 alpha_x = scaleX * cosAngle;
    const f32 beta_x  = scaleX * sinAngle;
    const f32 alpha_y = scaleY * cosAngle;
    const f32 beta_y  = scaleY * sinAngle;

    Matrix_3x3f W{
      alpha_x, beta_y,  (1.f - alpha_x)*x0 - beta_y*y0 + tX,
      -beta_x, alpha_y, beta_x*x0 + (1.f - alpha_y)*y0 + tY,
      0, 0, 1
    };

    return W;
  } // GetTransformationMatrix()


  // Based on Taylor series expansion with N terms
  inline static f32 fastExp(f32 x)
  {
#   define NUM_FAST_EXP_TERMS 2

    if(x < -(f32)(2*NUM_FAST_EXP_TERMS))
    {
      // Things get numerically unstable for very negative inputs x. Value is basically zero anyway.
      return 0.f;
    }
#   if NUM_FAST_EXP_TERMS==2
    x = 1.f + (x * 0.25f);  // Constant here is 1/(2^N)
    x *= x; x *= x;         // Number of multiplies here is also N
#   elif NUM_FAST_EXP_TERMS==3
    x = 1.f + (x * 0.125f); // Constant here is 1/(2^N)
    x *= x; x *= x; x *= x; // Number of multiplies here is also N
#   else
#   error Unsupported number of terms for fastExp()
#   endif

    return x;

#   undef NUM_FAST_EXP_TERMS
  }

#if PROCEDURALFACE_NOISE_FEATURE
  inline Array2d<u8> CreateNoiseImage(const Util::RandomGenerator& rng)
  {
    Array2d<u8> noiseImg(FACE_DISPLAY_HEIGHT, FACE_DISPLAY_WIDTH);
    u8* noiseImg_i = noiseImg.GetRow(0);
    for(s32 j=0; j<noiseImg.GetNumElements(); ++j)
    {
      // Storing noise in the range [0 255] instead of [0 2.0] both to reduce memory usage as well
      // as improving NEON performance by not having to convert between float and u8 
      noiseImg_i[j] = Util::numeric_cast_clamped<u8>(rng.RandDblInRange(kProcFace_NoiseMinLightness,
                                                                        kProcFace_NoiseMaxLightness) * 128);
    }
    return noiseImg;
  }

  const Array2d<u8>& ProceduralFaceDrawer::GetNoiseImage(const Util::RandomGenerator& rng)
  {
    // NOTE: Since this is called separately for each eye, this looks better if we use an odd number of images
    static_assert(kNumNoiseImages % 2 == 1, "Use odd number of noise images");
    static std::array<Array2d<u8>, kNumNoiseImages> kNoiseImages{{
      CreateNoiseImage(rng),
      CreateNoiseImage(rng),
      CreateNoiseImage(rng),
      CreateNoiseImage(rng),
      CreateNoiseImage(rng),
      CreateNoiseImage(rng),
      CreateNoiseImage(rng),
    }};

  #if REMOTE_CONSOLE_ENABLED
    kProcFace_NoiseMinLightness = Anki::Util::Clamp(kProcFace_NoiseMinLightness, 0.f, kProcFace_NoiseMaxLightness);
    kProcFace_NoiseMaxLightness = Anki::Util::Clamp(kProcFace_NoiseMaxLightness, kProcFace_NoiseMinLightness, 2.f);
  #endif // REMOTE_CONSOLE_ENABLED

    static f32 kProcFace_NoiseMinLightness_old = kProcFace_NoiseMinLightness;
    static f32 kProcFace_NoiseMaxLightness_old = kProcFace_NoiseMaxLightness;
    if(kProcFace_NoiseMinLightness_old != kProcFace_NoiseMinLightness || kProcFace_NoiseMaxLightness_old != kProcFace_NoiseMaxLightness) {
      for(auto& currentNoiseImage : kNoiseImages) {
        Array2d<u8> noiseImg = CreateNoiseImage(rng);
        std::swap(currentNoiseImage, noiseImg);
      }

      kProcFace_NoiseMinLightness_old = kProcFace_NoiseMinLightness;
      kProcFace_NoiseMaxLightness_old = kProcFace_NoiseMaxLightness;
    }

    if(kProcFace_NoiseNumFrames == 0) {
      return kNoiseImages[0];
    } else {
      // Cycle circularly through the set of noise images
      static s32 index = 0;
      index = (index + 1) % kProcFace_NoiseNumFrames;
      return kNoiseImages[index];
    }
  }
#endif // PROCEDURALFACE_NOISE_FEATURE

  void ProceduralFaceDrawer::ApplyAntiAliasing(Vision::Image& shape, float minX, float minY, float maxX, float maxY)
  {
    if(kProcFace_AntiAliasingSize > 0) {
      ANKI_CPU_PROFILE("AntiAliasing");

      Rectangle<s32> boundingBoxS32(minX, minY, maxX - minX + 1, maxY - minY + 1);
      Vision::Image shapeROI = shape.GetROI(boundingBoxS32);

      switch(kProcFace_AntiAliasingFilter) {
        case (uint8_t)Filter::BoxFilter:
        {
          static Vision::Image temp(shape.GetNumRows(), shape.GetNumCols());
          temp.FillWith(0);
          Vision::Image tempImage = temp.GetROI(boundingBoxS32);
          shapeROI.BoxFilter(tempImage, kProcFace_AntiAliasingSize);
          std::swap(shape, temp);
          break;
        }
        case (uint8_t)Filter::GaussianFilter:
        {
          cv::GaussianBlur(shape.get_CvMat_(),
                           shape.get_CvMat_(),
                           cv::Size(kProcFace_AntiAliasingSize,kProcFace_AntiAliasingSize),
                           (f32)kProcFace_AntiAliasingSize * kProcFace_AntiAliasingSigmaFraction);
          break;
        }
      }
    }
  }

void ProceduralFaceDrawer::LoadCustomEyePNG()
{
  std::lock_guard<std::mutex> lk(gCustomEyeMtx);
  _hasCustomEyes = false;
  static const cv::String kFaceOverlays[9] = { 
    "/anki/data/assets/cozmo_resources/assets/faceOverlays/lesbian.jpg", 
    "/anki/data/assets/cozmo_resources/assets/faceOverlays/gay.jpg",
    "/anki/data/assets/cozmo_resources/assets/faceOverlays/bi.jpg",
    "/anki/data/assets/cozmo_resources/assets/faceOverlays/trans.jpg",
    "/anki/data/assets/cozmo_resources/assets/faceOverlays/pan.jpg",
    "/anki/data/assets/cozmo_resources/assets/faceOverlays/frog.jpg",
    "/anki/data/assets/cozmo_resources/assets/faceOverlays/all.jpg",
    "/anki/data/assets/cozmo_resources/assets/faceOverlays/galaxy.jpg",
    "/data/data/customFaceOverlay.jpg"
  };
  const cv::String kFaceOverlay = kFaceOverlays[kProcFace_FlavorOfGay];
  cv::Mat img = cv::imread(kFaceOverlay, cv::IMREAD_UNCHANGED);
  if(img.empty()) {
    return;
  }

  // if given a 1920x1080 image, we don't want to scale that upon every face draw
  if (img.cols != FACE_DISPLAY_WIDTH || img.rows > FACE_DISPLAY_HEIGHT) {
    cv::resize(img, img, cv::Size(FACE_DISPLAY_WIDTH, FACE_DISPLAY_HEIGHT), 0, 0, cv::INTER_AREA);
  }

  bool hasAlpha = (img.channels() == 4);
  if(!hasAlpha && img.channels() != 3) {
    // what silly image is this
    return;
  }

  _customEyeOverlay.Allocate(img.rows, img.cols);
  _customEyeAlpha  .Allocate(img.rows, img.cols);

  for(int r=0; r<img.rows; ++r)
  {
    const cv::Vec4b* src4 = hasAlpha ? img.ptr<cv::Vec4b>(r) : nullptr;
    const cv::Vec3b* src3 = !hasAlpha? img.ptr<cv::Vec3b>(r) : nullptr;
    auto* dstPix = _customEyeOverlay.GetRow(r);
    u8*   dstA   = _customEyeAlpha .GetRow(r);

    for(int c=0; c<img.cols; ++c)
    {
      u8 b,g,r8,a;
      if(hasAlpha) { 
        b=src4[c][0]; 
        g=src4[c][1]; 
        r8=src4[c][2]; 
        a=src4[c][3]; 
      } else { 
        b=src3[c][0]; 
        g=src3[c][1]; 
        r8=src3[c][2]; 
        a = Util::Clamp<u8>(kProcFace_CustomEyeOpacity*255.f,0,255); 
      }

      dstPix[c] = { 
        static_cast<u16>( ((r8 & 0xF8)<<8) | ((g & 0xFC)<<3) | (b>>3) ) 
      };
      dstA[c] = a;
    }
  }
  _hasCustomEyes = true;
}


  void ProceduralFaceDrawer::DrawEye(const ProceduralFace& faceData, WhichEye whichEye, const Matrix_3x3f* W_facePtr,
                                     Vision::Image& faceImg, Rectangle<f32>& eyeBoundingBox)
  {
    const s32 eyeWidth  = ProceduralFace::NominalEyeWidth;
    const s32 eyeHeight = ProceduralFace::NominalEyeHeight;
    const f32 halfEyeWidth  = 0.5f*eyeWidth;
    const f32 halfEyeHeight = 0.5f*eyeHeight;

    // Left/right here will be in terms of the left eye. We will mirror to get
    // the right eye. So
    const f32 upLeftRadX  = faceData.GetParameter(whichEye, Parameter::UpperOuterRadiusX)*halfEyeWidth;
    const f32 upLeftRadY  = faceData.GetParameter(whichEye, Parameter::UpperOuterRadiusY)*halfEyeHeight;
    const f32 lowLeftRadX = faceData.GetParameter(whichEye, Parameter::LowerOuterRadiusX)*halfEyeWidth;
    const f32 lowLeftRadY = faceData.GetParameter(whichEye, Parameter::LowerOuterRadiusY)*halfEyeHeight;

    const f32 upRightRadX  = faceData.GetParameter(whichEye, Parameter::UpperInnerRadiusX)*halfEyeWidth;
    const f32 upRightRadY  = faceData.GetParameter(whichEye, Parameter::UpperInnerRadiusY)*halfEyeHeight;
    const f32 lowRightRadX = faceData.GetParameter(whichEye, Parameter::LowerInnerRadiusX)*halfEyeWidth;
    const f32 lowRightRadY = faceData.GetParameter(whichEye, Parameter::LowerInnerRadiusY)*halfEyeHeight;

    //
    // Compute eye and lid polygons:
    //
    std::vector<cv::Point> eyePoly, segment, lowerLidPoly, upperLidPoly;
    static const s32 kLineTypes[3] = { cv::LINE_4, cv::LINE_8, cv::LINE_AA };
    const s32 kLineType = kLineTypes[kProcFace_LineType];

    // 1. Eye shape poly
    if(kProcFace_EyeShape == static_cast<u8>(EyeShape::Heptagon)) {
      eyePoly.reserve(kHeptagonOutline.size());
      for(const auto& point : kHeptagonOutline) {
        eyePoly.emplace_back(ROUND(point.x * halfEyeWidth), ROUND(point.y * halfEyeHeight));
      }
    } else {
      //ANKI_CPU_PROFILE("EyeShapePoly");
      // Upper right corner
      if(upRightRadX > 0 && upRightRadY > 0) {
        cv::ellipse2Poly(cv::Point(ROUND(halfEyeWidth - upRightRadX), ROUND(-halfEyeHeight + upRightRadY)),
                         cv::Size(upRightRadX,upRightRadY), 0, 270, 360, kProcFace_EllipseDelta, segment);
        eyePoly.insert(eyePoly.end(), segment.begin(), segment.end());
      } else {
        eyePoly.emplace_back(halfEyeWidth,-halfEyeHeight);
      }

      // Lower right corner
      if(lowRightRadX > 0 && lowRightRadY > 0) {
        cv::ellipse2Poly(cv::Point(ROUND(halfEyeWidth - lowRightRadX), ROUND(halfEyeHeight - lowRightRadY)),
                         cv::Size(lowRightRadX,lowRightRadY), 0, 0, 90, kProcFace_EllipseDelta, segment);
        eyePoly.insert(eyePoly.end(), segment.begin(), segment.end());
      } else {
        eyePoly.emplace_back(halfEyeWidth, halfEyeHeight);
      }

      // Lower left corner
      if(lowLeftRadX > 0 && lowLeftRadY > 0) {
        cv::ellipse2Poly(cv::Point(ROUND(-halfEyeWidth + lowLeftRadX), ROUND(halfEyeHeight - lowLeftRadY)),
                         cv::Size(lowLeftRadX,lowLeftRadY), 0, 90, 180, kProcFace_EllipseDelta, segment);
        eyePoly.insert(eyePoly.end(), segment.begin(), segment.end());
      } else {
        eyePoly.emplace_back(-halfEyeWidth, halfEyeHeight);
      }

      // Upper left corner
      if(upLeftRadX > 0 && upLeftRadY > 0) {
        cv::ellipse2Poly(cv::Point(ROUND(-halfEyeWidth + upLeftRadX), ROUND(-halfEyeHeight + upLeftRadY)),
                         cv::Size(upLeftRadX,upLeftRadY), 0, 180, 270, kProcFace_EllipseDelta, segment);
        eyePoly.insert(eyePoly.end(), segment.begin(), segment.end());
      } else {
        eyePoly.emplace_back(-halfEyeWidth,-halfEyeHeight);
      }
    }

    // 2. Lower lid poly
    {
      //ANKI_CPU_PROFILE("LowerLidPoly");
      const f32 lowerLidY = faceData.GetParameter(whichEye, Parameter::LowerLidY) * static_cast<f32>(eyeHeight);
      const f32 angleDeg = faceData.GetParameter(whichEye, Parameter::LowerLidAngle);
      const f32 angleRad = DEG_TO_RAD(angleDeg);
      const f32 yAngleAdj = -halfEyeWidth * std::tan(angleRad);
      lowerLidPoly = {
        {(s32)ROUND( halfEyeWidth + 1.f), (s32)ROUND(halfEyeHeight - lowerLidY - yAngleAdj)}, // Upper right corner
        {(s32)ROUND( halfEyeWidth + 1.f), (s32)ROUND(halfEyeHeight + 1.f)}, // Lower right corner
        {(s32)ROUND(-halfEyeWidth - 1.f), (s32)ROUND(halfEyeHeight + 1.f)}, // Lower left corner
        {(s32)ROUND(-halfEyeWidth - 1.f), (s32)ROUND(halfEyeHeight - lowerLidY + yAngleAdj)}, // Upper left corner
      };
      // Add bend:
      const f32 yRad = faceData.GetParameter(whichEye, Parameter::LowerLidBend) * static_cast<f32>(eyeHeight);
      if(yRad != 0) {
        const f32 xRad = ROUND(halfEyeWidth / std::cos(angleRad));
        cv::ellipse2Poly(cv::Point(0, ROUND(halfEyeHeight - lowerLidY)),
                         cv::Size(xRad,yRad), angleDeg, 180, 360, kProcFace_EllipseDelta, segment);
        DEV_ASSERT(std::abs(segment.front().x - lowerLidPoly.back().x)<3 &&
                   std::abs(segment.front().y - lowerLidPoly.back().y)<3,
                   "First curved lower lid segment point not close to last lid poly point.");
        DEV_ASSERT(std::abs(segment.back().x - lowerLidPoly.front().x)<3 &&
                   std::abs(segment.back().y - lowerLidPoly.front().y)<3,
                   "Last curved lower lid segment point not close to first lid poly point.");
        lowerLidPoly.insert(lowerLidPoly.end(), segment.begin(), segment.end());
      }
    }

    // 3. Upper lid poly
    {
      //ANKI_CPU_PROFILE("UpperLidPoly");
      const f32 upperLidY = faceData.GetParameter(whichEye, Parameter::UpperLidY) * static_cast<f32>(eyeHeight);
      const f32 angleDeg = faceData.GetParameter(whichEye, Parameter::UpperLidAngle);
      const f32 angleRad = DEG_TO_RAD(angleDeg);
      const f32 yAngleAdj = -halfEyeWidth * std::tan(angleRad);
      upperLidPoly = {
        {(s32)ROUND(-halfEyeWidth - 1.f), (s32)ROUND(-halfEyeHeight + upperLidY + yAngleAdj)}, // Lower left corner
        {(s32)ROUND(-halfEyeWidth - 1.f), (s32)ROUND(-halfEyeHeight - 1.f)}, // Upper left corner
        {(s32)ROUND( halfEyeWidth + 1.f), (s32)ROUND(-halfEyeHeight - 1.f)}, // Upper right corner
        {(s32)ROUND( halfEyeWidth + 1.f), (s32)ROUND(-halfEyeHeight + upperLidY - yAngleAdj)}, // Lower right corner
      };
      // Add bend:
      const f32 yRad = faceData.GetParameter(whichEye, Parameter::UpperLidBend) * static_cast<f32>(eyeHeight);
      if(yRad != 0) {
        const f32 xRad = ROUND(halfEyeWidth / std::cos(angleRad));
        cv::ellipse2Poly(cv::Point(0, ROUND(-halfEyeHeight + upperLidY)),
                         cv::Size(xRad,yRad), angleDeg, 0, 180, kProcFace_EllipseDelta, segment);
        DEV_ASSERT(std::abs(segment.front().x - upperLidPoly.back().x)<3 &&
                   std::abs(segment.front().y - upperLidPoly.back().y)<3,
                   "First curved upper lid segment point not close to last lid poly point");
        DEV_ASSERT(std::abs(segment.back().x - upperLidPoly.front().x)<3 &&
                   std::abs(segment.back().y - upperLidPoly.front().y)<3,
                   "Last curved upper lid segment point not close to first lid poly point");
        upperLidPoly.insert(upperLidPoly.end(), segment.begin(), segment.end());
      }
    }

    ANKI_CPU_PROFILE_START(prof_getMat, "GetMatrices");
    Point<2, Value> eyeCenter = (whichEye == WhichEye::Left) ?
                                 Point<2, Value>(ProceduralFace::GetNominalLeftEyeX(), ProceduralFace::GetNominalEyeY()) :
                                 Point<2, Value>(ProceduralFace::GetNominalRightEyeX(), ProceduralFace::GetNominalEyeY());
    eyeCenter.x() += faceData.GetParameter(whichEye, Parameter::EyeCenterX);
    eyeCenter.y() += faceData.GetParameter(whichEye, Parameter::EyeCenterY);

    // Apply rotation, translation, and scaling to the eye and lid polygons.
    // This warp is a combination of the eye-specific parameters and full-face parameters
    auto W = GetTransformationMatrix(faceData.GetParameter(whichEye, Parameter::EyeAngle),
                                     faceData.GetParameter(whichEye, Parameter::EyeScaleX),
                                     faceData.GetParameter(whichEye, Parameter::EyeScaleY),
                                     eyeCenter.x(),
                                     eyeCenter.y());
    
    if(W_facePtr != nullptr)
    {
      // Compose full-face warp with eye-only warp
      W = (*W_facePtr) * W;
      
      // Update eye center now that full-face warp has been composed in
      eyeCenter.x() = W(0,2);
      eyeCenter.y() = W(1,2);
    }
    
#if PROCEDURALFACE_GLOW_FEATURE
    const Value glowFraction = Util::Min(1.f, Util::Max(-1.f, kProcFace_GlowSizeMultiplier * faceData.GetParameter(whichEye, Parameter::GlowSize)));
    const SmallMatrix<2, 3, f32> W_glow = GetTransformationMatrix(faceData.GetParameter(whichEye, Parameter::EyeAngle),
                                                                  (1+glowFraction) * faceData.GetParameter(whichEye, Parameter::EyeScaleX),
                                                                  (1+glowFraction) * faceData.GetParameter(whichEye, Parameter::EyeScaleY),
                                                                  eyeCenter.x(),
                                                                  eyeCenter.y());
#endif
    //ANKI_CPU_PROFILE_STOP(prof_getMat);

    // Initialize bounding box corners at their opposite extremes. We will figure out their
    // true locations as we loop over the eyePoly below.
    Point2f upperLeft(ProceduralFace::WIDTH, ProceduralFace::HEIGHT);
    Point2f bottomRight(0.f,0.f);

    ANKI_CPU_PROFILE_START(prof_applyMat, "ApplyMatrices");
    // Warp the poly and the glow. Use the warped glow (which is a larger shape) to compute
    // the overall eye bounding box
    for(auto & point : eyePoly)
    {
      const Point<3,f32> pointF32{
        static_cast<f32>(whichEye == WhichEye::Left ? point.x : -point.x), static_cast<f32>(point.y), 1.f
      };

      Point<2,f32> temp = W * pointF32;
      point.x = ROUND(temp.x());
      point.y = ROUND(temp.y());

#if PROCEDURALFACE_GLOW_FEATURE
      // Use glow warp to figure out larger bounding box which contains glow as well
      temp = W_glow * pointF32;
#endif
      upperLeft.x()   = std::min(upperLeft.x(),   (f32)std::floor(temp.x()));
      bottomRight.x() = std::max(bottomRight.x(), (f32)std::ceil(temp.x()));
      upperLeft.y()   = std::min(upperLeft.y(),   (f32)std::floor(temp.y()));
      bottomRight.y() = std::max(bottomRight.y(), (f32)std::ceil(temp.y()));
    }

    // Warp the lids
    for(auto poly : {&lowerLidPoly, &upperLidPoly}) {
      for(auto & point : *poly) {
        const Point<3,f32> pointF32{
          static_cast<f32>(whichEye == WhichEye::Left ? point.x : -point.x), static_cast<f32>(point.y), 1.f
        };

        Point<2,f32> temp = W * pointF32;
        point.x = ROUND(temp.x());
        point.y = ROUND(temp.y());
      }
    }
    //ANKI_CPU_PROFILE_STOP(prof_applyMat);

    // Make sure the upper left and bottom right points are in bounds (note that we loop over
    // pixels below *inclusive* of the bottom right point, so we use HEIGHT/WIDTH-1)

    // Note: visual artifacts can be seen with the NEON pathway, only slightly on the top/left border, which
    //       is fixed by extending the ROI by half the filter size (as expected).
    //       However, they are very clearly visible on the bottom/right and only disappear by extending the
    //       ROI by kProcFace_AntiAliasingSize.

    upperLeft.x() = std::max(0.f, upperLeft.x()-kProcFace_AntiAliasingSize*.5f);
    upperLeft.y() = std::max(0.f, upperLeft.y()-kProcFace_AntiAliasingSize*.5f);
    bottomRight.x() = std::min((f32)(ProceduralFace::WIDTH-1), bottomRight.x()+kProcFace_AntiAliasingSize);
    bottomRight.y() = std::min((f32)(ProceduralFace::HEIGHT-1), bottomRight.y()+kProcFace_AntiAliasingSize);

    // Create the bounding that we're returning from the upperLeft and bottomRight points
    eyeBoundingBox = Rectangle<f32>(upperLeft, bottomRight);

    // Draw eye
    _eyeShape.Allocate(ProceduralFace::HEIGHT, ProceduralFace::WIDTH);
    _eyeShape.FillWith(0);
    
    cv::fillConvexPoly(_eyeShape.get_CvMat_(), eyePoly, 255, kLineType);

    // Black out lids
    if(!upperLidPoly.empty()) {
      if(faceData.GetParameter(whichEye, Parameter::UpperLidBend) < 0.f) {
        const cv::Point* pts[1] = { &upperLidPoly[0] };
        int npts[1] = { (int)upperLidPoly.size() };
        cv::fillPoly(_eyeShape.get_CvMat_(), pts, npts, 1, 0, kLineType);
      } else {
        cv::fillConvexPoly(_eyeShape.get_CvMat_(), upperLidPoly, 0, kLineType);
      }
    }
    if(!lowerLidPoly.empty()) {
      if(faceData.GetParameter(whichEye, Parameter::LowerLidBend) < 0.f) {
        const cv::Point* pts[1] = { &lowerLidPoly[0] };
        int npts[1] = { (int)lowerLidPoly.size() };
        cv::fillPoly(_eyeShape.get_CvMat_(), pts, npts, 1, 0, kLineType);
      } else {
        cv::fillConvexPoly(_eyeShape.get_CvMat_(), lowerLidPoly, 0, kLineType);
      }
    }
  
    // only render if the eyes are large enough, scale is handled in the calling function
    if(eyeWidth > 0 && eyeHeight > 0)
    {
      // The hotspot center params leave the hot spot at the eye center if zero. If non-zero,
      // they shift left/right/up/down where a magnitude of 1.0 moves the center to the extreme
      // edge of the eye shape
      const Point2f hotSpotCenter = W * Point3f(0.5f*eyeWidth*faceData.GetParameter(whichEye, Parameter::HotSpotCenterX),
                                                0.5f*eyeHeight*faceData.GetParameter(whichEye, Parameter::HotSpotCenterY),
                                                1.f);

      // Inner Glow = the brighter glow at the center of the eye that falls off radially towards the edge of the eye
      // Outer Glow = the "halo" effect around the outside of the eye shape
      // Add inner glow to the eye shape, before we compute the outer glow, so that boundaries conditions match.
      if(kProcFace_HotspotRender)
      {
        ANKI_CPU_PROFILE("HotspotRender");

        const f32 sigmaX = kProcFace_HotspotFalloff*eyeWidth;
        const f32 sigmaY = kProcFace_HotspotFalloff*eyeHeight;
        
        // Compute the 2x2 inverse covariance matrix for the hotspot Gaussian falloff, incorporating
        // the scale and rotation from the eye and face warp (so it moves and rotates with the eyes)
        Matrix_2x2f sigmaInv;
        {
          const Matrix_2x2f W22{W(0,0), W(0,1), W(1,0), W(1,1)};
          Matrix_2x2f W22t = W22.GetTranspose();
          const Matrix_2x2f sigma{sigmaX, 0.f, 0.f, sigmaY};
          const Matrix_2x2f sigmaWarped(sigma * W22t * W22 * sigma);
          sigmaInv = sigmaWarped.GetInverse();
        }

        DEV_ASSERT_MSG(upperLeft.y()>=0 && bottomRight.y()<_eyeShape.GetNumRows(), "ProceduralFaceDrawer.DrawEye.BadRow", "%f %f", upperLeft.y(), bottomRight.y());
        DEV_ASSERT_MSG(upperLeft.x()>=0 && bottomRight.x()<_eyeShape.GetNumCols(), "ProceduralFaceDrawer.DrawEye.BadCol", "%f %f", upperLeft.x(), bottomRight.x());
        
        for(s32 i=upperLeft.y(); i<=bottomRight.y(); ++i) {

          u8* eyeShape_i = _eyeShape.GetRow(i);
          for(s32 j=upperLeft.x(); j<=bottomRight.x(); ++j) {

            u8& eyeValue  = eyeShape_i[j];
            const bool insideEye = (eyeValue > 0);
            if(insideEye) {
              // TODO: Use a separate approximation helper or LUT to get falloff
              const f32 dx = (f32)j-hotSpotCenter.x();
              const f32 dy = (f32)i-hotSpotCenter.y();
              
              // Hardcode simple 1x2 x 2x2 x 2x1 matrix multiplication here:
              //   [dx dy] * Sigma^(-1) * [dx]
              //                          [dy]
              const f32 x = ((dx*sigmaInv(0,0) + dy*sigmaInv(1,0))*dx +
                             (dx*sigmaInv(0,1) + dy*sigmaInv(1,1))*dy);
              
              const f32 falloff = fastExp(-0.5f*x);
              DEV_ASSERT_MSG(Util::InRange(falloff, 0.f, 1.f), "ProceduralFaceDrawer.DrawEye.BadInnerGlowFalloffValue", "%f", falloff);

              eyeValue = NUMERIC_CAST_CLAMPED(ROUND(static_cast<f32>(eyeValue) * falloff));
            }
          }
        }
      }
      
#if PROCEDURALFACE_GLOW_FEATURE
      Rectangle<s32> eyeBoundingBoxS32(upperLeft.CastTo<s32>(), bottomRight.CastTo<s32>());
      Vision::Image eyeShapeROI = _eyeShape.GetROI(eyeBoundingBoxS32);

      // Compute glow from the final eye shape (after lids are drawn)
      _glowImg.Allocate(faceImg.GetNumRows(), faceImg.GetNumCols());
      _glowImg.FillWith(0);

      const Value glowLightness = kProcFace_GlowLightnessMultiplier * faceData.GetParameter(whichEye, Parameter::GlowLightness);
      if(Util::IsFltGTZero(glowLightness) && Util::IsFltGTZero(glowFraction)) {
        ANKI_CPU_PROFILE("Glow");
        Vision::Image glowImgROI = _glowImg.GetROI(eyeBoundingBoxS32);

        s32 glowSizeX = std::ceil(glowFraction * 0.5f * scaledEyeWidth);
        s32 glowSizeY = std::ceil(glowFraction * 0.5f * scaledEyeHeight);

        // Make sure sizes are odd:
        if(glowSizeX % 2 == 0) {
          ++glowSizeX;
        }
        if(glowSizeY % 2 == 0) {
          ++glowSizeY;
        }

        switch(kProcFace_GlowFilter) {
          case (uint8_t)Filter::BoxFilter:
            cv::boxFilter(eyeShapeROI.get_CvMat_(), glowImgROI.get_CvMat_(), -1, cv::Size(glowSizeX,glowSizeY));
            break;
          case (uint8_t)Filter::GaussianFilter:
            cv::GaussianBlur(eyeShapeROI.get_CvMat_(), glowImgROI.get_CvMat_(), cv::Size(glowSizeX,glowSizeY),
                             (f32)glowSizeX, (f32)glowSizeY);
            break;
          case (uint8_t)Filter::NEONBoxFilter: {
            eyeShapeROI.BoxFilter(glowImgROI, 3);
            break;
          }
      }
#endif

      if(kProcFace_EnableAntiAliasing) {
        // Antialiasing (AFTER glow because it changes eyeShape, which we use to compute the glow above)

        // Optimization: if we're applying interpolation and transforming the face, skip antialiasing?
        ApplyAntiAliasing(_eyeShape, upperLeft.x(), upperLeft.y(), bottomRight.x(), bottomRight.y());
      }
        
      const f32 eyeLightness = faceData.GetParameter(whichEye, Parameter::Lightness);
      DEV_ASSERT(Util::InRange(eyeLightness, -1.f, 1.f), "ProceduralFaceDrawer.DrawEye.InvalidLightness");

      // Draw the eye into the face image, adding outer glow, noise, and stylized scanlines
      {
        ANKI_CPU_PROFILE("DrawEyePixels");

        DEV_ASSERT_MSG(upperLeft.y()>=0 && bottomRight.y()<faceImg.GetNumRows(), "ProceduralFaceDrawer.DrawEye.BadRow", "%f %f", upperLeft.y(), bottomRight.y());
        DEV_ASSERT_MSG(upperLeft.x()>=0 && bottomRight.x()<faceImg.GetNumCols(), "ProceduralFaceDrawer.DrawEye.BadCol", "%f %f", upperLeft.x(), bottomRight.x());
        for(s32 i=upperLeft.y(); i<=bottomRight.y(); ++i) {

          u8* faceImg_i = faceImg.GetRow(i);
          const u8* eyeShape_i = _eyeShape.GetRow(i);
#if PROCEDURALFACE_GLOW_FEATURE
          const u8* glowImg_i  = _glowImg.GetRow(i);
#endif

          for(s32 j=upperLeft.x(); j<=bottomRight.x(); ++j) {

            const u8 eyeValue = eyeShape_i[j];
#if PROCEDURALFACE_GLOW_FEATURE
            const u8 glowValue = glowImg_i[j];
            const bool somethingToDraw = (eyeValue > 0 || glowValue > 0);
#else
            const bool somethingToDraw = (eyeValue > 0);
#endif

            if(somethingToDraw) {
              f32 newValue = static_cast<f32>(eyeValue);

#if PROCEDURALFACE_GLOW_FEATURE
              // Combine everything together: inner glow falloff, and the glow value.
              // Note that the value in glowImg/eyeShape is already [0,255]
              newValue = std::max(newValue,eyeValue);

              const bool isPartOfEye = (eyeValue >= glowValue); // (and not part of glow)
              if(isPartOfEye) {
                newValue *= kProcFace_EyeLightnessMultiplier;
              } else {
                newValue *= glowLightness;
              }
#else
              newValue *= kProcFace_EyeLightnessMultiplier;
#endif

              newValue *= eyeLightness;

              // Put the final value into the face image
              // Note: If we're drawing the right eye, there may already be something in the image
              //       from when we drew the left eye (e.g. with large glow), so use max
              faceImg_i[j] = std::max(faceImg_i[j], NUMERIC_CAST_CLAMPED(ROUND(newValue)));
            }
          }
        }
      }
    }

    // Add distortion noise
    auto scanlineDistorter = faceData.GetScanlineDistorter();
    if(nullptr != scanlineDistorter) {
      scanlineDistorter->AddOffNoise(W, eyeHeight, eyeWidth, faceImg);
    }
      
  } // DrawEye()

  void ProceduralFaceDrawer::DrawFace(const ProceduralFace& faceData,
                                      const Util::RandomGenerator& rng,
                                      Vision::ImageRGB565& output)
  {
    ANKI_CPU_PROFILE("DrawFace");

    bool dirty = false; // set to true to force all stages to render, previous pipeline
    dirty = DrawEyes(faceData, dirty);
    dirty = ApplyScanlines(_faceCache.img8[_faceCache.finalFace], faceData.GetScanlineOpacity(), dirty);
    dirty = DistortScanlines(faceData, dirty);
    dirty = ApplyNoise(rng, dirty);
    dirty = ConvertColorspace(faceData, output, dirty);
    dirty = ApplyCustomOverlay(faceData, output, dirty);
  } // DrawFace()

  bool ProceduralFaceDrawer::DrawEyes(const ProceduralFace& faceData, bool dirty)
  {
    ANKI_CPU_PROFILE("DrawEyes");

    // The console setting is outside faceData, so changing it must also
    // invalidate a stationary face. -1 forces the first face to render.
    static int cachedEyeShape = -1;

    if(!dirty) {
      
      if (cachedEyeShape != kProcFace_EyeShape ||
          _faceCache.faceData.GetParameters(WhichEye::Left) != faceData.GetParameters(WhichEye::Left) ||
          _faceCache.faceData.GetParameters(WhichEye::Right) != faceData.GetParameters(WhichEye::Right) ||
          _faceCache.faceData.GetFaceAngle() != faceData.GetFaceAngle() ||
          _faceCache.faceData.GetFacePosition() != faceData.GetFacePosition() ||
          _faceCache.faceData.GetFaceScale() != faceData.GetFaceScale() ||
          !Util::IsFltNear(_faceCache.faceData.GetHue(), faceData.GetHue()) ||
          !Util::IsFltNear(_faceCache.faceData.GetSaturation(), faceData.GetSaturation())) {
        // Something changed, we must draw
        dirty = true;
      }
    }

    if(dirty) {
      // Update parameters used to generate this cached image
      cachedEyeShape = kProcFace_EyeShape;
      _faceCache.faceData.SetParameters(WhichEye::Left, faceData.GetParameters(WhichEye::Left));
      _faceCache.faceData.SetParameters(WhichEye::Right, faceData.GetParameters(WhichEye::Right));

      _faceCache.faceData.SetFaceAngle(faceData.GetFaceAngle());
      _faceCache.faceData.SetFacePosition(faceData.GetFacePosition());
      _faceCache.faceData.SetFaceScale(faceData.GetFaceScale());
      
      // Target image for this stage
      // Eyes are always first, assign first element in face cache
      _faceCache.finalFace = _faceCache.eyes = 0;
      DEV_ASSERT(_faceCache.finalFace < _faceCache.kSize, "ProceduralFaceDrawer.DistortScanlines.FaceCacheTooSmall");
      _faceCache.img8[_faceCache.eyes].Allocate(ProceduralFace::HEIGHT, ProceduralFace::WIDTH); // Will do nothing if already the right size
      _faceCache.img8[_faceCache.eyes].FillWith(0);

      // Create a full-face warp matrix if needed and provide it to the eye-rendering call
      const Matrix_3x3f* W_facePtr = nullptr;
      Matrix_3x3f W_face;
      const bool hasFaceTransform = (!Util::IsNearZero(_faceCache.faceData.GetFaceAngle()) ||
                                     !(_faceCache.faceData.GetFacePosition() == 0) ||
                                     !(_faceCache.faceData.GetFaceScale() == 1.f));
      if(hasFaceTransform)
      {
        W_face = GetTransformationMatrix(faceData.GetFaceAngle(),
                                         faceData.GetFaceScale().x(),
                                         faceData.GetFaceScale().y(),
                                         faceData.GetFacePosition().x(),
                                         faceData.GetFacePosition().y(),
                                         static_cast<f32>(ProceduralFace::WIDTH)*0.5f,
                                         static_cast<f32>(ProceduralFace::HEIGHT)*0.5f);
        W_facePtr = &W_face;
      }
      
      DrawEye(_faceCache.faceData, WhichEye::Left,  W_facePtr, _faceCache.img8[_faceCache.eyes], _leftBBox);
      DrawEye(_faceCache.faceData, WhichEye::Right, W_facePtr, _faceCache.img8[_faceCache.eyes], _rightBBox);
      
      const std::array<Quad2f,2> leftRightQuads{{ Quad2f(_leftBBox), Quad2f(_rightBBox) }};
      
      _faceRowMin = ProceduralFace::HEIGHT-1;
      _faceRowMax = 0;
      _faceColMin = _leftBBox.GetX();
      _faceColMax = _rightBBox.GetXmax();
      
      for(const auto& quad : leftRightQuads) {
        for(const auto& pt : quad) {
          _faceRowMin = std::min(_faceRowMin, (s32)std::floor(pt.y()));
          _faceRowMax = std::max(_faceRowMax, (s32)std::ceil(pt.y()));
          _faceColMin = std::min(_faceColMin, (s32)std::floor(pt.x()));
          _faceColMax = std::max(_faceColMax, (s32)std::ceil(pt.x()));
        }
      }
     
      // Just to be safe:
      _faceColMin = Util::Clamp(_faceColMin, 0, ProceduralFace::WIDTH-1);
      _faceColMax = Util::Clamp(_faceColMax, 0, ProceduralFace::WIDTH-1);
      _faceRowMin = Util::Clamp(_faceRowMin, 0, ProceduralFace::HEIGHT-1);
      _faceRowMax = Util::Clamp(_faceRowMax, 0, ProceduralFace::HEIGHT-1);

      _faceCache.finalFace = _faceCache.eyes;
    }
    
    return dirty;
  } // DrawEyes()

  bool ProceduralFaceDrawer::DistortScanlines(const ProceduralFace& faceData, bool dirty)
  {
    ANKI_CPU_PROFILE("DistortScanlines");

    // Distort the scanlines
    auto scanlineDistorter = faceData.GetScanlineDistorter();
    if(nullptr != scanlineDistorter) {
      // Any scanline distorter affects the output image, assigned a new element in the face cache
      // and make all later stages needed updates

      // Note: scanline distortion has changed from modifying the input image to generating a new
      //       output image, this allows eye rendering and face transforms to be cached and the
      //       scanline distortion applied to the original output rather than a face that has already
      //       had scanline distortion applied.
      dirty = true;

      _faceCache.finalFace = _faceCache.distortedFace = _faceCache.eyes+1;
      DEV_ASSERT(_faceCache.finalFace < _faceCache.kSize, "ProceduralFaceDrawer, face cache too small.");
      _faceCache.img8[_faceCache.distortedFace].Allocate(ProceduralFace::HEIGHT, ProceduralFace::WIDTH);
      _faceCache.img8[_faceCache.distortedFace].FillWith(0);

      s32 newColMin = _faceColMin;
      s32 newColMax = _faceColMax;

      const f32 scale = 1.f / (_faceRowMax - _faceRowMin);
      for(s32 row=_faceRowMin; row < _faceRowMax; ++row) {
        const f32 eyeFrac = (row - _faceRowMin) * scale;
        const s32 shift = scanlineDistorter->GetEyeDistortionAmount(eyeFrac) * sizeof(Vision::PixelRGB);

        if(shift < 0) {
          const u8* faceImg_row = _faceCache.img8[_faceCache.eyes].GetRow(row);
          u8* img_row = _faceCache.img8[_faceCache.distortedFace].GetRow(row);
          memcpy(img_row, faceImg_row-shift, ProceduralFace::WIDTH+shift);

          if(_faceColMin+shift < newColMin) {
            // shift left increases face bounding box, update new bounds,
            // but keep existing bounds for the rest of the effect
            newColMin = _faceColMin+shift;
          }
        } else if(shift > 0) {
          const u8* faceImg_row = _faceCache.img8[_faceCache.eyes].GetRow(row);
          u8* img_row = _faceCache.img8[_faceCache.distortedFace].GetRow(row);
          memcpy(img_row+shift, faceImg_row, ProceduralFace::WIDTH-shift);

          if(_faceColMax+shift > newColMax) {
            // shift right increases face bounding box, update new bounds,
            // but keep existing bounds for the rest of the effect
            newColMax = _faceColMax+shift;
          }
        }
      }

      _faceColMin = newColMin;
      _faceColMax = newColMax;
    } else {
      // No scanline distortion, pass forward the cached face transform as output
      _faceCache.finalFace = _faceCache.distortedFace = _faceCache.eyes;
    }

    return dirty;
  } // DistortScanlines()

  bool ProceduralFaceDrawer::ApplyNoise(const Util::RandomGenerator& rng, bool dirty)
  {
    if(PROCEDURALFACE_NOISE_FEATURE && (kProcFace_NoiseNumFrames > 0)) {
      ANKI_CPU_PROFILE("ApplyNoise");

      _faceCache.finalFace = _faceCache.distortedFace+1;
      DEV_ASSERT(_faceCache.finalFace < _faceCache.kSize, "ProceduralFaceDrawer, face cache too small.");
      _faceCache.img8[_faceCache.finalFace].Allocate(ProceduralFace::HEIGHT, ProceduralFace::WIDTH);
      if(dirty) {
        _faceCache.img8[_faceCache.finalFace].FillWith(0);
      }

      dirty = true;

      // Assign a new face cache for noise, the reason for the changes.
      // If the eyes haven't changed, the face hasn't changed and no-scanline distorter
      // then there is no work to do until the noise stage

      // TODO: https://ankiinc.atlassian.net/browse/VIC-3647 Apply noise to eyes individually rather than entire face
      // ^ might not be desirable with the neon version. May incur cache penalty when pulling images from memory twice
      // instead of just once...
      
      const Array2d<u8>& noiseImg = GetNoiseImage(rng);

      for(s32 i=_faceRowMin; i<=_faceRowMax; ++i) {

        const u8* noiseImg_i = noiseImg.GetRow(i);
        const u8* eyeShape_i = _faceCache.img8[_faceCache.distortedFace].GetRow(i);
        u8* faceImg_i = _faceCache.img8[_faceCache.finalFace].GetRow(i);

        noiseImg_i += _faceColMin;
        eyeShape_i += _faceColMin;
        faceImg_i += _faceColMin;

        s32 j = _faceColMin;
#ifdef __ARM_NEON__
        const s32 kNumElementsProcessed = 16;
        for(; j <= _faceColMax-(kNumElementsProcessed-1); j += kNumElementsProcessed)
        {
          uint8x16_t eye = vld1q_u8(eyeShape_i);
          eyeShape_i += kNumElementsProcessed;

          uint8x16_t noise = vld1q_u8(noiseImg_i);
          noiseImg_i += kNumElementsProcessed;

          // Multiply eye values by noise and expand to u16
          uint16x8_t value1 = vmull_u8(vget_low_u8(eye), vget_low_u8(noise));
          uint16x8_t value2 = vmull_u8(vget_high_u8(eye), vget_high_u8(noise));
          // Saturating narrowing right shift by 7 (divide by 128)
          uint8x8_t output1 = vqshrn_n_u16(value1, 7);
          uint8x8_t output2 = vqshrn_n_u16(value2, 7);
          // Combine back into u8x16
          uint8x16_t output = vcombine_u8(output1, output2);

          vst1q_u8(faceImg_i, output);
          faceImg_i += kNumElementsProcessed;
        }
#endif
     
        for(; j<=_faceColMax; ++j) {
          *faceImg_i = Util::numeric_cast_clamped<u8>((static_cast<u16>(*eyeShape_i) * static_cast<u16>(*noiseImg_i)) >> 7);
          ++noiseImg_i;
          ++eyeShape_i;
          ++faceImg_i;
        }
      }
    }

    return dirty;
  } // ApplyNoise()

bool ProceduralFaceDrawer::ConvertColorspace(const ProceduralFace& faceData,
                                             Vision::ImageRGB565& output,
                                             bool dirty)
{
  ANKI_CPU_PROFILE("ConvertColorspace");

  output.Allocate(FACE_DISPLAY_HEIGHT, FACE_DISPLAY_WIDTH);
  output.FillWith(Vision::PixelRGB(0,0,0));

  if(!dirty) {
    if(!Util::IsFltNear(_faceCache.faceData.GetHue(), faceData.GetHue()) ||
       !Util::IsFltNear(_faceCache.faceData.GetSaturation(), faceData.GetSaturation()))
      dirty = true;
  }

  if(dirty)
  {
    _faceCache.faceData.SetHue(faceData.GetHue());
    _faceCache.faceData.SetSaturation(faceData.GetSaturation());

    const u8 drawHue = ROUND(255.f*faceData.GetHue());

    f32 satF = 1.f;
#if PROCEDURALFACE_ANIMATED_SATURATION
    satF *= faceData.GetParameter(whichEye, Parameter::Saturation);
#endif
#if PROCEDURALFACE_PROCEDURAL_SATURATION
    satF *= faceData.GetSaturation();
#endif
    const u8 drawSat = ROUND(255.f*satF);

    Rectangle<s32> roiR(_faceColMin,_faceRowMin,
                        _faceColMax-_faceColMin+1,
                        _faceRowMax-_faceRowMin+1);

    Vision::ImageRGB565 outROI = output.GetROI(roiR);
    _faceCache.img8[_faceCache.finalFace]
            .GetROI(roiR)
            .ConvertV2RGB565(drawHue,drawSat, outROI);
  }

  return dirty;
}

bool ProceduralFaceDrawer::ApplyCustomOverlay(const ProceduralFace& faceData,
                                             Vision::ImageRGB565& output,
                                             bool dirty)
{
  static bool _didLoadCustom = (LoadCustomEyePNG(), true);

  if(kProcFace_CustomEyes && _hasCustomEyes)
  {
    std::lock_guard<std::mutex> lk(gCustomEyeMtx);
    (void)_didLoadCustom;

    const int padX = 5;
    int xMin0 = _faceColMin - padX;
    int xMax0 = _faceColMax + padX;
    int yMin0 = _faceRowMin;
    int yMax0 = _faceRowMax;
    int fullW = xMax0 - xMin0 + 1;
    int fullH = yMax0 - yMin0 + 1;

    int xMin = std::max(0, xMin0);
    int xMax = std::min(ProceduralFace::WIDTH - 1, xMax0);
    int yMin = std::max(0, yMin0);
    int yMax = std::min(ProceduralFace::HEIGHT - 1, yMax0);
    Rectangle<s32> roiR(xMin, yMin,
                        xMax - xMin + 1,
                        yMax - yMin + 1);

    auto dstROI  = output.GetROI(roiR);
    auto maskROI = _faceCache.img8[_faceCache.finalFace].GetROI(roiR);

    int overlayW = _customEyeOverlay.GetNumCols();
    int overlayH = _customEyeOverlay.GetNumRows();
    float scaleX = overlayW / float(fullW);
    float scaleY = overlayH / float(fullH);

    for(int y = 0; y < roiR.GetHeight(); ++y) {
      auto* dstRow  = dstROI .GetRow(y);
      auto* maskRow = maskROI.GetRow(y);
      int globalY    = yMin + y;
      int sampleY    = std::min(overlayH - 1, std::max(0, int((globalY - yMin0) * scaleY)));
      for(int x = 0; x < roiR.GetWidth(); ++x) {
        if(maskRow[x] == 0) continue;
        int globalX = xMin + x;
        int sampleX = std::min(overlayW - 1, std::max(0, int((globalX - xMin0) * scaleX)));
        u8 alpha = _customEyeAlpha.GetRow(sampleY)[sampleX];
        if(alpha == 0) continue;

        // unpack d
        u16 destVal = dstRow[x].GetValue();
        u8 dR = (destVal >> 11) & 0x1F;
        u8 dG = (destVal >> 5)  & 0x3F;
        u8 dB =  destVal        & 0x1F;
        u8 D_R = (dR << 3) | (dR >> 2);
        u8 D_G = (dG << 2) | (dG >> 4);
        u8 D_B = (dB << 3) | (dB >> 2);

        // overlay
        u16 oVal = _customEyeOverlay.GetRow(sampleY)[sampleX].GetValue();
        u8 oR = (oVal >> 11) & 0x1F;
        u8 oG = (oVal >> 5)  & 0x3F;
        u8 oB =  oVal        & 0x1F;
        u8 O_R = (oR << 3) | (oR >> 2);
        u8 O_G = (oG << 2) | (oG >> 4);
        u8 O_B = (oB << 3) | (oB >> 2);

        // modulate by d intensity
        u8 dV = std::max({D_R, D_G, D_B});
        O_R = O_R * dV / 255;
        O_G = O_G * dV / 255;
        O_B = O_B * dV / 255;

        // alpha blend
        u8 invA = 255 - alpha;
        u8 R = (O_R * alpha + D_R * invA) / 255;
        u8 G = (O_G * alpha + D_G * invA) / 255;
        u8 B = (O_B * alpha + D_B * invA) / 255;

        // pack
        u16 packed = ((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3);
        dstRow[x].SetValue(packed);
      }
    }

    dirty = true;
  }
  return dirty;
}



  bool ProceduralFaceDrawer::GetNextBlinkFrame(ProceduralFace& faceData,
                                               BlinkState& out_blinkState,
                                               TimeStamp_t& out_offset)
  {
    static ProceduralFace originalFace;

    struct BlinkParams {
      ProceduralFace::Value height, width;
      TimeStamp_t t;
      BlinkState blinkState;
    };

    static const std::vector<BlinkParams> blinkParams{
      {.height = .85f, .width = 1.05f, .t = 33,  .blinkState = BlinkState::Closing},
      {.height = .6f,  .width = 1.2f,  .t = 33,  .blinkState = BlinkState::Closing},
      {.height = .1f,  .width = 2.5f,  .t = 33,  .blinkState = BlinkState::Closing},
      {.height = .05f, .width = 5.f,   .t = 33,  .blinkState = BlinkState::Closed},
      {.height = .15f, .width = 2.f,   .t = 33,  .blinkState = BlinkState::JustOpened},
      {.height = .7f,  .width = 1.2f,  .t = 33,  .blinkState = BlinkState::Opening},
      {.height = .9f,  .width = 1.f,   .t = 100, .blinkState = BlinkState::Opening}
    };

    static const std::vector<Parameter> lidParams{
      Parameter::LowerLidY, Parameter::LowerLidBend, Parameter::LowerLidAngle,
      Parameter::UpperLidY, Parameter::UpperLidBend, Parameter::UpperLidAngle,
    };

    static auto paramIter = blinkParams.begin();

    if(paramIter == blinkParams.end()) {
      // Set everything back to original params
      faceData = originalFace;
      out_offset = 33;

      // Reset for next time
      paramIter = blinkParams.begin();
      // Let caller know this is the last blink frame
      return false;

    } else {
      if(paramIter == blinkParams.begin()) {
        // Store the current pre-blink parameters before we muck with them
        originalFace = faceData;
      }

      for(auto whichEye : {WhichEye::Left, WhichEye::Right}) {
        faceData.SetParameter(whichEye, Parameter::EyeScaleX,
                              originalFace.GetParameter(whichEye, Parameter::EyeScaleX) * paramIter->width);
        faceData.SetParameter(whichEye, Parameter::EyeScaleY,
                              originalFace.GetParameter(whichEye, Parameter::EyeScaleY) * paramIter->height);
      }
      out_offset = paramIter->t;

      switch(paramIter->blinkState) {
        case BlinkState::Closed:
        {
          // In case eyes are at different height, get the average height so the
          // blink line when completely closed is nice and horizontal
          const ProceduralFace::Value blinkHeight = (originalFace.GetParameter(WhichEye::Left,  Parameter::EyeCenterY) +
                                                     originalFace.GetParameter(WhichEye::Right, Parameter::EyeCenterY))/2;

          // Zero out the lids so they don't interfere with the "closed" line
          for(auto whichEye : {WhichEye::Left, WhichEye::Right}) {
            faceData.SetParameter(whichEye, Parameter::EyeCenterY, blinkHeight);
            for(auto lidParam : lidParams) {
              faceData.SetParameter(whichEye, lidParam, 0);
            }
          }
          break;
        }
        case BlinkState::JustOpened:
        {
          // Restore eye heights and lids
          for(auto whichEye : {WhichEye::Left, WhichEye::Right}) {
            faceData.SetParameter(whichEye, Parameter::EyeCenterY,
                                  originalFace.GetParameter(whichEye, Parameter::EyeCenterY));
            for(auto lidParam : lidParams) {
              faceData.SetParameter(whichEye, lidParam, originalFace.GetParameter(whichEye, lidParam));
            }
          }
          break;
        }
        default:
          break;
      }
      
      out_blinkState = paramIter->blinkState;
      ++paramIter;

      // Let caller know there are more blink frames left, so keep calling
      return true;
    }

  } // GetNextBlinkFrame()
  
  bool ProceduralFaceDrawer::ApplyScanlines(Vision::ImageRGB& imageHsv, const float opacity, bool dirty)
  {
#if PROCEDURALFACE_SCANLINE_FEATURE
    if(kProcFace_Scanlines) {
      ANKI_CPU_PROFILE("ApplyScanlines");

      const bool applyScanlines = !Util::IsNear(opacity, 1.f);
      if (applyScanlines) {
        DEV_ASSERT(Util::InRange(opacity, 0.f, 1.f), "ProceduralFaceDrawer.ApplyScanlines.InvalidOpacity");

        dirty = true;

        const auto nRows = imageHsv.GetNumRows();
        const auto nCols = imageHsv.GetNumCols();

        for (int i=0 ; i < nRows ; i++) {
          if (ShouldApplyScanlineToRow(i)) {
            auto* thisRow = imageHsv.GetRow(i);
            for (int j=0 ; j < nCols ; j++) {
              // the 'blue' channel in an HSV image is the value
              thisRow[j].b() *= opacity;
            }
          }
        }
      }
    }
#endif

    return dirty;
  } // ApplyScanlines()

  bool ProceduralFaceDrawer::ApplyScanlines(Vision::Image& image8, const float opacity, bool dirty)
  {
#if PROCEDURALFACE_SCANLINE_FEATURE
    if(kProcFace_Scanlines) {
      ANKI_CPU_PROFILE("ApplyScanlines");

      DEV_ASSERT(Util::InRange(opacity, 0.f, 1.f), "ProceduralFaceDrawer.ApplyScanlines.InvalidOpacity");

      const bool is0 = Util::IsNear(opacity, 0.f);
      const bool is1 = Util::IsNear(opacity, 1.f);
      if (is0) {
        dirty = true;

        const auto nRows = image8.GetNumRows();
        const auto nCols = image8.GetNumCols();

        for (int i=0 ; i < nRows ; i++) {
          if (ShouldApplyScanlineToRow(i)) {
            u8* thisRow = image8.GetRow(i);
            memset(thisRow, 0, nCols);
          }
        }
      } else if (!is1) {
        dirty = true;

        const auto nRows = image8.GetNumRows();
        const auto nCols = image8.GetNumCols();

        for (int i=0 ; i < nRows ; i++) {
          if (ShouldApplyScanlineToRow(i)) {
            u8* thisRow = image8.GetRow(i);
            for (int j=0 ; j < nCols ; j++) {
              *thisRow *= opacity;
              ++thisRow;
            }
          }
        }
      }
    }
#endif

    return dirty;
  } // ApplyScanlines()
} // namespace Vector
} // namespace Anki
