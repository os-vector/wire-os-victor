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

// AkiOSSoundEngine.h

/// \file 
/// Main Sound Engine interface, specific iOS.

#ifndef _AK_IOS_SOUND_ENGINE_H_
#define _AK_IOS_SOUND_ENGINE_H_

#include <AK/SoundEngine/Common/AkTypes.h>
#include <AK/Tools/Common/AkPlatformFuncs.h>

#include <AudioToolbox/AudioToolbox.h>

/// The IDs of the iOS audio session categories, useful for defining app-level audio behaviours such as inter-app audio mixing policies and audio routing behaviours. These IDs are funtionally equivalent to the corresponding constants defined by the iOS audio session service backend (AVAudioSession). Refer to Xcode documentation for details on the audio session categories. The original prefix "AV" is replaced with "Ak" for the ID names.
///
/// \remark An audio session category may demand a fixed value for a certain category options (AkAudioSessionCategoryOptions) depending on the underlying audio session services. Ensure that AkPlatformInitSettings.audioSession.eCategory and AkPlatformInitSettings.audioSession.eCategoryOptions be compatible with each other. If conflicts are detected between the user-specified audio sessoin category and category options, the sound engine will try to fix them during the initialization according to the underlying policies defined by the audio services. The policies are:
/// - The audio session category AkAudioSessionCategory_AmbientSound forbid the use of the category options: AkAudioSessionCategoryOptionAllowBluetooth and AkAudioSessionCategoryOptionDefaultToSpeaker.
/// - AkAudioSessionCategory_SoloAmbientSound forbid the use of the category options: AkAudioSessionCategoryOptionAllowBluetooth and AkAudioSessionCategoryOptionDefaultToSpeaker.
///
/// \sa
/// - \ref AkPlatformInitSettings
/// - \ref AkAudioSessionCategoryOptions
/// - \ref AkAudioSessionProperties
enum AkAudioSessionCategory
{
	AkAudioSessionCategoryAmbient, ///< Audio session category corresponding to the AVAudiosession's AVAudioSessionCategoryAmbient constant
	AkAudioSessionCategorySoloAmbient, ///< Audio session category corresponding to the AVAudiosession's AVAudioSessionCategorySoloAmbient constant
	AkAudioSessionCategoryPlayAndRecord ///< Audio session category corresponding to the AVAudiosession's AVAudioSessionCategoryPlayAndRecord constant
};

/// The IDs of the iOS audio session category options, used for customizing the audio session category features. These IDs are funtionally equivalent to the corresponding constants defined by the iOS audio session service backend (AVAudioSession). Refer to Xcode documentation for details on the audio session category options. The original prefix "AV" is replaced with "Ak" for the ID names.
///
/// \sa
/// - \ref AkPlatformInitSettings
/// - \ref AkAudioSessionCategory
/// - \ref AkAudioSessionProperties
enum AkAudioSessionCategoryOptions
{
	AkAudioSessionCategoryOptionMixWithOthers  = 1, ///< Audio session category option corresponding to the AVAudiosession's AVAudioSessionCategoryOptionMixWithOthers constant
	AkAudioSessionCategoryOptionDuckOthers  = 2, ///< Audio session category option corresponding to the AVAudiosession's AVAudioSessionCategoryOptionDuckOthers constant
	AkAudioSessionCategoryOptionAllowBluetooth  = 4, ///< Audio session category option corresponding to the AVAudiosession's AVAudioSessionCategoryOptionAllowBluetooth constant
	AkAudioSessionCategoryOptionDefaultToSpeaker  = 8 ///< Audio session category option corresponding to the AVAudiosession's AVAudioSessionCategoryOptionDefaultToSpeaker constant
};

