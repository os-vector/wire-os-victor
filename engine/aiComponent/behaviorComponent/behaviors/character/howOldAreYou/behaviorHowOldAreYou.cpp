/**
 * File: BehaviorHowOldAreYou.cpp
 *
 * Author: Andrew Stout
 * Created: 2018-11-13
 *
 * Description: Responds to How Old Are You character voice intent.
 *
 * Copyright: Anki, Inc. 2018
 *
 **/


#include "engine/aiComponent/behaviorComponent/behaviors/character/howOldAreYou/behaviorHowOldAreYou.h"

#include "engine/aiComponent/behaviorComponent/behaviorContainer.h"
#include "engine/aiComponent/behaviorComponent/behaviors/onboarding/behaviorOnboardingCoordinator.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/components/localeComponent.h"
#include "engine/components/robotStatsTracker.h"
#include "engine/cozmoContext.h"


#define LOG_CHANNEL "BehaviorHowOldAreYou"

namespace {
  constexpr const float kMonthsThresh_days = 30.436875; // days at which we start counting in months instead
  constexpr const float kYearsThresh_months = 12; // days at which we start counting in years instead

  // Localization keys
  constexpr const char * kOneDayKey = "BehaviorHowOldAreYou.OneDay";
  constexpr const char * kSomeDaysKey = "BehaviorHowOldAreYou.SomeDays";
  constexpr const char * kOneMonthKey = "BehaviorHowOldAreYou.OneMonth";
  constexpr const char * kAndOneMonthKey = "BehaviorHowOldAreYou.AndOneMonth";
  constexpr const char * kSomeMonthsKey = "BehaviorHowOldAreYou.SomeMonths";
  constexpr const char * kAndSomeMonthsKey = "BehaviorHowOldAreYou.AndSomeMonths";
  constexpr const char * kOneYearKey = "BehaviorHowOldAreYou.OneYear";
  constexpr const char * kOneYearAndMonthKey = "BehaviorHowOldAreYou.OneYearAndMonth";
  constexpr const char * kSomeYearsKey = "BehaviorHowOldAreYou.SomeYears";
  constexpr const char * kSomeYearsAndMonthKey = "BehaviorHowOldAreYou.SomeYearsAndMonth";

}

