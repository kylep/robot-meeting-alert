// Robot Meeting Alert, project 2: RGB light, sounds and a knob.
// Based on Dear ImGui's example_sdl3_sdlrenderer3.
#include <SDL3/SDL.h>

#include <cstdio>

#include "discovery.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "link.hpp"

namespace {

constexpr int kWidth = 440;
constexpr int kHeight = 660;
const char* kSounds[] = {"Off", "Westminster chime", "Twinkle twinkle", "Ode to joy", "Beep"};
const char* kMeterModes[] = {"None", "Brightness", "Volume", "Both"};

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
                                        SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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
  style.ItemSpacing = ImVec2(10, 10);
  style.FrameRounding = 6.0f;
  style.ScaleAllSizes(scale);
  style.FontScaleDpi = scale;
  style.FontSizeBase = 16.0f;
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  rma2::Link link(rma::find_pico_ports);
  link.start();

  rma2::Desired want;
  want.light = true;
  float color[3] = {1.0f, 0.55f, 0.1f};  // warm orange to start
  int sound = want.sound;
  int meter = want.meter;

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
      continue;
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    rma2::LinkSnapshot snap = link.snapshot();
    bool connected = snap.state == rma2::LinkState::Connected;
    if (connected) {
      StatusDot(IM_COL32(80, 200, 120, 255));
      ImGui::Text("Connected  %s  fw %s", snap.port.c_str(), snap.fw.c_str());
    } else if (snap.port_busy) {
      StatusDot(IM_COL32(240, 180, 60, 255));
      ImGui::TextUnformatted("Port busy - close other serial apps");
    } else {
      StatusDot(IM_COL32(130, 130, 130, 255));
      ImGui::TextUnformatted("Looking for Pico... (plug in USB)");
    }

    // Light.
    Section("LIGHT");
    ImU32 swatch = ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], 1.0f));
    Toggle("Light on", &want.light, swatch);
    ImGui::SetNextItemWidth(220.0f * scale);
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
    want.r = static_cast<uint8_t>(color[0] * 255.0f + 0.5f);
    want.g = static_cast<uint8_t>(color[1] * 255.0f + 0.5f);
    want.b = static_cast<uint8_t>(color[2] * 255.0f + 0.5f);

    // Sound.
    Section("SOUND");
    ImGui::SetNextItemWidth(220.0f * scale);
    if (ImGui::Combo("##sound", &sound, kSounds, IM_ARRAYSIZE(kSounds))) {
      want.sound = static_cast<uint8_t>(sound);
      link.set_desired(want);        // selection reaches the device first...
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

    // Knob.
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

    link.set_desired(want);
    ImGui::End();
    ImGui::Render();
    SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColorFloat(renderer, 0.08f, 0.08f, 0.10f, 1.0f);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
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
