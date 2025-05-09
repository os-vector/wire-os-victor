/*
 * File: soundbankBundleInfo.h
 *
 * Author: Jordan Rivas
 * Created: 04/24/17
 *
 *  Description: Struct defines soundbank bundle information provided in SoundbankBundleInfo.json file. This file is
 *               generated by build server script and is delivered to app with audio assets. Use this information to
 *               locate and load soundbanks.
 *
 * Copyright: Anki, Inc. 2017
 */

#ifndef __AnkiAudio_SoundbankBundleInfo_H__
#define __AnkiAudio_SoundbankBundleInfo_H__

#include <string>
#include <vector>


namespace Json {
class Value;
}

namespace Anki {
namespace AudioEngine {

// Soundbank Build Info
struct SoundbankBundleInfo {
  
  enum class BankType {
    Normal = 0, // Autoload
    LazyLoad,   // Load on demand
    Debug       // Only load for debug
  };
  
  std::string BundleName;
  std::string SoundbankName;
  std::string Language;
  std::string Path;
  BankType Type = BankType::Normal;
  
  SoundbankBundleInfo( const Json::Value& jsonData );
  
  // Static Helper Method
  static bool GetSoundbankBundleInfoMetadata( const std::string& filePath, std::vector<SoundbankBundleInfo>& out_list );
};

} // AudioEngine
} // Anki

#endif /* __AnkiAudio_SoundbankBundleInfo_H__ */
