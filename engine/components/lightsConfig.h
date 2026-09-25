/**
 * File: lightsConfig.h
 *
 * Author: Raj-jyot
 *
 * Created: 21/09/2025
 *
 * Description:
 *   Choose backpack lights thing
 *
 * Copyright: Raj-jyot, 2025
 **/

#include <sys/stat.h>

namespace Anki {
namespace Vector {

  inline bool& _ankilights() {
    static bool initialized = false;
    static bool value = false;
    
    if (!initialized) {
      struct stat buffer;
      value = (stat("/data/data/enableankilights", &buffer) == 0);
      initialized = true;
    }
    
    return value;
  }


  inline bool& _userlights() {
    static bool initialized = false;
    static bool value = false;
    
    if (!initialized) {
      struct stat buffer;
      value = (stat("/data/data/customBackpackLights/cubeSpinner/purple/spinner_purple_celebration.json", &buffer) == 0);
      initialized = true;
    }
    
    return value;
  }

}
}
