/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_keycode.h>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include <cstdio>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <thread>

#include "hal/adc_driver.h"
#include "hal/rotary_encoder.h"
#include "hal/switch_driver.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#if !SDL_VERSION_ATLEAST(2,0,19)
#error This backend requires SDL 2.0.19+ because of SDL_RenderGeometryRaw() function
#endif

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "stb_image.h"

#if defined(STBI_NO_STDIO)
  #error "STBI_NO_STDIO is defined"
#endif

#include "simu.h"
#include "widgets.h"
#include "knobs.h"
#include "display.h"

#include "simuaudio.h"
#include "simulib.h"

#include "hal/key_driver.h"
#include "hal/switch_driver.h"
#include "switches.h"

#include "audio.h"
#include "debug.h"
#include "edgetx.h"

#include "arg_parser.h"
#include "simu_control.h"
#include "simu_inputs.h"

#define TIMER_INTERVAL 10 // 10ms

static SDL_Window* window;
static SDL_Renderer* renderer;
static SDL_Texture* screen_frame_buffer;


#if !defined(__EMSCRIPTEN__)
static const unsigned char _icon_png[] = {
#include "icon.lbm"
};
#endif

static bool handleKeyEvents(SDL_Event& event)
{
  if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
    const auto& key_event = event.key;

    bool key_handled = false;
    uint8_t key = 0;
    
    switch(key_event.keysym.sym) {

    case SDLK_ESCAPE:
      key = KEY_EXIT;
      key_handled = true;
      break;
      
    case SDLK_RETURN:
      key = KEY_ENTER;
      key_handled = true;
      break;

    case SDLK_LEFT:
      if (keysGetSupported() & (1 << KEY_LEFT)) {
        key = KEY_LEFT;
        key_handled = true;
      }
      break;
      
    case SDLK_RIGHT:
      if (keysGetSupported() & (1 << KEY_RIGHT)) {
        key = KEY_RIGHT;
        key_handled = true;
      }
      break;

    case SDLK_UP:
#if defined(ROTARY_ENCODER_NAVIGATION)
      if (event.type == SDL_KEYDOWN) {
        simuRotaryEncoderEvent(-1);
      }
#else
      if (keysGetSupported() & (1 << KEY_UP)) {
        key = KEY_UP;
        key_handled = true;
      }
#endif
      break;
      
    case SDLK_DOWN:
#if defined(ROTARY_ENCODER_NAVIGATION)
      if (event.type == SDL_KEYDOWN) {
        simuRotaryEncoderEvent(1);
      }
#else
      if (keysGetSupported() & (1 << KEY_DOWN)) {
        key = KEY_DOWN;
        key_handled = true;
      }
#endif
      break;

    case SDLK_PLUS:
      if (keysGetSupported() & (1 << KEY_PLUS)) {
        key = KEY_PLUS;
        key_handled = true;
      }
      break;

    case SDLK_MINUS:
      if (keysGetSupported() & (1 << KEY_MINUS)) {
        key = KEY_MINUS;
        key_handled = true;
      }
      break;

    case SDLK_PAGEUP:
      if (keysGetSupported() & (1 << KEY_PAGEUP)) {
        key = KEY_PAGEUP;
        key_handled = true;
      }
      break;

    case SDLK_PAGEDOWN:
      if (keysGetSupported() & (1 << KEY_PAGEDN)) {
        key = KEY_PAGEDN;
        key_handled = true;
      }
      break;

    case SDLK_m:
      if (keysGetSupported() & (1 << KEY_MENU)) {
        key = KEY_MENU;
        key_handled = true;
      } else if (keysGetSupported() & (1 << KEY_MODEL)) {
        key = KEY_MODEL;
        key_handled = true;     
      }
      break;

    case SDLK_s:
      if (keysGetSupported() & (1 << KEY_SYS)) {
        key = KEY_SYS;
        key_handled = true;
      }
      break;

    case SDLK_t:
      if (keysGetSupported() & (1 << KEY_TELE)) {
        key = KEY_TELE;
        key_handled = true;
      }
      break;

    case SDLK_l:
      ImGui::StyleColorsLight();
      break;

    case SDLK_d:
      ImGui::StyleColorsDark();
      break;

    case SDLK_r:
      if (event.type == SDL_KEYUP) {
        simuStop();
        simuStart();
      }
      break;

    default:
      key_handled = false;
      break;
    }

    if (key_handled) {
      simuSetKey(key, key_event.type == SDL_KEYDOWN ? true : false);
    }

    return key_handled;
  }

  return false;
}

static void redraw();

