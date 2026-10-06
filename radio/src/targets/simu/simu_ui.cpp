/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * Applies show_telemetry / show_widget on the menus task. The control
 * thread only posts a request.
 */

#include "simulib.h"

#include "edgetx.h"

#include <atomic>
#include <cstring>
#include <mutex>

#if defined(COLORLCD)
#include "layout.h"
#include "view_main.h"
#include "widget.h"
#endif

namespace {

enum Kind { KIND_NONE, KIND_TELEMETRY, KIND_WIDGET };

std::mutex g_mu;
std::atomic<int> g_status{0};  // 0 idle, 1 pending, 2 ok, 3 error
std::atomic<uint32_t> g_telemetryRuns{0};
std::atomic<uint32_t> g_widgetRefreshes{0};
char g_name[64];
char g_error[160];
Kind g_kind = KIND_NONE;
bool g_applied = false;
uint32_t g_mark = 0;

void setError(const char* text)
{
  strncpy(g_error, text, sizeof(g_error) - 1);
  g_error[sizeof(g_error) - 1] = '\0';
  g_status.store(3);
}

bool post(Kind kind, const char* name, const char** error)
{
  if (!name || !name[0]) {
    if (error) *error = "missing name";
    return false;
  }
  std::lock_guard<std::mutex> lock(g_mu);
  int st = g_status.load();
  if (st == 1) {
    if (error) *error = "ui request already in progress";
    return false;
  }
  strncpy(g_name, name, sizeof(g_name) - 1);
  g_name[sizeof(g_name) - 1] = '\0';
  g_error[0] = '\0';
  g_kind = kind;
  g_applied = false;
  g_mark = 0;
  g_status.store(1);
  return true;
}

#if defined(PCBTARANIS) && !defined(COLORLCD)
bool scriptNameEquals(const char* stored, const char* name)
{
  size_t n = strlen(name);
  if (n == 0 || n > LEN_SCRIPT_FILENAME) return false;
  if (memcmp(stored, name, n) != 0) return false;
  for (size_t i = n; i < LEN_SCRIPT_FILENAME; i++) {
    if (stored[i] != 0 && stored[i] != ' ') return false;
  }
  return true;
}

void applyTelemetry()
{
  if (strlen(g_name) > LEN_SCRIPT_FILENAME) {
    setError("telemetry script name is longer than 6 characters");
    return;
  }

  int found = -1;
  for (int i = 0; i < MAX_TELEMETRY_SCREENS; i++) {
    if (TELEMETRY_SCREEN_TYPE(i) == TELEMETRY_SCREEN_TYPE_SCRIPT &&
        scriptNameEquals(g_model.screens[i].script.file, g_name)) {
      found = i;
      break;
    }
  }

  if (found < 0) {
    found = 0;
    g_model.screensType =
        (g_model.screensType & ~(0x03 << (2 * found))) |
        (TELEMETRY_SCREEN_TYPE_SCRIPT << (2 * found));
    memset(g_model.screens[found].script.file, 0, LEN_SCRIPT_FILENAME);
    memcpy(g_model.screens[found].script.file, g_name, strlen(g_name));
    simuLuaReloadPermanentScripts();
  }

  while (menuLevel > 0) popMenu();
  selectedTelemView = (uint8_t)found;
  chainMenu(menuViewTelemetry);
  g_mark = g_telemetryRuns.load();
  g_applied = true;
}
#endif

#if defined(COLORLCD)
void applyWidget()
{
  if (!customScreens[0]) LayoutFactory::loadDefaultLayout();
  auto* screen = customScreens[0];
  if (!screen) return;

  const WidgetFactory* factory = WidgetFactory::getWidgetFactory(g_name);
  if (!factory) {
    // Widgets are registered during startup. Keep waiting until the main
    // view exists and the factory list has been filled; a missing name
    // after that is a load failure.
    if (ViewMain::instance() && ViewMain::instance()->getMainViewsCount() > 0) {
      setError("widget not loaded");
    }
    return;
  }

  if (screen->getZonesCount() < 1) {
    setError("main screen has no widget zone");
    return;
  }

  Widget* existing = screen->getWidget(0);
  if (!existing || !existing->getFactory() ||
      strcmp(existing->getFactory()->getName(), g_name) != 0) {
    if (!screen->createWidget(0, factory)) {
      setError("could not place widget");
      return;
    }
  }

  if (ViewMain::instance()) ViewMain::instance()->setCurrentMainView(0);
  g_mark = g_widgetRefreshes.load();
  g_applied = true;
}
#endif

}  // namespace

void simuNoteTelemetryRun() { g_telemetryRuns.fetch_add(1); }

void simuNoteWidgetRefresh() { g_widgetRefreshes.fetch_add(1); }

uint32_t simuTelemetryRunCount() { return g_telemetryRuns.load(); }

uint32_t simuWidgetRefreshCount() { return g_widgetRefreshes.load(); }

bool simuUiPostTelemetry(const char* name, const char** error)
{
#if !defined(PCBTARANIS) || defined(COLORLCD)
  (void)name;
  if (error) *error = "telemetry scripts are not available on this firmware";
  return false;
#else
  return post(KIND_TELEMETRY, name, error);
#endif
}

bool simuUiPostWidget(const char* name, const char** error)
{
#if !defined(COLORLCD)
  (void)name;
  if (error) *error = "widgets are not available on this firmware";
  return false;
#else
  return post(KIND_WIDGET, name, error);
#endif
}

void simuUiCancel()
{
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_status.load() == 1) g_status.store(0);
}

int simuUiStatus() { return g_status.load(); }

const char* simuUiStatusError() { return g_error; }

void simuPollUiRequests()
{
  if (g_status.load() != 1) return;
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_status.load() != 1) return;

  if (!g_applied) {
#if defined(PCBTARANIS) && !defined(COLORLCD)
    if (g_kind == KIND_TELEMETRY) applyTelemetry();
#endif
#if defined(COLORLCD)
    if (g_kind == KIND_WIDGET) applyWidget();
#endif
#if !defined(PCBTARANIS) || defined(COLORLCD)
    if (g_kind == KIND_TELEMETRY && g_status.load() == 1) {
      setError("telemetry scripts are not available on this firmware");
    }
#endif
#if !defined(COLORLCD)
    if (g_kind == KIND_WIDGET && g_status.load() == 1) {
      setError("widgets are not available on this firmware");
    }
#endif
    return;
  }

  if (g_kind == KIND_TELEMETRY) {
    if (g_telemetryRuns.load() != g_mark) g_status.store(2);
  } else if (g_kind == KIND_WIDGET) {
    if (g_widgetRefreshes.load() != g_mark) g_status.store(2);
  }
}
