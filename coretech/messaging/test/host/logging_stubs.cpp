/**
 * File: logging_stubs.cpp
 *
 * Description: Host-test stand-ins for the Anki::CoreTech logging entry
 * points. The real coretech/common/shared/logging.cpp forwards into the full
 * Anki::Util logging stack (logger providers, string tables, console system),
 * none of which is under test here. These stubs print to stderr so failing
 * socket tests still show the firmware's own diagnostics.
 *
 * Same pattern as coretech/planning/test/host/cv_stubs.cpp.
 */

#include "coretech/common/shared/logging.h"

#include <cstdarg>
#include <cstdio>

namespace {

void Print(const char* level, const char* name, const char* format, va_list args)
{
  fprintf(stderr, "[%s] %s: ", level, name);
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
}

} // anonymous namespace

namespace Anki {
namespace CoreTech {

void LogError(const char* name, const char* format, ...)
{
  va_list args;
  va_start(args, format);
  Print("ERROR", name, format, args);
  va_end(args);
}

void LogWarning(const char* name, const char* format, ...)
{
  va_list args;
  va_start(args, format);
  Print("WARN", name, format, args);
  va_end(args);
}

void LogInfo(const char* channel, const char* name, const char* format, ...)
{
  (void)channel;
  va_list args;
  va_start(args, format);
  Print("INFO", name, format, args);
  va_end(args);
}

void LogDebug(const char* channel, const char* name, const char* format, ...)
{
  (void)channel;
  va_list args;
  va_start(args, format);
  Print("DEBUG", name, format, args);
  va_end(args);
}

} // namespace CoreTech
} // namespace Anki