/// The IDs of the iOS audio session modes, used for customizing the audio session for typical app types. These IDs are funtionally equivalent to the corresponding constants defined by the iOS audio session service backend (AVAudioSession). Refer to Xcode documentation for details on the audio session category options. The original prefix "AV" is replaced with "Ak" for the ID names.
///
/// \sa
/// - \ref AkPlatformInitSettings
/// - \ref AkAudioSessionProperties
enum AkAudioSessionMode
{
	AkAudioSessionModeDefault, ///< Audio session mode corresponding to the AVAudiosession's AVAudioSessionModeDefault constant
	AkAudioSessionModeVoiceChat, ///< Audio session mode corresponding to the AVAudiosession's AVAudioSessionModeVoiceChat constant
	AkAudioSessionModeGameChat, ///< Audio session mode corresponding to the AVAudiosession's AVAudioSessionModeGameChat constant
	AkAudioSessionModeVideoRecording, ///< Audio session mode corresponding to the AVAudiosession's AVAudioSessionModeVideoRecording constant
	AkAudioSessionModeMeasurement, ///< Audio session mode corresponding to the AVAudiosession's AVAudioSessionModeMeasurement constant
	AkAudioSessionModeMoviePlayback, ///< Audio session mode corresponding to the AVAudiosession's AVAudioSessionModeMoviePlayback constant
	AkAudioSessionModeVideoChat ///< Audio session mode corresponding to the AVAudiosession's AVAudioSessionModeMoviePlayback constant
};

/// The behaviour flags for when iOS audio session is activated. These IDs are funtionally equivalent to the corresponding constants defined by the iOS audio session service backend (AVAudioSession). Refer to Xcode documentation for details on the audio session category options. The original prefix "AV" is replaced with "Ak" for the ID names.
///
/// \sa
/// - \ref AkPlatformInitSettings
/// - \ref AkAudioSessionProperties
enum AkAudioSessionSetActiveOptions
{
	 AkAudioSessionSetActiveOptionNotifyOthersOnDeactivation  = 1 ///< Audio session activation option corresponding to the AVAudiosession's AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation constant
};

/// The API structure used with AkPlatformInitSettings for specifying iOS audio session for the sound engine.
///
/// \sa
/// - \ref AkPlatformInitSettings
/// - \ref AkAudioSessionCategory
/// - \ref AkAudioSessionCategoryOptions
/// - \ref AkAudioSessionMode
/// - \ref AkAudioSessionSetActiveOptions
struct AkAudioSessionProperties
{
	AkAudioSessionCategory eCategory; ///< \sa AkAudioSessionCategory
	AkAudioSessionCategoryOptions eCategoryOptions; ///< \sa AkAudioSessionCategoryOptions
	AkAudioSessionMode eMode; ///< \sa AkAudioSessionMode
	AkAudioSessionSetActiveOptions eSetActivateOptions; ///< \sa AkAudioSessionSetActiveOptions
};

namespace AK
{
	namespace SoundEngine
	{
		namespace iOS
		{
			/// iOS-only callback function prototype used for audio input source plugin. Implement this function to transfer the 
			/// input sample data to the sound engine and perform brief custom processing.
			/// \remark See the remarks of \ref AkGlobalCallbackFunc.
			///
			/// \sa
			/// - \ref AkPlatformInitSettings
			typedef AKRESULT ( * AudioInputCallbackFunc )(
				AudioBufferList* io_Data,	///< An exposed CoreAudio structure that holds the input audio samples generated from 
											///< audio input hardware. The buffer is pre-allocated by the sound engine and the buffer 
											///< size can be obtained from the structure. Refer to the microphone demo of the IntegrationDemo for an example of usage.
				void* in_pCookie ///< User-provided data, e.g., a user structure.
				);

