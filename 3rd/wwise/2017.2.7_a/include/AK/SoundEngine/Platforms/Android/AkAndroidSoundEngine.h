/*******************************************************************************
The content of this file includes portions of the AUDIOKINETIC Wwise Technology
released in source code form as part of the SDK installer package.

Commercial License Usage

Licensees holding valid commercial licenses to the AUDIOKINETIC Wwise Technology
may use this file in accordance with the end user license agreement provided 
with the software or, alternatively, in accordance with the terms contained in a
written agreement between you and Audiokinetic Inc.

Apache License Usage

Alternatively, this file may be used under the Apache License, Version 2.0 (the 
"Apache License"); you may not use this file except in compliance with the 
Apache License. You may obtain a copy of the Apache License at 
http://www.apache.org/licenses/LICENSE-2.0.

Unless required by applicable law or agreed to in writing, software distributed
under the Apache License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES
OR CONDITIONS OF ANY KIND, either express or implied. See the Apache License for
the specific language governing permissions and limitations under the License.

  Version: v2017.2.7  Build: 6581
  Copyright (c) 2006-2018 Audiokinetic Inc.
*******************************************************************************/

// AkAndroidSoundEngine.h

/// \file 
/// Main Sound Engine interface, specific Android.

#pragma once

#include <AK/SoundEngine/Common/AkTypes.h>
#include <AK/Tools/Common/AkPlatformFuncs.h>
#include <SLES/OpenSLES.h>
#include "SLES/OpenSLES_Android.h"
#include <jni.h>


/// Platform specific initialization settings
/// \sa AK::SoundEngine::Init
/// \sa AK::SoundEngine::GetDefaultPlatformInitSettings
/// - \ref soundengine_initialization_advanced_soundengine_using_memory_threshold
struct AkPlatformInitSettings
{
	// Threading model.
    AkThreadProperties  threadLEngine;			///< Lower engine threading properties
	AkThreadProperties  threadBankManager;		///< Bank manager threading properties (its default priority is AK_THREAD_PRIORITY_NORMAL)
	AkThreadProperties  threadMonitor;			///< Monitor threading properties (its default priority is AK_THREAD_PRIORITY_ABOVENORMAL). This parameter is not used in Release build.	
	
    // Memory.
	AkReal32            fLEngineDefaultPoolRatioThreshold;	///< 0.0f to 1.0f value: The percentage of occupied memory where the sound engine should enter in Low memory mode. \ref soundengine_initialization_advanced_soundengine_using_memory_threshold
	AkUInt32            uLEngineDefaultPoolSize;///< Lower Engine default memory pool size

	AkUInt32			uSampleRate;			///< Sampling Rate.  Set to 0 to get the native sample rate.  Default value is 0.
	AkUInt16            uNumRefillsInVoice;		///< Number of refill buffers in voice buffer.  Defaults to 4.
	AkChannelMask		uChannelMask;			///< use AK_SPEAKER_SETUP_STEREO
	bool				bRoundFrameSizeToHWSize;///< Used when hardware-preferred frame size and user-preferred frame size (AkInitSettings.uNumSamplesPerFrame) are not compatible.  
												/// If true (default) the sound engine will initialize to a multiple of the HW setting, close to the user setting.
												/// If false, the user setting is used as is, regardless of the HW preference (might incur a performance hit).

	SLObjectItf			pSLEngine;				///< OpenSL engine reference for sharing between various audio components.
	JavaVM*				pJavaVM;				///< Active JavaVM for the app, used for internal system calls.  Usually provided through the android_app structure given at startup or the NativeActivity. This parameter needs to be set to allow the sound engine initialization.
	jobject				jNativeActivity;		///< NativeActivity instance for this application. Usually provided through the android_app structure, or through other means if your application has an overridden activity.
												///< This is optional.  However, not providing this object will prevent the background music muting when player start his own music in an external player.
};

struct AkInitSettings;

namespace AK
{
	namespace SoundEngine
	{
		/// Get instance of OpenSL created by the sound engine at initialization.
		/// \return NULL if sound engine is not initialized
		AK_EXTERNAPIFUNC( SLObjectItf, GetWwiseOpenSLInterface )();

		/// Gets specific settings for the fast audio path on Android.  Call this function after AK::SoundEngine::GetDefaultSettings and AK::SoundEngine::GetPlatformDefaultSettings to modify settings for the fast path.
		/// in_pfSettings.pJavaVM and in_pfSettings.jNativeActivity must be filled properly prior to calling GetFastPathSettings.
		/// The fast path constraints are:
		/// -The sample rate must match the hardware native sample rate 
		/// -The number of samples per frame must be a multiple of the hardware buffer size.
		/// Not fulfilling these constraints makes the audio hardware less efficient.
		/// In general, using the fast path means a higher CPU usage.  Complex audio designs may not be feasible while using the fast path.
		AKRESULT GetFastPathSettings(AkInitSettings &in_settings, AkPlatformInitSettings &in_pfSettings);
	};
};