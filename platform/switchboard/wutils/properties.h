/*
 * Copyright (C) 2006 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __WUTILS_PROPERTIES_H
#define __WUTILS_PROPERTIES_H

#include <sys/cdefs.h>
#include <stddef.h>
#include <stdint.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef __cplusplus
extern "C" {
#endif

/* System properties are *small* name value pairs managed by the
** property service.  If your data doesn't fit in the provided
** space it is not appropriate for a system property.
**
** WARNING: system/bionic/include/sys/system_properties.h also defines
**          these, but with different names.  (TODO: fix that)
*/
#define PROPERTY_KEY_MAX  60
#define PROPERTY_VALUE_MAX  60

int property_get(const char* key, char* value, const char* default_value)
{
  if (!key || !value) return 0;

  char cmd[256] = {0};
  snprintf(cmd, sizeof(cmd), "getprop %s", key);

  FILE* fp = popen(cmd, "r");
  if (!fp) {
    if (default_value) {
      strncpy(value, default_value, PROPERTY_VALUE_MAX - 1);
      value[PROPERTY_VALUE_MAX - 1] = 0;
      return strlen(value);
    }
    value[0] = 0;
    return 0;
  }

  if (!fgets(value, PROPERTY_VALUE_MAX, fp)) {
    pclose(fp);
    if (default_value) {
      strncpy(value, default_value, PROPERTY_VALUE_MAX - 1);
      value[PROPERTY_VALUE_MAX - 1] = 0;
      return strlen(value);
    }
    value[0] = 0;
    return 0;
  }

  pclose(fp);

  size_t len = strlen(value);
  while (len > 0 && (value[len - 1] == '\n' || value[len - 1] == '\r')) {
    value[--len] = 0;
  }

  if (len == 0 && default_value) {
    strncpy(value, default_value, PROPERTY_VALUE_MAX - 1);
    value[PROPERTY_VALUE_MAX - 1] = 0;
    return strlen(value);
  }

  return len;
}

int property_set(const char* key, const char* value)
{
  if (!key || !value) return -1;

  char cmd[512] = {0};
  snprintf(cmd, sizeof(cmd), "setprop %s \"%s\"", key, value);

  int rc = system(cmd);
  if (rc != 0) return -1;

  return 0;
}

#ifdef __cplusplus
}
#endif

#endif