			/// iOS-only callback function prototype used for handling audio session interruption. It is mandatory to implement 
			/// this callback to respond to audio interruptions such as phone calls or alarms according to the application logic. 
			/// The examples of such responses include calling relevant sound engine API to suspend the device or wake up the 
			/// device from suspend, and enabling certain UI elements when an interruption happens.
			/// \remark 
			/// - Under interruptible (non-mixable) audio session categories, the app needs to respond to audio interruptions such as phone calls, alarms, or the built-in music player control from various remote control interfaces, according to app's own policy. Such a policy may include pausing and resuming the sound engine pipeline, pausing and resuming the entire game, and updating UI elements either as the feedback to users about the interruption status, or as a means for the users to restore the audio manually if the application requires the user intervention.
			/// - There is no need to call AK::SoundEngine::Suspend() and AK::SoundEngine::WakeupFromSuspend() in this callback. The sound engine call them internally depending on the interruption status.
			/// - When in_bEnterInterruption is true, this callback is called before the sound engine calls AK::SoundEngine::Suspend(), where user can take actions to prepare for the suspend, e.g., posting global pause events or switching to a special user interface; when in_bEnterInterruption is false, this callback is called after the sound engine calls AK::SoundEngine::WakeFromSuspend() and only when waking up succeeds, so that user can restore suspended resources, e.g., post global resume events or switching back to default user interface. This is useful for games in which audio is essential to the gameplay.
			/// - Under the AkAudioSessionCategorySoloAmbient audio session category, to avoid remote-control-related audio loss, any pause event posted in this callback when entering interruption needs to be paired with a resume event in the AudioSourceChangeCallbackFunc's conditional branch for when other app's audio is not playing. This is because by Apple's design, under this category, the end of interruption will trigger the source change callback instead of the interruption callback if the interruption source is the user music.
			/// - Starting from iOS 7, under the AkAudioSessionCategorySoloAmbient audio session category, this callback will be triggered with an end-of-interruption flag when app returns to the foreground from previous backgrounding, e.g., after a home-screen-and-back cycle, or a locking-unlocking cycle. It is triggered after the applicationDidBecomeActive: call. This means that the background music decisions made within applicationDidBecomeActive: can be overriden by the interruption callback.
			///
			/// \sa
			/// - \ref ListenToAudioSessionInterruption
			/// - \ref AkGlobalCallbackFunc
			/// - \ref AkPlatformInitSettings
			/// - \ref Suspend
			/// - \ref WakeupFromSuspend
			typedef AKRESULT ( * AudioInterruptionCallbackFunc )(
				bool in_bEnterInterruption,	///< Indicating whether or not an interruption is about to start (e.g., an incoming 
											///< call is received) or end (e.g., the incoming call is dismissed).

				void* in_pCookie ///< User-provided data, e.g., a user structure.
				);
		}
	}
}

/// The API structure used for specifying all iOS-specific callback functions and user data from the app side.
///
/// \sa
/// - \ref AkPlatformInitSettings
struct AkAudioCallbacks
{
	AK::SoundEngine::iOS::AudioInputCallbackFunc inputCallback;					///< Application-defined audio input callback function
	void* inputCallbackCookie;													///< Application-defined user data for the audio input callback function
	AK::SoundEngine::iOS::AudioInterruptionCallbackFunc interruptionCallback;	///< Application-defined audio interruption callback function
	void* interruptionCallbackCookie;											///< Application-defined user data for the audio interruption callback function
};

/// Platform specific initialization settings
/// \sa AK::SoundEngine::Init
/// \sa AK::SoundEngine::GetDefaultPlatformInitSettings
/// - \ref soundengine_initialization_advanced_soundengine_using_memory_threshold
/// - \ref AK::SoundEngine::iOS::AudioSessionCategory
struct AkPlatformInitSettings
{
	// Threading model.
    AkThreadProperties  threadLEngine;			///< Lower engine threading properties
	AkThreadProperties  threadBankManager;		///< Bank manager threading properties (its default priority is AK_THREAD_PRIORITY_NORMAL)
	AkThreadProperties  threadMonitor;			///< Monitor threading properties (its default priority is AK_THREAD_PRIORITY_ABOVENORMAL). This parameter is not used in Release build.
	
    // Memory.
	AkReal32            fLEngineDefaultPoolRatioThreshold;	///< 0.0f to 1.0f value: The percentage of occupied memory where the 
															///< sound engine should enter in Low memory mode. \ref soundengine_initialization_advanced_soundengine_using_memory_threshold
	AkUInt32            uLEngineDefaultPoolSize;///< Lower Engine default memory pool size
	AkUInt32			uSampleRate;			///< Sampling Rate. Default 48000 Hz
	// Voices.
	AkUInt16            uNumRefillsInVoice;		///< Number of refill buffers in voice buffer. 2 == double-buffered, defaults to 4
	AkAudioSessionProperties audioSession;	///< iOS audio session properties 
	AkAudioCallbacks audioCallbacks; ///< iOS audio callbacks
};


#endif //_AK_IOS_SOUND_ENGINE_H_
