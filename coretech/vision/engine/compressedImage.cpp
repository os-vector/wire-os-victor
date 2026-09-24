/**
 * File: compressedImage.cpp
 *
 * Author: Al Chaussee
 * Date:   11/8/2018
 *
 * Description: Class for storing a jpg compressed image along with info about the original uncompressed image
 *
 * Copyright: Anki, Inc. 2018
 **/

#include "coretech/vision/engine/compressedImage.h"

#include "coretech/vision/engine/image.h"

#include "util/helpers/templateHelpers.h"

#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/imgcodecs.hpp"

#ifdef VICOS
#include "anki/cozmo/shared/factory/emrHelper.h"
#include <turbojpeg.h>
#endif

namespace Anki {
namespace Vision {

#ifdef VICOS
namespace {

struct TurboJpegCompressor
{
  tjhandle       handle      = nullptr;
  unsigned char* jpegBuf     = nullptr;
  size_t         jpegBufSize = 0;

  ~TurboJpegCompressor()
  {
    if(nullptr != jpegBuf)
    {
      tj3Free(jpegBuf);
    }
    if(nullptr != handle)
    {
      tj3Destroy(handle);
    }
  }
};

thread_local TurboJpegCompressor s_turboJpeg;

bool CompressWithTurboJpeg(const cv::Mat& mat, const int pixelFormat, const int subsamp, const s32 quality,
                           std::vector<u8>& compressedBuffer)
{
  if(mat.empty())
  {
    return false;
  }

  TurboJpegCompressor& tj = s_turboJpeg;
  if(nullptr == tj.handle)
  {
    tj.handle = tj3Init(TJINIT_COMPRESS);
    if(nullptr == tj.handle)
    {
      PRINT_NAMED_WARNING("CompressedImage.CompressWithTurboJpeg.InitFailed", "");
      return false;
    }
    tj3Set(tj.handle, TJPARAM_FASTDCT, 1);
    tj3Set(tj.handle, TJPARAM_NOREALLOC, 1);
  }

  if((0 != tj3Set(tj.handle, TJPARAM_QUALITY, quality)) ||
     (0 != tj3Set(tj.handle, TJPARAM_SUBSAMP, subsamp)))
  {
    PRINT_NAMED_WARNING("CompressedImage.CompressWithTurboJpeg.SetParamFailed", "%s", tj3GetErrorStr(tj.handle));
    return false;
  }

  const size_t maxJpegSize = tj3JPEGBufSize(mat.cols, mat.rows, subsamp);
  if(0 == maxJpegSize)
  {
    PRINT_NAMED_WARNING("CompressedImage.CompressWithTurboJpeg.BufSizeFailed", "%s", tj3GetErrorStr(tj.handle));
    return false;
  }

  if(tj.jpegBufSize < maxJpegSize)
  {
    tj3Free(tj.jpegBuf);
    tj.jpegBuf = static_cast<unsigned char*>(tj3Alloc(maxJpegSize));
    tj.jpegBufSize = (nullptr != tj.jpegBuf ? maxJpegSize : 0);
    if(nullptr == tj.jpegBuf)
    {
      PRINT_NAMED_WARNING("CompressedImage.CompressWithTurboJpeg.AllocFailed", "%zu bytes", maxJpegSize);
      return false;
    }
  }

  size_t jpegSize = tj.jpegBufSize;
  if(0 != tj3Compress8(tj.handle, mat.data, mat.cols, static_cast<int>(mat.step[0]), mat.rows, pixelFormat,
                       &tj.jpegBuf, &jpegSize))
  {
    PRINT_NAMED_WARNING("CompressedImage.CompressWithTurboJpeg.CompressFailed", "%s", tj3GetErrorStr(tj.handle));
    return false;
  }

  compressedBuffer.assign(tj.jpegBuf, tj.jpegBuf + jpegSize);
  return true;
}

bool TurboJpegCompress(const ImageBase<PixelRGB>& img, const s32 quality, std::vector<u8>& compressedBuffer)
{
  const int pixelFormat = (Vector::IsXray() ? TJPF_BGR : TJPF_RGB);
  return CompressWithTurboJpeg(img.get_CvMat_(), pixelFormat, TJSAMP_420, quality, compressedBuffer);
}

bool TurboJpegCompress(const ImageBase<u8>& img, const s32 quality, std::vector<u8>& compressedBuffer)
{
  return CompressWithTurboJpeg(img.get_CvMat_(), TJPF_GRAY, TJSAMP_GRAY, quality, compressedBuffer);
}

template<class PixelType>
bool TurboJpegCompress(const ImageBase<PixelType>&, const s32, std::vector<u8>&)
{
  return false;
}

}
#endif

// Template specializations for RGB and Gray images
template<>
CompressedImage::CompressedImage(const ImageBase<PixelRGB>& img, s32 quality)
{
  Compress(img, quality);
}

template<>
CompressedImage::CompressedImage(const ImageBase<u8>& img, s32 quality)
{
  Compress(img, quality);
}

template<class PixelType>
void CompressedImage::SetMetadata(const ImageBase<PixelType>& img)
{
  _timestamp   = img.GetTimestamp();
  _imageId     = img.GetImageId();
  _numRows     = img.GetNumRows();
  _numCols     = img.GetNumCols();
  _numChannels = img.GetNumChannels();
}

template<class PixelType>
const std::vector<u8>& CompressedImage::Compress(const ImageBase<PixelType>& img, s32 quality)
{
  _compressedBuffer.clear();

  SetMetadata(img);

#ifdef VICOS
  if(TurboJpegCompress(img, quality, _compressedBuffer))
  {
    return _compressedBuffer;
  }
#endif

  _uncompressedBuffer.resize(img.GetNumElements() * img.GetNumChannels());
  cv::Mat_<PixelType> mat(img.GetNumRows(),
                          img.GetNumCols(),
                          reinterpret_cast<PixelType*>(_uncompressedBuffer.data()));

  // Convert to BGR so that imencode works
  img.ConvertToShowableFormat(mat);

  const std::vector<int> compressionParams = {cv::IMWRITE_JPEG_QUALITY, quality};

  cv::imencode(".jpg", mat, _compressedBuffer, compressionParams);

  return _compressedBuffer;
}

template<>
bool CompressedImage::Decompress(ImageBase<PixelRGB>& img) const
{
  if(GetNumChannels() != 3)
  {
    PRINT_NAMED_WARNING("CompressedImage.RGBDecompress.NotRGB",
                        "Trying to decompress to rgb but original image was not rgb");
    return false;
  }

  auto mat = cv::imdecode(_compressedBuffer, cv::IMREAD_COLOR);
  img.SetFromShowableFormat(mat);
  
  return true;
}

template<>
bool CompressedImage::Decompress(ImageBase<u8>& img) const
{
  if(GetNumChannels() != 1)
  {
    PRINT_NAMED_WARNING("CompressedImage.GrayDecompress.NotGray",
                        "Trying to decompress to gray but original image was not gray");
    return false;
  }

  auto mat = cv::imdecode(_compressedBuffer, cv::IMREAD_GRAYSCALE);
  img.SetFromShowableFormat(mat);
  
  return true;
}


void CompressedImage::Display(const std::string& name) const
{
  //auto img = cv::imdecode(GetCompressedBuffer(),
  //                        (GetNumChannels() > 1 ? cv::IMREAD_COLOR : cv::IMREAD_GRAYSCALE));
 
}

}
}