static bool handleEvents()
{
  SDL_Event event;
  while (SDL_PollEvent(&event)) {

    if(handleKeyEvents(event))
      continue;
      
    ImGui_ImplSDL2_ProcessEvent(&event);

    if (event.type == SDL_QUIT)
      return false;

    if (event.type == SDL_WINDOWEVENT &&
        event.window.event == SDL_WINDOWEVENT_CLOSE &&
        event.window.windowID == SDL_GetWindowID(window))
      return false;
  }

  redraw();
  return true;
}

static SDL_Surface* LoadImage(const unsigned char* pixels, size_t len)
{
  // Read data
  int32_t w, h, bpp;

  void* data = stbi_load_from_memory(pixels, len, &w, &h, &bpp, 0);
  if (!data) return NULL;

  Uint32 format = (bpp == 3) ? SDL_PIXELFORMAT_RGB24 : SDL_PIXELFORMAT_RGBA32;

  SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, bpp * 8, format);
  if (surface) {
    SDL_LockSurface(surface);
    memcpy(surface->pixels, data, w * h * bpp);
    SDL_UnlockSurface(surface);
  }

  stbi_image_free(data);
  return surface;
}

static SDL_Texture* LoadTexture(SDL_Renderer* renderer, const unsigned char* pixels, size_t len)
{
  SDL_Surface* surface = LoadImage(pixels, len);
  if (!surface) return NULL;

  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_FreeSurface(surface);

  return texture;
}

static float switches_width()
{
  const int max_sw_line = MAX_SWITCHES / 2;
  const int sw_line = std::min((int)switchGetMaxSwitches(), max_sw_line);
  return sw_line * 18 + (sw_line - 1) * 4;
}

static void draw_switches()
{
  const float spacing = 4;
  const ImVec2 sw_size(18, 50);

  ImGui::PushID("switches");
  {
    static int switches[MAX_SWITCHES] = {0};

    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 4.0f);

    int sw_idx = 0;
    for (int i = 0; i < switchGetMaxSwitches(); i++) {
      if (sw_idx) ImGui::SameLine();

      if (!switchIsCustomSwitch(i)) {
        if (++sw_idx >= MAX_SWITCHES / 2) sw_idx = 0;
        if (!SWITCH_EXISTS(i)) {
          switches[i] = 0;
          ImGui::Dummy(sw_size);
        } else {
          ImGui::PushID(i);
          ImGui::VSliderInt("##sw", sw_size,
                            &switches[i], IS_CONFIG_3POS(i) ? 2 : 1,
                            0, "", ImGuiSliderFlags_NoInput);
          if (ImGui::IsItemActive() || ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", switchGetDefaultName(i));
          }
          ImGui::PopID();
        }
        
        if (IS_CONFIG_3POS(i)) {
          simuSetSwitch(i, switches[i] == 0 ? -1 : switches[i] == 1 ? 0 : 1);
        } else {
          simuSetSwitch(i, switches[i] == 0 ? -1 : 1);
        }
      }
    }

    ImGui::PopStyleVar(3);
    ImGui::EndGroup();
  }
  ImGui::PopID();
}

#if defined(FUNCTION_SWITCHES)
static ImVec4 rgb2rgba(uint32_t col)
{
  float r = (float)((col >> 16) & 0xff) / 255.0f;
  float g = (float)((col >>  8) & 0xff) / 255.0f;
  float b = (float)((col >>  0) & 0xff) / 255.0f;
  return {r, g, b, 1.0f};
}

static inline ImVec4 blend(const ImVec4& a, const ImVec4& b)
{
  float w0 = a.w + b.w;
  return ImVec4((a.x * a.w + b.x * b.w) / w0, (a.y * a.w + b.y * b.w) / w0,
                (a.z * a.w + b.z * b.w) / w0, 1.0f);
}

static void push_custom_switch_styles(int index)
{
  int cfsIdx = switchGetCustomSwitchIdx(index);
  bool sw_on = fsLedState(cfsIdx);
  bool rgb_set = switchIsCustomSwitch(index);
  ImVec4 rgb = rgb2rgba(fsGetLedRGB(cfsIdx));

  ImVec4 btn, border;
  if (sw_on) {
    btn = rgb_set ? rgb : ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab);
    border = ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab);
  } else {
    btn = rgb_set ? rgb : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    border = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
  }

  // color intensity
  float btn_int = (btn.x + btn.y + btn.z) / 3.0f;
  float inv_int = 1.0f - btn_int;
  ImVec4 mask(inv_int, inv_int, inv_int, 0.3f);

  ImGui::PushStyleColor(ImGuiCol_Button, btn);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, blend(btn, mask));
  ImGui::PushStyleColor(ImGuiCol_Border, border);

  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f);
}

