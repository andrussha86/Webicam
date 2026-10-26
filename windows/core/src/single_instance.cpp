#include "webicam/single_instance.h"

namespace webicam {

std::optional<HANDLE> AcquireSingleInstance(const wchar_t* name) {
  HANDLE mutex = CreateMutexW(nullptr, TRUE, name);
  if (!mutex) return std::nullopt;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(mutex);
    return std::nullopt;
  }
  return mutex;
}

}  // namespace webicam
