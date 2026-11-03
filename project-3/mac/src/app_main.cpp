// Robot Meeting Alert, project 3: the finished product. Reads the Mac's
// calendar and tells the robot (over Wi-Fi, or USB) when the next meetings
// are; the robot keeps time and runs the alerts itself. Manual mode keeps
// project 2's light and sound controls.
// Based on Dear ImGui's example_sdl3_sdlrenderer3.
#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>

#include "calendar.hpp"
#include "discovery.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "link.hpp"
#include "pairing.hpp"
#include "rma3/auth.hpp"

namespace {

constexpr int kWidth = 880;
constexpr int kHeight = 680;
constexpr double kLookahead = 12 * 3600;
const char* kSounds[] = {"Off",          "Westminster chime", "Twinkle twinkle", "Ode to joy",
                         "Beep",         "Fur Elise",         "Frere Jacques",   "Mary had a little lamb",
                         "Happy birthday", "Doorbell",        "Chirp"};
const char* kMeterModes[] = {"None", "Brightness", "Volume", "Both"};
const char* kNet[] = {"no Wi-Fi settings", "joining Wi-Fi...", "on Wi-Fi", "can't join Wi-Fi"};
// In rma3::Look order.
const char* kLookNames[] = {"No calendar", "Free", "Meeting soon", "Meeting starting", "In a meeting"};
const char* kLookHints[] = {"Before this Mac sends a calendar, or 15 min after it goes quiet.",
                            "No meeting in the next 5 minutes.", "A meeting starts within 5 minutes (breathes).",
                            "The first 10 seconds of a meeting.", "For the rest of a meeting."};

uint8_t to_byte(float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

// Settings file form of a status colour: "RRGGBB", or "-RRGGBB" for none
// (the colour comes back if None is unticked).
std::string look_pref(const rma3::LookColour& c) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "%02X%02X%02X", c.r, c.g, c.b);
  return (c.none ? "-" : "") + std::string(buf);
}

rma3::LookColour look_from_pref(const std::string& s, const rma3::LookColour& def) {
  bool none = !s.empty() && s[0] == '-';
  std::string hex = none ? s.substr(1) : s;
  if (hex.size() != 6 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return def;
  unsigned long v = std::strtoul(hex.c_str(), nullptr, 16);
  return rma3::LookColour{none, static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)};
}

double wall_now() {
  using namespace std::chrono;
  return duration_cast<duration<double>>(system_clock::now().time_since_epoch()).count();
}

std::string clock_time(double t) {
  time_t tt = static_cast<time_t>(t);
  tm local{};
  localtime_r(&tt, &local);
  char buf[32];
  strftime(buf, sizeof buf, "%-I:%M %p", &local);
  return buf;
}

std::string in_words(double secs) {
  long m = static_cast<long>(secs / 60.0 + 0.5);
  if (m < 1) return "now";
  if (m < 60) return "in " + std::to_string(m) + " min";
  return "in " + std::to_string(m / 60) + " h " + std::to_string(m % 60) + " min";
}

// ------------------------------------------------------------ settings file

// ~/Library/Application Support/RobotMeetingAlert/project3.ini, key=value.
// Holds choices only (calendar ids, mode, colours); no passwords.
struct Prefs {
  std::map<std::string, std::string> kv;