static void pop_custom_switch_styles()
{
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar();
}

static void draw_custom_switches()
{
  const float spacing = 4;
  const ImVec2 sw_size(18, 18);

  ImGui::PushID("custom switches");
  {
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 4.0f);

    for (int i = 0, n = 0; i < switchGetMaxSwitches(); i += 1) {
      if (switchIsCustomSwitch(i)) {
        int cfsIdx = switchGetCustomSwitchIdx(i);

        if (n) ImGui::SameLine();
        ImGui::PushID(i);

        push_custom_switch_styles(i);
        ImGui::Button("##csw", sw_size);
        pop_custom_switch_styles();

        bool active = ImGui::IsItemActive();
        if (active || ImGui::IsItemHovered()) {
          const char* name = switchGetDefaultName(i);
          const char* on_off = fsLedState(cfsIdx) ? "on" : "off";
          ImGui::SetTooltip("%s (%s)", name, on_off);
        }
        simuSetSwitch(i, active ? 1 : -1);

        ImGui::PopID();
        n += 1;
      }
    }

    ImGui::PopStyleVar(3);
    ImGui::EndGroup();
  }
  ImGui::PopID();
}
#endif

static float gimbals_width()
{
  return 244.0f;
}

static uint16_t gimbalToAdc(float v)
{
  if (v < 0.f) v = 0.f;
  if (v > 1.f) v = 1.f;
  return (uint16_t)(v * 4096.f);
}

static void draw_gimbals()
{
  GimbalState stick_left = {};
  GimbalState stick_right = {};
  stick_left.pos.x = simuInputsGetAdc(0) / 4096.f;
  stick_left.pos.y = 1.f - simuInputsGetAdc(1) / 4096.f;
  stick_right.pos.y = 1.f - simuInputsGetAdc(2) / 4096.f;
  stick_right.pos.x = simuInputsGetAdc(3) / 4096.f;
  stick_left.lock_y = (g_eeGeneral.stickMode == 1);
  stick_right.lock_y = (g_eeGeneral.stickMode == 0);

  GimbalPair("#gimbals", stick_left, stick_right);

  simuInputsSetAdc(0, gimbalToAdc(stick_left.pos.x));
  simuInputsSetAdc(1, gimbalToAdc(1.f - stick_left.pos.y));
  simuInputsSetAdc(2, gimbalToAdc(1.f - stick_right.pos.y));
  simuInputsSetAdc(3, gimbalToAdc(stick_right.pos.x));
}

static float pots_width()
{
  const float spacing = 2;
  float pot_width = ImGui::GetTextLineHeight() * 4.0f;
  int pots = std::min((int)adcGetMaxInputs(ADC_INPUT_FLEX), 5);
  return pots * pot_width + (pots - 1) * spacing +
         2 * ImGui::GetStyle().CellPadding.x;
}

static int potFromAdc(int idx, uint16_t adc)
{
  if (getPotType(idx) == FLEX_MULTIPOS) return (int)((uint32_t)adc * 5 / 4096);
  return (int)((uint32_t)adc * 200 / 4096) - 100;
}

static uint16_t adcFromPot(int idx, int value)
{
  if (getPotType(idx) == FLEX_MULTIPOS) {
    if (value < 0) value = 0;
    if (value > 5) value = 5;
    return (uint16_t)((uint32_t)value * 4096 / 5);
  }
  if (value < -100) value = -100;
  if (value > 100) value = 100;
  return (uint16_t)(((uint32_t)(value + 100) * 4096) / 200);
}

