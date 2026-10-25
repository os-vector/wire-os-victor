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
#include "sjpeg.h"
#endif

namespace Anki {
namespace Vision {

#ifdef VICOS
namespace {

constexpr int kSjpegNumThreads = 2;

thread_local cv::Mat s_rgbScratch;

sjpeg::EncoderParam MakeSjpegParam(const s32 quality, const SjpegYUVMode yuvMode)
{
  sjpeg::EncoderParam param(static_cast<float>(quality));
  param.yuv_mode = yuvMode;
  param.Huffman_compress = false;
  param.adaptive_quantization = false;
  param.num_threads = kSjpegNumThreads;
  return param;
}

bool SjpegCompress(const ImageBase<PixelRGB>& img, const s32 quality, std::vector<u8>& compressedBuffer)
{
  const cv::Mat* mat = &img.get_CvMat_();
  if(mat->empty())
  {
    return false;
  }

  if(Vector::IsXray())
  {
    cv::cvtColor(*mat, s_rgbScratch, cv::COLOR_BGR2RGB);
    mat = &s_rgbScratch;
  }

  const std::shared_ptr<sjpeg::ByteSink> sink = sjpeg::MakeByteSink(&compressedBuffer);
  if(!sink || !sjpeg::Encode(mat->data, mat->cols, mat->rows, static_cast<int>(mat->step[0]),
                             MakeSjpegParam(quality, SJPEG_YUV_420), sink.get()))
  {
    PRINT_NAMED_WARNING("CompressedImage.SjpegCompress.RGBFailed", "%dx%d", mat->cols, mat->rows);
    compressedBuffer.clear();
    return false;
  }
  return true;
}

bool SjpegCompress(const ImageBase<u8>& img, const s32 quality, std::vector<u8>& compressedBuffer)
{
  const cv::Mat& mat = img.get_CvMat_();
  if(mat.empty())
  {
    return false;
  }

  const std::shared_ptr<sjpeg::ByteSink> sink = sjpeg::MakeByteSink(&compressedBuffer);
  if(!sink || !sjpeg::EncodeGray(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step[0]),
                                 MakeSjpegParam(quality, SJPEG_YUV_400), sink.get()))
  {
    PRINT_NAMED_WARNING("CompressedImage.SjpegCompress.GrayFailed", "%dx%d", mat.cols, mat.rows);
    compressedBuffer.clear();
    return false;
  }
  return true;
}

template<class PixelType>
bool SjpegCompress(const ImageBase<PixelType>&, const s32, std::vector<u8>&)
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
  if(SjpegCompress(img, quality, _compressedBuffer))
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

