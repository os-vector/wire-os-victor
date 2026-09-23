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

#pragma once

#include <AK/Tools/Common/AkAssert.h>
#include <AK/SoundEngine/Common/AkTypes.h>
#include <android/log.h>

#include <time.h>
#include <stdlib.h>

#define AK_THREAD_INIT_CODE(_threadProperties) syscall(__NR_sched_setaffinity, 0, sizeof(_threadProperties.dwAffinityMask), &_threadProperties.dwAffinityMask)


namespace AKPLATFORM
{
	/// Platform Independent Helper
	inline void PerformanceFrequency( AkInt64 * out_piFreq )
	{
		// TO DO ANDROID ... is there something better
		*out_piFreq = CLOCKS_PER_SEC;
	}
}

#include <AK/Tools/POSIX/AkPlatformFuncs.h>

namespace AKPLATFORM
{

#ifndef AK_OPTIMIZED
	/// Output a debug message on the console (Ansi string)
	inline void OutputDebugMsg( const char* in_pszMsg )
	{
		// To see output of this 
		// adb logcat ActivityManager:I YourApplication:D AKDEBUG:D *:S
		__android_log_print(ANDROID_LOG_INFO, "AKDEBUG", "%s", in_pszMsg);	
	}	
#else	
	inline void OutputDebugMsg( const char* ){}
#endif
	// Atomic Operations
    // ------------------------------------------------------------------

	/// Platform Independent Helper
	inline AkInt32 AkInterlockedIncrement( AkAtomic32 * pValue )
	{
		return __atomic_add_fetch(pValue,1, __ATOMIC_SEQ_CST);
	}

	/// Platform Independent Helper
	inline AkInt32 AkInterlockedDecrement( AkAtomic32 * pValue )
	{
		return __atomic_sub_fetch(pValue,1, __ATOMIC_SEQ_CST);
	}

