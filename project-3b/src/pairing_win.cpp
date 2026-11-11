// pairing.hpp (../project-3/mac/src) on Windows: the robot key and the iCal
// link live in %APPDATA%\RobotMeetingAlert\ (pairing.key, ics.link). Each
// file is created with an access list that lets only the current Windows
// user open it (no inherited entries), the Windows version of chmod 600.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <shlobj.h>

#include <string>
#include <system_error>

#include "pairing.hpp"

namespace rma {
namespace {

// "D:P(A;;FA;;;<this user's SID>)": protected DACL, full access for this user only.
std::wstring user_only_sddl() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::wstring out;
  if (size > 0) {
    std::string buf(size, '\0');
    if (GetTokenInformation(token, TokenUser, buf.data(), size, &size)) {
      LPWSTR sid = nullptr;
      if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &sid)) {
        out = L"D:P(A;;FA;;;" + std::wstring(sid) + L")";
        LocalFree(sid);
      }
    }
  }
  CloseHandle(token);
  return out;
}

bool write_private(const wchar_t* name, const void* data, size_t len) {
  std::filesystem::path dir = app_data_dir();
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::wstring path = (dir / name).wstring(), tmp = path + L".tmp";
  std::wstring sddl = user_only_sddl();
  if (sddl.empty()) return false;
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return false;
  SECURITY_ATTRIBUTES sa{sizeof sa, sd, FALSE};
  DeleteFileW(tmp.c_str());  // a leftover would keep its old access list
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, &sa, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  LocalFree(sd);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD n = 0;
  bool ok = WriteFile(h, data, static_cast<DWORD>(len), &n, nullptr) && n == len;
  ok = FlushFileBuffers(h) && ok;
  CloseHandle(h);
  if (ok) ok = MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  if (!ok) DeleteFileW(tmp.c_str());
  return ok;
}

bool read_file(const wchar_t* name, std::string* out, size_t max) {
  std::wstring path = (app_data_dir() / name).wstring();
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  char buf[512];
  DWORD n = 0;
  out->clear();
  while (ReadFile(h, buf, sizeof buf, &n, nullptr) && n > 0 && out->size() < max) out->append(buf, n);
  CloseHandle(h);
  return true;
}

}  // namespace

std::filesystem::path app_data_dir() {
  PWSTR roaming = nullptr;
  std::filesystem::path dir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) dir = roaming;
  CoTaskMemFree(roaming);
  if (dir.empty()) dir = ".";
  return dir / L"RobotMeetingAlert";
}

std::string pairing_key_path() { return (app_data_dir() / L"pairing.key").u8string(); }

bool load_pairing_key(uint8_t key[16]) {
  std::string data;
  if (!read_file(L"pairing.key", &data, 64) || data.size() != 16) return false;
  for (int i = 0; i < 16; ++i) key[i] = static_cast<uint8_t>(data[i]);
  return true;
}

bool save_pairing_key(const uint8_t key[16]) { return write_private(L"pairing.key", key, 16); }

std::string load_ics_link() {
  std::string out;
  if (!read_file(L"ics.link", &out, 8192)) return {};
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
  return out;
}

bool save_ics_link(const std::string& link) {
  if (link.empty()) {
    std::wstring path = (app_data_dir() / L"ics.link").wstring();
    return DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
  }
  return write_private(L"ics.link", link.data(), link.size());
}

}  // namespace rma