  static std::string path() {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/Library/Application Support/RobotMeetingAlert/project3.ini";
  }
  void load() {
    std::ifstream f(path());
    std::string line;
    while (std::getline(f, line)) {
      size_t eq = line.find('=');
      if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
  }
  void save() const {
    std::string dir = path().substr(0, path().rfind('/'));
    mkdir(dir.c_str(), 0755);
    std::ofstream f(path());
    for (const auto& [k, v] : kv) f << k << "=" << v << "\n";
  }
  int num(const std::string& k, int def) const {
    auto it = kv.find(k);
    return it == kv.end() ? def : std::atoi(it->second.c_str());
  }
  std::string str(const std::string& k) const {
    auto it = kv.find(k);
    return it == kv.end() ? std::string() : it->second;
  }
  std::set<std::string> list(const std::string& k) const {
    std::set<std::string> out;
    auto it = kv.find(k);
    if (it == kv.end()) return out;
    std::stringstream ss(it->second);
    std::string item;
    while (std::getline(ss, item, ',')) {
      if (!item.empty()) out.insert(item);
    }
    return out;
  }
};

// ------------------------------------------------------------ widgets

// iOS-style toggle switch. Returns true when clicked.
bool Toggle(const char* label, bool* v, ImU32 on_color) {
  float h = ImGui::GetFrameHeight();
  float w = h * 1.8f;
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::PushID(label);
  bool clicked = ImGui::InvisibleButton("##toggle", ImVec2(w, h));
  ImGui::PopID();
  if (clicked) *v = !*v;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImU32 track = *v ? on_color : ImGui::GetColorU32(ImGuiCol_FrameBg);
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), track, h * 0.5f);
  float r = h * 0.5f - 2.0f;
  float cx = *v ? p.x + w - r - 2.0f : p.x + r + 2.0f;
  dl->AddCircleFilled(ImVec2(cx, p.y + h * 0.5f), r, IM_COL32(245, 245, 245, 255));
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  return clicked;
}

void StatusDot(ImU32 color) {
  float h = ImGui::GetTextLineHeight();
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(h, h));
  ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + h * 0.5f, p.y + h * 0.5f), h * 0.32f, color);
  ImGui::SameLine();
}

void Section(const char* title) {
  ImGui::Spacing();
  ImGui::TextDisabled("%s", title);
  ImGui::Separator();
}

// A status colour at full strength, so dim ones still read as a dot.
ImU32 Vivid(const rma3::LookColour& c) {
  int m = std::max({c.r, c.g, c.b});
  if (m == 0) return IM_COL32(60, 60, 60, 255);  // black: the light is off
  return IM_COL32(c.r * 255 / m, c.g * 255 / m, c.b * 255 / m, 255);
}

// What the robot's light means right now, as reported in the state line.
void StageLine(const rma3::State& s, const rma3::Config& cfg) {
  const char* text[] = {"no calendar from this Mac yet", "free", "a meeting starts soon", "in a meeting"};
  const rma3::Look look[] = {rma3::kLookWaiting, rma3::kLookFree, rma3::kLookSoon, rma3::kLookMeeting};
  int st = s.stage < 4 ? s.stage : 0;
  const rma3::LookColour& c = cfg.look[look[st]];
  StatusDot(c.none ? IM_COL32(130, 130, 130, 255) : Vivid(c));
  ImGui::Text("Robot status: %s%s", text[st], c.none ? " (light left as it was)" : "");
}

}  // namespace

