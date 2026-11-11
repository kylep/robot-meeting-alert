// fetch_url() (../project-3/mac/src/ics_provider.hpp) on WinHTTP. HTTPS
// only; uses the system proxy settings; error texts never contain the URL.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <string>

#include "ics_provider.hpp"

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4  // Windows 8.1+
#endif

namespace rma {
namespace {

constexpr size_t kMaxBytes = 20 * 1024 * 1024;

std::wstring widen(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string reason(DWORD e) {
  switch (e) {
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "server not found";
    case ERROR_WINHTTP_TIMEOUT: return "the server didn't answer";
    case ERROR_WINHTTP_CANNOT_CONNECT: return "can't connect to the server";
    case ERROR_WINHTTP_SECURE_FAILURE: return "secure connection failed";
    default: return "network error " + std::to_string(static_cast<unsigned long>(e));
  }
}

struct Handle {
  HINTERNET h = nullptr;
  ~Handle() {
    if (h) WinHttpCloseHandle(h);
  }
};

}  // namespace

bool fetch_url(const std::string& url, std::string* body, std::string* error) {
  std::wstring wurl = widen(url);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof parts;
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  parts.dwUrlPathLength = static_cast<DWORD>(-1);
  parts.dwExtraInfoLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
    *error = "that isn't a valid https:// link";
    return false;
  }
  std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
  if (parts.lpszExtraInfo) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
  if (path.empty()) path = L"/";

  Handle session, connect, request;
  session.h = WinHttpOpen(L"RobotMeetingAlert/0.4", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session.h) {  // older Windows: no automatic proxy mode
    session.h = WinHttpOpen(L"RobotMeetingAlert/0.4", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
  }
  if (!session.h) {
    *error = reason(GetLastError());
    return false;
  }
  WinHttpSetTimeouts(session.h, 10000, 10000, 30000, 30000);
  connect.h = WinHttpConnect(session.h, host.c_str(), parts.nPort, 0);
  if (connect.h) {
    request.h = WinHttpOpenRequest(connect.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH);
  }
  if (!request.h || !WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.h, nullptr)) {
    *error = reason(GetLastError());
    return false;
  }
  DWORD status = 0, size = sizeof status;
  WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                      &status, &size, WINHTTP_NO_HEADER_INDEX);
  if (status != 200) {
    *error = "the server said HTTP " + std::to_string(static_cast<unsigned long>(status));
    return false;
  }
  body->clear();
  for (;;) {
    char buf[16384];
    DWORD n = 0;
    if (!WinHttpReadData(request.h, buf, sizeof buf, &n)) {
      *error = reason(GetLastError());
      return false;
    }
    if (n == 0) break;
    body->append(buf, n);
    if (body->size() > kMaxBytes) {
      *error = "the calendar is too big";
      return false;
    }
  }
  return true;
}

}  // namespace rma