namespace Anki {
namespace Vector {

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorHowOldAreYou::InstanceConfig::InstanceConfig()
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorHowOldAreYou::DynamicVariables::DynamicVariables():
    robotAge_hours(-1)
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorHowOldAreYou::BehaviorHowOldAreYou(const Json::Value& config)
 : ICozmoBehavior(config)
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorHowOldAreYou::~BehaviorHowOldAreYou()
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorHowOldAreYou::WantsToBeActivatedBehavior() const
{
  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorHowOldAreYou::GetBehaviorOperationModifiers(BehaviorOperationModifiers& modifiers) const
{
  modifiers.wantsToBeActivatedWhenOffTreads = true;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorHowOldAreYou::GetAllDelegates(std::set<IBehavior*>& delegates) const
{
  delegates.insert( _iConfig.countingAnimationBehavior.get() );
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorHowOldAreYou::GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const
{
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorHowOldAreYou::InitBehavior()
{
  const BehaviorContainer& container = GetBEI().GetBehaviorContainer();
  container.FindBehaviorByIDAndDowncast<BehaviorCountingAnimation>( BEHAVIOR_ID(HowOldAreYouCounting),
                                                                    BEHAVIOR_CLASS(CountingAnimation),
                                                                    _iConfig.countingAnimationBehavior );
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorHowOldAreYou::OnBehaviorActivated()
{
  // reset dynamic variables
  _dVars = DynamicVariables();

  // the behavior is active now, time to do something!

  // get age
  _dVars.robotAge_hours = GetRobotAge();
  // round age
  const auto & presentable = PresentableAgeFromHours(_dVars.robotAge_hours);

  LOG_INFO("BehaviorHowOldAreYou.OnBehaviorActivated.PresentableAge",
           "Robot presentable age: %d %s",
           presentable.first,
           presentable.second.c_str());

  // delegate to counting animation
  _iConfig.countingAnimationBehavior->SetCountTarget(presentable.first, presentable.second);
  if ( _iConfig.countingAnimationBehavior->WantsToBeActivated() ) {
    DelegateIfInControl( _iConfig.countingAnimationBehavior.get() );
  }

}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::chrono::hours BehaviorHowOldAreYou::GetRobotAge()
{
  // This used to use the onboarding completion time for date, when authing the dev bot to wp this gets overwritten so instead use when /data/persist was created
  // check whether onboardingState file exists
  // const auto* platform = GetBEI().GetRobotInfo().GetContext()->GetDataPlatform();
  // _saveFolder = "/data/vic-gateway";
  // saveFolder = Util::FileUtils::AddTrailingFileSeparator( _saveFolder );
  // const kGatewayCertFilePath = kGatewayCertFolder + kGatewayCertFile;
  // if( Util::FileUtils::DirectoryExists( kGatewayCertFolder ) && Util::FileUtils::FileExists( kGatewayCertFile ) ) {

    // file exists
    // const std::string & file_s = Util::FileUtils::ReadFile(onboardingDataFilePath);
    // gotta parse the file
    // Json::Value file_j;
    // Json::Reader reader;
    // bool parsed = false;
    // bool containsBoD = false;
    // if (parsed) {
    //   LOG_DEBUG("BehaviorHowOldAreYou.GetRobotAge.ParsedJson", "");
    // } else {
    //   LOG_WARNING("BehaviorHowOldAreYou.GetRobotAge.JsonParsingFailed",
    //       "%s exists but failed to parse as Json", onboardingDataFilePath.c_str());
    // }

    int64 onboardingTime_sse;
    // if (parsed) {
      // check whether born on date is written to file
    //   containsBoD = file_j.isMember(BehaviorOnboardingCoordinator::kOnboardingTimeKey);
    //   if (containsBoD) {
        // if so, use that
    //     onboardingTime_sse = file_j[BehaviorOnboardingCoordinator::kOnboardingTimeKey].asInt64();
    //     LOG_INFO("BehaviorHowOldAreYou.GetRobotAge.ReadOnboardingTime",
    //         "Read onboarding time (seconds since epoch): %lld", onboardingTime_sse);
    //   } else {
        // INFO because plenty of robots have onboarded before we introduce the change that writes this value to file.
    //     LOG_INFO("BehaviorHowOldAreYou.GetRobotAge.NoOnboardingTime",
    //         "%s not a member of onboarding file",
    //         BehaviorOnboardingCoordinator::kOnboardingTimeKey.c_str());
    //   }
    // }

    // if(!parsed || !containsBoD) {
      // if not we couldn't get born on date from file, use modification time of the file
      onboardingTime_sse = Util::FileUtils::GetFileLastModificationTime( "/data/persist" ); // seconds since the epoch
      LOG_INFO("BehaviorHowOldAreYou.GetRobotAge.ModificationTimeFallback",
          "Using file modification time of vic-gateway certificate (seconds since epoch): %lld",
          onboardingTime_sse);
    // }

    // convert seconds since the epoch to age in hours
    // make a duration, in seconds; make a system_clock timepoint from that duration--i.e., that many seconds since the epoch
    const auto onboarding_tp = std::chrono::system_clock::time_point( std::chrono::seconds(onboardingTime_sse) );
    // subtract onboarding_tp from now to get a duration--the time from onboarding time until now
    const auto onboarding_dsc = std::chrono::system_clock::now() - onboarding_tp;
    // convert that duration to the units we want--hours, in this case. We're totally cool with losing precision.
    const std::chrono::hours robotAge_dh = std::chrono::duration_cast<std::chrono::hours>(onboarding_dsc);
    LOG_INFO("BehaviorHowOldAreYou.GetRobotAge.ComputedRobotAgeFromOnboarding",
                "robot age from onboarding time: %ld hours", robotAge_dh.count());
    return robotAge_dh;

  // } else {

    // onboarding save file does not exist; fall back on RobotStatsTracker as done above
    // WARNING because it's really expected that this file exists by the time we get here
    // LOG_WARNING("BehaviorHowOldAreYou.GetRobotAge.NoOnboardingFallback",
    //     "no onboarding data found at %s. Falling back on RobotStatsTracker.");
  //   const auto& rst = GetBehaviorComp<RobotStatsTracker>();
  //   const float robotAge_h = rst.GetNumHoursAlive();
  //   return std::chrono::hours{static_cast<int>(robotAge_h)};
  // }

}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorHowOldAreYou::PresentableAge BehaviorHowOldAreYou::PresentableAgeFromHours(std::chrono::hours age_hours)
{
  // Handy types that aren't built in until C++20
  using duration_days = std::chrono::duration<int, std::ratio<86400> >;
  using duration_months = std::chrono::duration<int, std::ratio<2629746> >;
  using duration_years = std::chrono::duration<int, std::ratio<31556952>>;

  // translate hours to days
  // we're cool with loss of precision
  const duration_days age_days = std::chrono::duration_cast<duration_days>(age_hours);
  const int count_days = age_days.count();

  // translate days to months
  // loss of precision is basically the point
  const duration_months age_months = std::chrono::duration_cast<duration_months>(age_days);
  const int count_months = age_months.count();

  // Translate months to years
  // Precision: 0 I'm gussing
  const duration_years age_years = std::chrono::duration_cast<duration_years>(age_months);
  const int count_years = age_years.count();

  const int remaining_months = count_months - (count_years * 12);

  //
  // Use locale component to get localized version of announcement string.
  // This is basically a four-way switch between "one day", "N days", "one month", and "N months".
  // If we're less than kMonthsThresh_days days, use days, else translate to months.
  // And if we're less than kYearsThresh_months, use months, otherwise use years. -- Raj-jyot / Switch_modder
  //
  // Note: current implementation (at Design's request) is to return the floor:
  // i.e., round everything down until we get to a whole day: 47 hours -> 1 day, 49 hours -> 2 days
  //
  // Note-2: We can't really "return the floor" past one year so the above will be neglected for when the age is over one year  -- Raj-jyot / Switch_modder
  //
  const auto & localeComponent = GetBEI().GetRobotInfo().GetLocaleComponent();

  if (count_days == 1) {
    return PresentableAge(count_days, localeComponent.GetString(kOneDayKey));
  } else if (count_days < kMonthsThresh_days) {
    return PresentableAge(count_days, localeComponent.GetString(kSomeDaysKey, std::to_string(count_days)));
  } else if (count_months == 1) {
    return PresentableAge(count_months, localeComponent.GetString(kOneMonthKey));
  } else if (count_months < kYearsThresh_months) {
    return PresentableAge(count_months, localeComponent.GetString(kSomeMonthsKey, std::to_string(count_months)));
  } else {
    std::string ageString;

    if (count_years == 1 && remaining_months <= 0) {
      ageString = localeComponent.GetString(kOneYearKey);
    } else if (count_years == 1 && remaining_months == 1) {
      ageString = localeComponent.GetString(kOneYearAndMonthKey);
    } else if (count_years == 1 && remaining_months > 1) {
      ageString = localeComponent.GetString(kOneYearAndMonthKey);
    } else if (count_years == 1 && remaining_months > 0 && count_months < 12) {
      ageString = localeComponent.GetString(kOneYearAndMonthKey);
    } else if (count_years > 1 && remaining_months == 0) {
      ageString = localeComponent.GetString(kSomeYearsKey, std::to_string(count_years));
    } else if (count_years > 1 && remaining_months > 0) {
      ageString = localeComponent.GetString(kSomeYearsAndMonthKey, std::to_string(count_years));
    }

    if ((remaining_months > 0)) {
      if (remaining_months == 1) {
        ageString += localeComponent.GetString(kAndOneMonthKey);
      } else {
        ageString += localeComponent.GetString(kAndSomeMonthsKey, std::to_string(remaining_months));
      }
    }

    return PresentableAge(count_months, ageString);
  }
}


} // namespace Vector
} // namespace Anki