	inline bool AkInterlockedCompareExchange( volatile AkAtomic32* io_pDest, AkInt32 in_newValue, AkInt32 in_expectedOldVal )
	{
		return __atomic_compare_exchange(io_pDest, &in_expectedOldVal, &in_newValue, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	}
	
	inline bool AkInterlockedCompareExchange( volatile AkAtomic64* io_pDest, AkInt64 in_newValue, AkInt64 in_expectedOldVal)
	{
		return __atomic_compare_exchange(io_pDest, &in_expectedOldVal, &in_newValue, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	}

	inline void AkMemoryBarrier()
	{
		 __atomic_thread_fence(__ATOMIC_SEQ_CST);
		 /*
#ifdef AK_CPU_ARM
		__asm("DMB ST");	//Wait for stores to complete.
#elif AK_CPU_X86
		__asm("mfence");	
#endif*/
	}

    // Time functions
    // ------------------------------------------------------------------

	/// Platform Independent Helper
    inline void PerformanceCounter( AkInt64 * out_piLastTime )
	{
		*out_piLastTime = clock();
	}
	

	template<class destType, class srcType>
	inline size_t AkSimpleConvertString( destType* in_pdDest, const srcType* in_pSrc, size_t in_MaxSize, size_t destStrLen(const destType *),  size_t srcStrLen(const srcType *) )
	{ 
		size_t i;
		size_t lenToCopy = srcStrLen(in_pSrc);
		
		lenToCopy = (lenToCopy > in_MaxSize-1) ? in_MaxSize-1 : lenToCopy;
		for(i = 0; i < lenToCopy; i++)
		{
			in_pdDest[i] = (destType) in_pSrc[i];
		}
		in_pdDest[lenToCopy] = (destType)0;
		
		return lenToCopy;
	}

	#define CONVERT_UTF16_TO_CHAR( _astring_, _charstring_ ) \
		_charstring_ = (char*)AkAlloca( (1 + AKPLATFORM::AkUtf16StrLen((const AkUtf16*)_astring_)) * sizeof(char) ); \
		AK_UTF16_TO_CHAR( _charstring_, (const AkUtf16*)_astring_, AKPLATFORM::AkUtf16StrLen((const AkUtf16*)_astring_)+1 ) 

	#define AK_UTF16_TO_OSCHAR(	in_pdDest, in_pSrc, in_MaxSize )	AKPLATFORM::AkSimpleConvertString(	in_pdDest, in_pSrc, in_MaxSize, strlen, AKPLATFORM::AkUtf16StrLen )
	#define AK_UTF16_TO_CHAR(	in_pdDest, in_pSrc, in_MaxSize )	AKPLATFORM::AkSimpleConvertString(	in_pdDest, in_pSrc, in_MaxSize, strlen, AKPLATFORM::AkUtf16StrLen )
	#define AK_CHAR_TO_UTF16(	in_pdDest, in_pSrc, in_MaxSize )	AKPLATFORM::AkSimpleConvertString(	in_pdDest, in_pSrc, in_MaxSize, AKPLATFORM::AkUtf16StrLen, strlen)	
	#define AK_OSCHAR_TO_UTF16(	in_pdDest, in_pSrc, in_MaxSize )	AKPLATFORM::AkSimpleConvertString(	in_pdDest, in_pSrc, in_MaxSize, AKPLATFORM::AkUtf16StrLen, strlen)	
	
	/// Stack allocations.
	#define AkAlloca( _size_ ) __builtin_alloca( _size_ )

	/// Platform Independent Helper
	inline void AkCreateThread( 
		AkThreadRoutine pStartRoutine,					// Thread routine.
		void * pParams,									// Routine params.
		const AkThreadProperties & in_threadProperties,	// Properties. NULL for default.
		AkThread * out_pThread,							// Returned thread handle.
		const char * /*in_szThreadName*/ )				// Opt thread name.
	{
		AKASSERT( out_pThread != NULL );

		pthread_attr_t  attr;

		// Create the attr
		AKVERIFY(!pthread_attr_init(&attr));
		// Set the stack size
		AKVERIFY(!pthread_attr_setstacksize(&attr,in_threadProperties.uStackSize));

		AKVERIFY(!pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE));		

		// Create the tread
		int     threadError = pthread_create( out_pThread, &attr, pStartRoutine, pParams);
		AKASSERT( threadError == 0 );
		AKVERIFY(!pthread_attr_destroy(&attr));

		if( threadError != 0 )
		{
			AkClearThread( out_pThread );
			return;
		}

		// ::CreateThread() return NULL if it fails.
		if ( !*out_pThread )
		{
			AkClearThread( out_pThread );
			return;
		}		

		// Try to set the thread policy
		int sched_policy = in_threadProperties.uSchedPolicy;		

		// Get the priority for the policy
		int minPriority, maxPriority;
		minPriority = sched_get_priority_min(sched_policy);
		maxPriority = sched_get_priority_max(sched_policy);

		// Set the thread priority if valid
		sched_param schedParam;
		schedParam.sched_priority = in_threadProperties.nPriority;	
		AKASSERT( in_threadProperties.nPriority >= minPriority && in_threadProperties.nPriority <= maxPriority );		

		//pthread_setschedparam WILL fail on Android Lollipop when used with SCHED_FIFO (the default).  Not allowed anymore. (ignore the error code).
		int err = pthread_setschedparam(*out_pThread, sched_policy, &schedParam);
		if (err != 0)
		{			
			//Make sure the priority is well set, even if the policy could not.			
			sched_policy = SCHED_NORMAL;
			minPriority = sched_get_priority_min(sched_policy);
			maxPriority = sched_get_priority_max(sched_policy);
			if (in_threadProperties.nPriority == AK_THREAD_PRIORITY_ABOVE_NORMAL)
				schedParam.sched_priority = maxPriority;
			else if (in_threadProperties.nPriority == AK_THREAD_PRIORITY_BELOW_NORMAL)
				schedParam.sched_priority = minPriority;
			else
				schedParam.sched_priority = (maxPriority + minPriority) / 2;
			err = pthread_setschedparam(*out_pThread, sched_policy, &schedParam);						
			AKASSERT(err == 0);
		}
	}
}