static void draw_pots()
{
  const float spacing = 2;
  int stickCount = adcGetMaxInputs(ADC_INPUT_MAIN);
  int flexCount = adcGetMaxInputs(ADC_INPUT_FLEX);
  static int shown[MAX_POTS];
  for (int i = 0; i < flexCount && i < MAX_POTS; i++)
    shown[i] = potFromAdc(i, simuInputsGetAdc((uint8_t)(stickCount + i)));

  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
  ImGui::PushID("pots");
  {
    ImGui::BeginGroup();
    int pot_idx = 0;
    for (int i = 0; i < flexCount; i++) {
      if (pot_idx > 0) ImGui::SameLine();
      if (++pot_idx >= 5) pot_idx = 0;

      if (!IS_POT_AVAILABLE(i)) {
        shown[i] = 0;
        auto sz = ImGui::GetTextLineHeight() * 4.0f;
        ImGui::Dummy({sz, sz});
      } else {
        ImGui::PushID(i);
        auto flags = ImGuiKnobFlags_NoTitle | ImGuiKnobFlags_ValueTooltip |
                     ImGuiKnobFlags_TitleTooltip;
        auto label = adcGetInputLabel(ADC_INPUT_FLEX, i);
        switch(getPotType(i)) {
        case FLEX_POT:
        case FLEX_POT_CENTER:
        case FLEX_SLIDER:
          ImGuiKnobs::KnobInt(label, &shown[i], -100, 100, 1, "%d",
                              ImGuiKnobVariant_Tick, 0, flags);
          break;

        case FLEX_MULTIPOS:
          ImGuiKnobs::KnobInt(label, &shown[i], 0, 5, 0.2f, "%d",
                              ImGuiKnobVariant_Stepped, 0, flags, 6);
          break;
        }
        ImGui::PopID();
      }
    }
    ImGui::EndGroup();
  }
  ImGui::PopID();
  ImGui::PopStyleVar();

  for (int i = 0; i < flexCount && i < MAX_POTS; i++)
    simuInputsSetAdc((uint8_t)(stickCount + i), adcFromPot(i, shown[i]));
}

void draw_controls()
{
  ImGui::BeginTable("controls", 3, ImGuiTableFlags_SizingFixedFit);
  ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, switches_width());
  ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, gimbals_width());
  ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, pots_width());

  ImGui::TableNextColumn();
  draw_switches();
#if defined(FUNCTION_SWITCHES)
  draw_custom_switches();
#endif
  
  ImGui::TableNextColumn();
  draw_gimbals();

  ImGui::TableNextColumn();
  draw_pots();

  ImGui::EndTable();
}

ImU32 get_bg_color()
{
  if (LCD_DEPTH < 16) {
    if (isBacklightEnabled()) {
      return IM_COL32(47, 123, 227, 255);
    } else {
      return IM_COL32(200, 200, 200, 255);
    }
  } else {
    return IM_COL32_BLACK_TRANS;
  }
}

static ImVec2 calc_screen_size()
{
  float width, height;

  height = ImGui::GetContentRegionAvail().y;
  width = height * float(LCD_W) / float(LCD_H);

  if (width > ImGui::GetContentRegionAvail().x) {
    width = ImGui::GetContentRegionAvail().x;
    height = width * float(LCD_H) / float(LCD_W);
  }

  return {width, height};
}

static void draw_screen()
{
  const ScreenDesc desc = {
    .width = LCD_W,
    .height = LCD_H,
    .is_dot_matrix = LCD_DEPTH == 1 || LCD_DEPTH == 4,
  };

  auto size = calc_screen_size();
  SimuScreen(desc, (ImTextureID)screen_frame_buffer, size, get_bg_color(),
             (LCD_DEPTH == 16 && !isBacklightEnabled()) ?
             IM_COL32(0,0,0,127) : IM_COL32_BLACK_TRANS);

#if defined(HARDWARE_TOUCH)
  ScreenMouseEvent touch_event;
  if (SimuScreenMouseEvent(desc, touch_event)) {
    if (touch_event.type == ScreenMouseEventType::MouseDown) {
      touchPanelDown(touch_event.pos_x, touch_event.pos_y);
    } else {
      touchPanelUp();
    }
  }
#endif
}

static int get_used_window_height()
{
  return (int)ImGui::GetCursorScreenPos().y;
}

static int calc_min_width()
{
  return switches_width() + gimbals_width() + pots_width() +
         6 * ImGui::GetStyle().CellPadding.x;
}

static void redraw()
{
  refreshDisplay(screen_frame_buffer);
    
  // Start the Dear ImGui frame
  ImGui_ImplSDLRenderer2_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  static ImGuiWindowFlags flags =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_NoTitleBar;

  // Use full work area (without menu-bars, task-bars etc.)
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);

  bool show_win = true;

  if (ImGui::Begin("Main window", &show_win, flags)) {
    draw_controls();
    draw_screen();
  }
  ImGui::End();

  // Rendering
  ImGui::Render();

  ImGuiIO& io = ImGui::GetIO();
  SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x,
                     io.DisplayFramebufferScale.y);

  auto bg_col = ImGui::GetColorU32(ImGuiCol_WindowBg);
  SDL_SetRenderDrawColor(renderer,
                         (bg_col >> IM_COL32_R_SHIFT) & 0xFF,
                         (bg_col >> IM_COL32_G_SHIFT) & 0xFF,
                         (bg_col >> IM_COL32_B_SHIFT) & 0xFF,
                         (bg_col >> IM_COL32_A_SHIFT) & 0xFF);

  SDL_RenderClear(renderer);

  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
  SDL_RenderPresent(renderer);

  // Adjust window size
  if (show_win) {
    int min_w = calc_min_width();
    int w, h;
    SDL_GetWindowMinimumSize(window, &w, &h);
    if (w < min_w || h < 400) {
      SDL_Log("Window width: %d < %d", w, min_w);
      SDL_SetWindowMinimumSize(window, min_w, 400);
    }
  }
}