int main(int, char**) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::printf("Error: SDL_Init(): %s\n", SDL_GetError());
    return 1;
  }
  float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  if (scale <= 0.0f) scale = 1.0f;
  SDL_Window* window = SDL_CreateWindow("Robot Meeting Alert", static_cast<int>(kWidth * scale),
                                        static_cast<int>(kHeight * scale),
                                        SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
  if (window == nullptr) {
    std::printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
    return 1;
  }
  SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
  if (renderer == nullptr) {
    std::printf("Error: SDL_CreateRenderer(): %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetRenderVSync(renderer, 1);
  SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(window);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowPadding = ImVec2(18, 16);
  style.ItemSpacing = ImVec2(10, 9);
  style.FrameRounding = 6.0f;
  style.ScaleAllSizes(scale);
  style.FontScaleDpi = scale;
  style.FontSizeBase = 16.0f;
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  Prefs prefs;
  prefs.load();

  rma::RobotFinder finder;
  char host_buf[64] = {};
  std::snprintf(host_buf, sizeof host_buf, "%s", prefs.kv["host"].c_str());
  finder.set_manual(host_buf);
  rma3::Link link([&] { return finder.candidates(); });
  uint8_t pairing_key[16];
  bool have_passphrase = rma::load_pairing_key(pairing_key);
  if (have_passphrase) link.set_key(pairing_key);
  char passphrase[128] = {};
  bool show_pass = false;
  link.start();

  // Calendar source (Settings tab). Chosen calendars are remembered per source.
  int source = prefs.num("source", 0);
  if (source < 0 || source >= rma::kCalendarKinds) source = 0;
  auto chosen_key = [&] { return source == 0 ? std::string("calendars") : "calendars" + std::to_string(source); };
  std::unique_ptr<rma::CalendarProvider> cal = rma::make_calendar_provider(static_cast<rma::CalendarKind>(source));
  std::set<std::string> chosen = prefs.list(chosen_key());
  std::vector<rma::CalendarInfo> calendars;
  std::vector<rma3::CalEvent> events;
  double test_until = 0;  // a pretend meeting from the Test alert button
  bool test_on = false;
  rma3::CalEvent test_event;
  double next_fetch = 0;
  if (!chosen.empty()) cal->request_access();

  rma3::Desired want;
  want.light = prefs.num("light", 1) != 0;
  want.mode = static_cast<uint8_t>(prefs.num("mode", chosen.empty() ? 0 : 1));
  want.sound = static_cast<uint8_t>(prefs.num("sound", 1));
  want.meter = static_cast<uint8_t>(prefs.num("meter", 0));
  float color[3] = {prefs.num("r", 255) / 255.0f, prefs.num("g", 140) / 255.0f, prefs.num("b", 25) / 255.0f};
  int sound = want.sound, meter = want.meter, mode = want.mode;
  int vol_pct = std::clamp(prefs.num("vol", 100), 0, 100);
  int bright_pct = std::clamp(prefs.num("bright", 100), 0, 100);
  bool warn_on = prefs.num("warn", 0) != 0;
  int warn_min = std::clamp(prefs.num("warn_min", 5), 1, static_cast<int>(rma3::kMaxWarnMin));
  want.cfg.start_sound = prefs.num("start_sound", 1) != 0;
  for (int i = 0; i < rma3::kLooks; ++i) {
    want.cfg.look[i] = look_from_pref(prefs.str("look" + std::to_string(i)), rma3::kDefaultLooks[i]);
  }
  int look_sel = rma3::kLookFree;
  char ssid[33] = {}, pass[64] = {};

  bool done = false;
  while (!done) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) done = true;
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) {
        done = true;
      }
    }
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
      SDL_Delay(10);
    }

    // Calendar: re-read every 30 s, on any calendar change, and after the
    // Test alert button. Runs even while the window is minimised.
    double now = wall_now();
    if (test_on && now >= test_until) {  // drop the pretend meeting from the robot
      test_on = false;
      next_fetch = 0;
    }
    bool cal_changed = cal->take_changed();
    if (cal_changed && cal->access() == rma::CalendarProvider::Access::Granted) calendars = cal->calendars();
    if (cal_changed || now >= next_fetch) {
      events = cal->events(std::vector<std::string>(chosen.begin(), chosen.end()), now - 86400, now + kLookahead);
      next_fetch = now + 30;
      std::vector<rma3::CalEvent> all = events;
      if (now < test_until) all.push_back(test_event);
      link.set_calendar(!chosen.empty() || now < test_until, all);
    }

    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) continue;

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    // ---- Status
    rma3::LinkSnapshot snap = link.snapshot();
    bool connected = snap.state == rma3::LinkState::Connected;
    if (connected) {
      StatusDot(IM_COL32(80, 200, 120, 255));
      if (snap.over_wifi) {
        ImGui::Text("Connected over Wi-Fi (%s)  fw %s", snap.ip.c_str(), snap.fw.c_str());
      } else {
        ImGui::Text("Connected over USB  fw %s", snap.fw.c_str());
      }
      ImGui::TextDisabled("Robot is %s%s%s", kNet[snap.net < 4 ? snap.net : 0],
                          snap.net == 2 ? " at " : "", snap.net == 2 ? snap.ip.c_str() : "");
      if (snap.reported.mode == 1) StageLine(snap.reported, want.cfg);
    } else if (snap.port_busy) {
      StatusDot(IM_COL32(240, 180, 60, 255));
      ImGui::TextUnformatted("USB port busy - quit rmactl or other serial apps");
    } else if (snap.not_paired) {
      StatusDot(IM_COL32(240, 180, 60, 255));
      ImGui::TextUnformatted(have_passphrase
                                 ? "Found the robot, but it didn't accept this Mac's passphrase (see Settings)."
                                 : "Found the robot. Type its passphrase in Settings to control it.");
    } else {
      StatusDot(IM_COL32(130, 130, 130, 255));
      ImGui::TextUnformatted("Looking for the robot (Wi-Fi or USB)...");
    }
    if (connected && !snap.last_error.empty()) {
      ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", snap.last_error.c_str());
    }
    ImU32 swatch = ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], 1.0f));
    Toggle("Light on", &want.light, swatch);

    // Two tabs: the robot (two columns, no scrolling) and settings.
    bool tabs = ImGui::BeginTabBar("tabs");
    if (tabs && ImGui::BeginTabItem("Robot")) {
    ImGui::BeginTable("cols", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV);
    ImGui::TableNextColumn();

    // ---- Mode
    Section("MODE");
    ImGui::RadioButton("Calendar", &mode, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Manual", &mode, 0);
    want.mode = static_cast<uint8_t>(mode);

    // ---- Calendar
    if (mode == 1) {
      Section("CALENDAR");
      switch (cal->access()) {
        case rma::CalendarProvider::Access::Unknown:
          ImGui::TextWrapped("The robot needs to see when your meetings are (times only, never titles).");
          if (ImGui::Button("Allow calendar access")) cal->request_access();
          break;
        case rma::CalendarProvider::Access::NotBuilt:
          ImGui::TextWrapped("\"%s\" isn't built yet. Choose \"Mac Calendar app\" in Settings.", cal->name());
          break;
        case rma::CalendarProvider::Access::Requesting:
          ImGui::TextUnformatted("Waiting for permission...");
          break;
        case rma::CalendarProvider::Access::Denied:
          ImGui::TextWrapped(
              "Calendar access is off. Turn it on in System Settings > Privacy & Security > Calendars, "
              "then reopen the app.");
          break;
        case rma::CalendarProvider::Access::Granted: {
          if (calendars.empty()) calendars = cal->calendars();
          std::string account;
          bool changed = false;
          ImGui::BeginChild("cals", ImVec2(-1, 170 * scale), ImGuiChildFlags_None);
          for (const rma::CalendarInfo& c : calendars) {
            if (c.account != account) {
              account = c.account;
              ImGui::TextDisabled("%s", account.c_str());
            }
            bool on = chosen.count(c.id) > 0;
            ImGui::PushID(c.id.c_str());
            ImGui::ColorButton("##c", ImVec4(c.rgb[0], c.rgb[1], c.rgb[2], 1),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(12 * scale, 12 * scale));
            ImGui::SameLine();
            if (ImGui::Checkbox(c.title.c_str(), &on)) {
              if (on) chosen.insert(c.id);
              else chosen.erase(c.id);
              changed = true;
            }
            ImGui::PopID();
          }
          ImGui::EndChild();
          if (changed) next_fetch = 0;  // re-read at once
          if (chosen.empty()) {
            ImGui::TextDisabled("Tick the calendar(s) to watch.");
          } else {
            auto picked = rma3::pick_meetings(now, events);
            const rma3::Pushed* next = nullptr;
            for (const auto& p : picked) {
              if (p.start > 0) { next = &p; break; }
            }
            if (next) {
              ImGui::Text("Next meeting: %s (%s)", clock_time(now + next->start).c_str(),
                          in_words(next->start).c_str());
            } else {
              ImGui::TextUnformatted("No more meetings in the next 12 hours.");
            }
            if (snap.pushed >= 0) ImGui::TextDisabled("Robot knows about %d meeting(s).", snap.pushed);
          }
          break;
        }
      }
      if (!snap.schedule_error.empty()) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", snap.schedule_error.c_str());
      ImGui::BeginDisabled(!connected || now < test_until);
      if (ImGui::Button("Test alert")) {  // amber now, melody in 10 s, red for 30 s
        test_event = rma3::CalEvent{};
        test_event.id = "test-alert-" + std::to_string(static_cast<long>(now));
        test_event.start = now + 10;
        test_event.end = now + 40;
        test_until = now + 45;
        test_on = true;
        next_fetch = 0;
      }
      ImGui::EndDisabled();
      if (now < test_until) {
        ImGui::SameLine();
        ImGui::TextDisabled(now < test_event.start ? "melody in %.0f s" : "pretend meeting...",
                            test_event.start - now);
      }

      // ---- Light config: the colour for each status
      Section("LIGHT CONFIG");
      ImGui::SetNextItemWidth(170.0f * scale);
      ImGui::Combo("##look", &look_sel, kLookNames, rma3::kLooks);
      rma3::LookColour& lc = want.cfg.look[look_sel];
      ImGui::SameLine();
      ImGui::BeginDisabled(lc.none);
      float rgb[3] = {lc.r / 255.0f, lc.g / 255.0f, lc.b / 255.0f};
      if (ImGui::ColorEdit3("##lookcolour", rgb,
                            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel |
                                ImGuiColorEditFlags_NoAlpha)) {
        lc.r = to_byte(rgb[0]);
        lc.g = to_byte(rgb[1]);
        lc.b = to_byte(rgb[2]);
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::Checkbox("None##look", &lc.none);
      ImGui::SameLine();
      if (ImGui::Button("Default##look")) lc = rma3::kDefaultLooks[look_sel];
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled("%s", lc.none ? "None: the light stays as it was." : kLookHints[look_sel]);
      ImGui::PopTextWrapPos();
    }

    // ---- Manual colour
    if (mode == 0) {
      Section("COLOUR");
      ImGui::SetNextItemWidth(200.0f * scale);
      ImGui::ColorPicker3("##colour", color,
                          ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoSidePreview |
                              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);
      struct Preset { const char* name; float c[3]; };
      const Preset presets[] = {{"Red", {1, 0, 0}},       {"Green", {0, 1, 0}},  {"Blue", {0, 0, 1}},
                                {"Yellow", {1, 0.85f, 0}}, {"Purple", {0.6f, 0, 1}}, {"White", {1, 1, 1}}};
      for (int i = 0; i < 6; ++i) {
        if (i) ImGui::SameLine();
        const Preset& p = presets[i];
        if (ImGui::ColorButton(p.name, ImVec4(p.c[0], p.c[1], p.c[2], 1), ImGuiColorEditFlags_NoTooltip,
                               ImVec2(28 * scale, 28 * scale))) {
          for (int k = 0; k < 3; ++k) color[k] = p.c[k];
        }
      }
    }
    want.r = static_cast<uint8_t>(color[0] * 255.0f + 0.5f);
    want.g = static_cast<uint8_t>(color[1] * 255.0f + 0.5f);
    want.b = static_cast<uint8_t>(color[2] * 255.0f + 0.5f);

    ImGui::TableNextColumn();

    // ---- Sound
    Section(mode == 1 ? "ALERT SOUND" : "SOUND");
    ImGui::SetNextItemWidth(200.0f * scale);
    if (ImGui::Combo("##sound", &sound, kSounds, IM_ARRAYSIZE(kSounds))) {
      want.sound = static_cast<uint8_t>(sound);
      link.set_desired(want);           // selection reaches the device first...
      if (sound > 0) link.play(sound);  // ...then a one-off preview
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(sound == 0);
    if (ImGui::Button("Play")) link.play();
    ImGui::EndDisabled();
    if (connected && snap.reported.playing) {
      ImGui::SameLine();
      ImGui::TextDisabled("playing...");
    }
    if (mode == 1) {
      ImGui::Checkbox("Play it when a meeting starts", &want.cfg.start_sound);
      ImGui::Checkbox("Warning beep", &warn_on);
      ImGui::SameLine();
      ImGui::BeginDisabled(!warn_on);
      ImGui::SetNextItemWidth(160.0f * scale);
      ImGui::SliderInt("##warnmin", &warn_min, 1, rma3::kMaxWarnMin, "%d min before", ImGuiSliderFlags_AlwaysClamp);
      ImGui::EndDisabled();
    }
    want.cfg.warn_min = static_cast<uint8_t>(warn_on ? warn_min : 0);

    // ---- Knob
    Section("KNOB");
    ImGui::TextUnformatted("Knob controls:");
    for (int i = 0; i < 4; ++i) {
      if (i) ImGui::SameLine();
      ImGui::RadioButton(kMeterModes[i], &meter, i);
    }
    want.meter = static_cast<uint8_t>(meter);
    char overlay[16];
    std::snprintf(overlay, sizeof overlay, connected ? "%d%%" : "--", snap.reported.knob);
    ImGui::ProgressBar(connected ? snap.reported.knob / 100.0f : 0.0f, ImVec2(-1, 0), overlay);

    // ---- Overall levels, multiplied by the knob when it controls them
    Section("VOLUME AND BRIGHTNESS");
    ImGui::SetNextItemWidth(200.0f * scale);
    ImGui::SliderInt("Volume##overall", &vol_pct, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(200.0f * scale);
    ImGui::SliderInt("Brightness##overall", &bright_pct, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    want.cfg.volume = static_cast<uint8_t>(vol_pct);
    want.cfg.brightness = static_cast<uint8_t>(bright_pct);
    if (connected) {
      int k = snap.reported.knob;
      int v = (meter == 2 || meter == 3) ? vol_pct * k / 100 : vol_pct;        // Volume, Both
      int b = (meter == 1 || meter == 3) ? bright_pct * k / 100 : bright_pct;  // Brightness, Both
      ImGui::TextDisabled("With the knob: volume %d%%, brightness %d%%", v, b);
    } else {
      ImGui::TextDisabled("Multiplied by the knob when it controls them.");
    }

    ImGui::EndTable();
    ImGui::EndTabItem();
    }

    // ---- Settings tab: calendar source, Wi-Fi setup and the robot's address
    if (tabs && ImGui::BeginTabItem("Settings")) {
      Section("CALENDAR SOURCE");
      for (int k = 0; k < rma::kCalendarKinds; ++k) {
        if (ImGui::RadioButton(rma::calendar_kind_name(static_cast<rma::CalendarKind>(k)), source == k) &&
            source != k) {
          source = k;
          cal = rma::make_calendar_provider(static_cast<rma::CalendarKind>(k));
          calendars.clear();
          events.clear();
          chosen = prefs.list(chosen_key());
          next_fetch = 0;
        }
      }
      ImGui::Spacing();
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled("%s", cal->help());
      ImGui::PopTextWrapPos();
      Section("ROBOT PASSPHRASE");
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled(
          "Any Mac with this passphrase can control the robot over Wi-Fi. Type the same one on each Mac. "
          "It never crosses the network. Changing it on the robot needs USB.");
      ImGui::PopTextWrapPos();
      ImGui::SetNextItemWidth(260.0f * scale);
      ImGui::InputTextWithHint("##pass", have_passphrase ? "saved (type to change)" : "click Generate",
                               passphrase, sizeof passphrase, show_pass ? 0 : ImGuiInputTextFlags_Password);
      ImGui::SameLine();
      if (ImGui::Button("Generate")) {
        std::snprintf(passphrase, sizeof passphrase, "%s", rma3::generate_passphrase().c_str());
        show_pass = true;  // write it down for the other Mac
      }
      ImGui::SameLine();
      ImGui::Checkbox("Show", &show_pass);
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled(
          "Use Generate: someone who records your Wi-Fi traffic could try guessing a made-up passphrase "
          "offline, but not a generated one. Write it down to type on your other Mac.");
      ImGui::PopTextWrapPos();
      bool usb_now = connected && !snap.over_wifi;
      bool long_enough = std::strlen(passphrase) >= 8;
      ImGui::BeginDisabled(!long_enough);
      if (ImGui::Button(usb_now ? "Save, and set on robot" : "Save on this Mac")) {
        uint8_t k[16];
        rma3::passphrase_key(passphrase, k);  // ~0.1 s, deliberately slow
        std::fill(std::begin(passphrase), std::end(passphrase), '\0');
        show_pass = false;
        if (rma::save_pairing_key(k)) {
          have_passphrase = true;
          link.set_key(k);
          if (usb_now) link.set_key_on_robot();
        }
      }
      ImGui::EndDisabled();
      if (!long_enough && passphrase[0]) {
        ImGui::SameLine();
        ImGui::TextDisabled("at least 8 characters");
      }
      if (!snap.key_result.empty()) ImGui::TextWrapped("%s", snap.key_result.c_str());

      Section("WI-FI SETUP");
      bool usb = connected && !snap.over_wifi;
      ImGui::TextWrapped(usb ? "Type your Wi-Fi details; they are saved on the robot (not in this app)."
                             : "Plug the robot into this Mac with USB to set up Wi-Fi.");
      ImGui::BeginDisabled(!usb);
      ImGui::SetNextItemWidth(220.0f * scale);
      ImGui::InputText("Network name", ssid, sizeof ssid);
      ImGui::SetNextItemWidth(220.0f * scale);
      ImGui::InputText("Password", pass, sizeof pass, ImGuiInputTextFlags_Password);
      if (ImGui::Button("Save to robot")) {
        link.wifi_setup(ssid, pass);
        std::fill(std::begin(pass), std::end(pass), '\0');
      }
      ImGui::SameLine();
      if (ImGui::Button("Forget Wi-Fi")) link.wifi_clear();
      ImGui::EndDisabled();
      if (!snap.wifi_result.empty()) ImGui::TextWrapped("%s", snap.wifi_result.c_str());
      if (usb) {  // live, from the robot's NET reply (polled every 5 s)
        if (snap.net == 2) {
          ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.5f, 1), "On Wi-Fi at %s. You can unplug the USB cable now.",
                             snap.ip.c_str());
        } else if (snap.net == 1) {
          ImGui::TextDisabled("Joining Wi-Fi...");
        } else if (snap.net == 3) {
          ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1),
                             "Can't join Wi-Fi. Check the name and password (2.4 GHz networks only).");
        } else {
          ImGui::TextDisabled("No Wi-Fi saved on the robot yet.");
        }
      }
      Section("ROBOT ADDRESS");
      ImGui::TextDisabled("Only needed if the app doesn't find the robot by itself:");
      ImGui::SetNextItemWidth(160.0f * scale);
      if (ImGui::InputTextWithHint("##host", "e.g. 192.168.1.50", host_buf, sizeof host_buf)) {
        finder.set_manual(host_buf);
      }
      std::string found = finder.found();
      if (!found.empty()) ImGui::TextDisabled("Found rma.local at %s", found.c_str());
      ImGui::EndTabItem();
    }
    if (tabs) ImGui::EndTabBar();

    link.set_desired(want);
    ImGui::End();
    ImGui::Render();
    SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColorFloat(renderer, 0.08f, 0.08f, 0.10f, 1.0f);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);

    // Remember choices (cheap; only when something changed).
    Prefs next = prefs;
    std::string ids;
    for (const auto& id : chosen) ids += (ids.empty() ? "" : ",") + id;
    next.kv[chosen_key()] = ids;
    next.kv["source"] = std::to_string(source);
    next.kv["mode"] = std::to_string(want.mode);
    next.kv["light"] = std::to_string(want.light ? 1 : 0);
    next.kv["sound"] = std::to_string(want.sound);
    next.kv["meter"] = std::to_string(want.meter);
    next.kv["r"] = std::to_string(want.r);
    next.kv["g"] = std::to_string(want.g);
    next.kv["b"] = std::to_string(want.b);
    next.kv["vol"] = std::to_string(vol_pct);
    next.kv["bright"] = std::to_string(bright_pct);
    next.kv["warn"] = warn_on ? "1" : "0";
    next.kv["warn_min"] = std::to_string(warn_min);
    next.kv["start_sound"] = want.cfg.start_sound ? "1" : "0";
    for (int i = 0; i < rma3::kLooks; ++i) next.kv["look" + std::to_string(i)] = look_pref(want.cfg.look[i]);
    next.kv["host"] = host_buf;
    if (next.kv != prefs.kv) {
      prefs = next;
      prefs.save();
    }
  }

  link.stop();
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
