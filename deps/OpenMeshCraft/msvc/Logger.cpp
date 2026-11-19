#include "Logger.h"

#include <iostream>

#ifdef _MSC_VER
// Include Windows headers outside namespace OMC. On ARM64 the SDK declares
// _Interlocked* in the current namespace, and winbase.h then fails to match
// those declarations when this include sits inside namespace OMC.
#  include <windows.h>
#  include <psapi.h>
#endif

namespace OMC {

void Logger::trace(OMC_UNUSED const std::string &msg)
{
#ifdef OMC_LOG_TRACE
  std::cout << OMC::format("[trace]: {}\n", msg);
#endif
}

void Logger::debug(OMC_UNUSED const std::string &msg)
{
#ifdef OMC_LOG_DEBUG
  if (console_level <= Level::DEBUG)
    std::cout << OMC::format("[debug]: {}\n", msg);
#endif
}

void Logger::info(OMC_UNUSED const std::string &msg)
{
#if defined(OMC_LOG_DEBUG) || defined(OMC_LOG_INFO)
  std::cout << OMC::format("[info]: {}\n", msg);
#endif
}

void Logger::warn(OMC_UNUSED const std::string &msg)
{
#if defined(OMC_LOG_DEBUG) || defined(OMC_LOG_INFO) || defined(OMC_LOG_WARN)
  std::cout << OMC::format("[warn]: {}\n", msg);
#endif
}

void Logger::fatal(OMC_UNUSED const std::string &msg)
{
#if defined(OMC_LOG_DEBUG) || defined(OMC_LOG_INFO) || \
  defined(OMC_LOG_WARN) || defined(OMC_LOG_FATAL)
  std::cout << OMC::format("[fatal]: {}\n", msg);
#endif
}

std::chrono::steady_clock::time_point Logger::elapse_reset()
{
  return std::chrono::steady_clock::now();
}

std::chrono::duration<double>
Logger::elapsed(const std::chrono::steady_clock::time_point &last)
{
  auto now = std ::chrono::steady_clock::now();
  return now - last;
}

#ifdef _MSC_VER

// To ensure correct resolution of symbols, add Psapi.lib to TARGETLIBS
// and compile with -DPSAPI_VERSION=1

double getPeakMegabytesUsed()
{
  HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                FALSE, GetCurrentProcessId());
  if (NULL == hProcess)
    return 0;

  PROCESS_MEMORY_COUNTERS pmc;
  double                  mem = 0;

  if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc)))
  {
    mem = pmc.PeakWorkingSetSize / 1048576.0;
  }

  CloseHandle(hProcess);
  return mem;
}

double getMegabytesUsed()
{
  HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                FALSE, GetCurrentProcessId());
  if (NULL == hProcess)
    return 0;

  PROCESS_MEMORY_COUNTERS pmc;
  double                  mem = 0;
  if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc)))
  {
    mem = pmc.WorkingSetSize / 1048576.0;
  }

  CloseHandle(hProcess);
  return mem;
}

#else

double getPeakMegabytesUsed() { return 0.0; }
double getMegabytesUsed() { return 0.0; }

#endif

} // namespace OMC