int main(int argc, char* argv[])
{
  auto progname = std::filesystem::path(argv[0]).filename();
  ArgumentParser args(progname.string());

  if (!args.parse(argc, argv)) {
    return 1;
  }

  if (args.isHelpRequested()) {
    args.printHelp();
    return 0;
  }

  SimuControlConfig control;
  if (args.hasControl()) {
    std::string error;
    if (!simuControlParse(args.getControl(), control, error)) {
      fprintf(stderr, "%s\n", error.c_str());
      return 1;
    }
    simuControlUseStdout(control);
    simuCreateDefaults();
  }

  int window_height = 600;
  if (args.hasHeight()) {
    window_height = args.getHeight();
  }

  int window_width = 800;
  if (args.hasWidth()) {
    window_width = args.getWidth();
  }

  // Setup SDL
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    printf("Error: %s\n", SDL_GetError());
    return -1;
  }

  simuAudioInit();
  
  // From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
  SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

  // Create window with SDL_Renderer graphics context
  int window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
  window = SDL_CreateWindow("EdgeTx Simu", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, window_width, window_height,
                            window_flags);

  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (renderer == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "cannot create SDL_Renderer!");
    return 0;
  }

  SDL_RendererInfo info;
  SDL_GetRendererInfo(renderer, &info);
  SDL_Log("Current SDL_Renderer: %s", info.name);

#if !defined(__EMSCRIPTEN__)
  SDL_Surface* icon = LoadImage(_icon_png, sizeof(_icon_png));
  if (window && icon) {
    SDL_SetWindowIcon(window, icon);
  }
#endif

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  // Setup Dear ImGui style
  ImGui::StyleColorsLight();
  // ImGui::StyleColorsDark();

  // Setup Platform/Renderer backends
  ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer2_Init(renderer);

  // Create textures for radio screen
  screen_frame_buffer =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                        SDL_TEXTUREACCESS_STREAMING, LCD_W, LCD_H);

  if (!screen_frame_buffer) {
    SDL_Log("Could not create frame buffer textures");
    return 0;
  }

  // SDL_SetTextureScaleMode(screen_frame_buffer, SDL_ScaleModeBest);
  SDL_SetTextureBlendMode(screen_frame_buffer, SDL_BLENDMODE_BLEND);

  // Init simulation
  simuInit();
  simuInputsSetThrottleLow(simuInputsInferStickMode(args.getStoragePath()));
  simuFatfsSetPaths(args.getStoragePath().c_str(),
                    args.getSettingsPath().c_str());
  simuStart(control.mode == SimuControlConfig::Mode::None);

  std::thread controlThread;
  if (control.mode != SimuControlConfig::Mode::None) {
    controlThread = std::thread([control] { simuControlServe(control); });
  }

  // Main loop
  SDL_SetEventFilter([](void*, SDL_Event* event){
    if (event->type == SDL_WINDOWEVENT &&
        (event->window.event == SDL_WINDOWEVENT_EXPOSED)) {
      redraw();
      return 0;
    }
    return 1;
  }, NULL);

#if defined(__EMSCRIPTEN__)
  emscripten_set_main_loop([]() { handleEvents(); }, 0, true);
#else
  do {
    if (simuControlStopRequested()) break;
    Uint64 start_ts = SDL_GetPerformanceCounter();
    if (!handleEvents()) break;

    Uint64 end_ts = SDL_GetPerformanceCounter();
    float elapsedMS =
      (end_ts - start_ts) / (float)SDL_GetPerformanceFrequency() * 1000.0f;

    // Cap to 60 FPS
    SDL_Delay(std::max(0,(int32_t)floor(16.666f - elapsedMS)));

  } while(true);
#endif

  if (controlThread.joinable()) {
    simuControlRequestStop();
    controlThread.join();
  }

  // App cleanup
  simuStop();

  // Cleanup
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  SDL_DestroyTexture(screen_frame_buffer);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
#if !defined(__EMSCRIPTEN__)
  SDL_FreeSurface(icon);
#endif
  SDL_CloseAudio();
  SDL_Quit();
  
  return 0;
}

void simuLcdNotify() {}
