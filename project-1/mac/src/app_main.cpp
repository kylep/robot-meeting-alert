// Robot Meeting Alert: three LED switches and a hold-to-buzz button.
// Based on Dear ImGui's example_sdl3_sdlrenderer3.
#include <SDL3/SDL.h>

#include <cstdio>

#include "discovery.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "link.hpp"

namespace {

constexpr int kWidth = 380;
constexpr int kHeight = 300;

// iOS-style toggle switch. Returns true when clicked.
bool Toggle(const char* label, bool* v, ImU32 on_color) {
  ImGuiStyle& style = ImGui::GetStyle();
  float h = ImGui::GetFrameHeight();
  float w = h * 1.8f;
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::PushID(label);
  bool clicked = ImGui::InvisibleButton("##toggle", ImVec2(w, h));
  ImGui::PopID();
  if (clicked) *v = !*v;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImU32 track = *v ? on_color
                   : ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_FrameBgHovered
                                                               : ImGuiCol_FrameBg);
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), track, h * 0.5f);
  float r = h * 0.5f - 2.0f;
  float cx = *v ? p.x + w - r - 2.0f : p.x + r + 2.0f;
  dl->AddCircleFilled(ImVec2(cx, p.y + h * 0.5f), r, IM_COL32(245, 245, 245, 255));
  ImGui::SameLine(0, style.ItemInnerSpacing.x * 2);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  return clicked;
}

void StatusDot(ImU32 color) {
  float h = ImGui::GetTextLineHeight();
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(h, h));
  ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + h * 0.5f, p.y + h * 0.5f),
                                              h * 0.32f, color);
  ImGui::SameLine();
}

}  // namespace

int main(int, char**) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::printf("Error: SDL_Init(): %s\n", SDL_GetError());
    return 1;
  }
  float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  if (scale <= 0.0f) scale = 1.0f;
  SDL_Window* window = SDL_CreateWindow(
      "Robot Meeting Alert", static_cast<int>(kWidth * scale),
      static_cast<int>(kHeight * scale), SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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
  io.IniFilename = nullptr;  // nothing worth persisting
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowPadding = ImVec2(18, 16);
  style.ItemSpacing = ImVec2(10, 12);
  style.FrameRounding = 6.0f;
  style.ScaleAllSizes(scale);
  style.FontScaleDpi = scale;
  style.FontSizeBase = 16.0f;
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  rma::Link link(rma::find_pico_ports);
  link.start();

  rma::Outputs want;
  bool focused = true;
  bool done = false;
  while (!done) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) done = true;
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
          event.window.windowID == SDL_GetWindowID(window)) {
        done = true;
      }
      if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) focused = false;
      if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) focused = true;
    }

    bool minimized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0;
    if (minimized || !focused) want.buzz = false;
    // Every iteration, including minimized ones, so the buzz stamp stays honest.
    link.set_desired(want);
    if (minimized) {
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
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    rma::LinkSnapshot snap = link.snapshot();
    if (snap.state == rma::LinkState::Connected) {
      StatusDot(IM_COL32(80, 200, 120, 255));
      ImGui::Text("Connected  %s  fw %s", snap.port.c_str(), snap.fw.c_str());
    } else if (snap.port_busy) {
      StatusDot(IM_COL32(240, 180, 60, 255));
      ImGui::TextUnformatted("Port busy - close other serial apps");
    } else {
      StatusDot(IM_COL32(130, 130, 130, 255));
      ImGui::TextUnformatted("Looking for Pico... (plug in USB)");
    }
    ImGui::Separator();

    struct Row { const char* label; bool* want; bool have; ImU32 color; };
    Row rows[] = {
        {"Red", &want.r, snap.reported.r, IM_COL32(225, 70, 70, 255)},
        {"Green", &want.g, snap.reported.g, IM_COL32(60, 190, 100, 255)},
        {"Blue", &want.b, snap.reported.b, IM_COL32(70, 130, 235, 255)},
    };
    for (Row& row : rows) {
      Toggle(row.label, row.want, row.color);
      if (snap.state == rma::LinkState::Connected) {
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetTextLineHeight());
        StatusDot(row.have ? row.color : IM_COL32(70, 70, 70, 255));
        ImGui::NewLine();
      }
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(90, 70, 150, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(110, 90, 175, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(150, 110, 230, 255));
    ImGui::Button(want.buzz ? "Buzzing..." : "Hold to buzz",
                  ImVec2(-1, 64.0f * scale));
    ImGui::PopStyleColor(3);
    want.buzz = ImGui::IsItemActive() && focused;

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
