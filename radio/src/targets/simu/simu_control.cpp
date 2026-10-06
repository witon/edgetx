/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#include "simu_control.h"
#include "simu_hostio.h"

#include "debug.h"
#include "edgetx.h"
#include "hal/adc_driver.h"
#include "hal/key_driver.h"
#include "hal/switch_driver.h"
#include "simu_inputs.h"
#include "simu_json.h"
#include "simu_png.h"
#include "simu_trace.h"
#include "simulcd.h"
#include "simulib.h"

#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};

bool ieq(const std::string& a, const char* b)
{
  size_t i = 0;
  for (; i < a.size() && b[i]; i++) {
    if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
  }
  return i == a.size() && b[i] == 0;
}

struct KeyName {
  const char* name;
  uint8_t key;
};

const KeyName kKeys[] = {
    {"MENU", KEY_MENU},   {"EXIT", KEY_EXIT},   {"ENTER", KEY_ENTER},
    {"PAGEUP", KEY_PAGEUP}, {"PAGEDN", KEY_PAGEDN}, {"PAGE", KEY_PAGEDN},
    {"UP", KEY_UP},       {"DOWN", KEY_DOWN},   {"LEFT", KEY_LEFT},
    {"RIGHT", KEY_RIGHT}, {"PLUS", KEY_PLUS},   {"MINUS", KEY_MINUS},
    {"MODEL", KEY_MODEL}, {"TELE", KEY_TELE},   {"SYS", KEY_SYS},
    {"SHIFT", KEY_SHIFT}, {"BIND", KEY_BIND},
};

bool lookupKey(const std::string& name, uint8_t& key)
{
  for (const auto& item : kKeys) {
    if (!ieq(name, item.name)) continue;
    if (item.key >= MAX_KEYS) return false;
    if (!(keysGetSupported() & (1u << item.key))) return false;
    key = item.key;
    return true;
  }
  return false;
}

bool lookupSwitch(const std::string& name, uint8_t& idx)
{
  int n = switchGetMaxSwitches();
  for (int i = 0; i < n; i++) {
    const char* sw = switchGetDefaultName((uint8_t)i);
    if (sw && ieq(name, sw)) {
      idx = (uint8_t)i;
      return true;
    }
  }
  return false;
}

bool switchState(const std::string& text, int8_t& state)
{
  if (ieq(text, "up")) {
    state = -1;
    return true;
  }
  if (ieq(text, "mid") || ieq(text, "middle")) {
    state = 0;
    return true;
  }
  if (ieq(text, "down")) {
    state = 1;
    return true;
  }
  return false;
}

bool luaErrorLine(const std::string& line)
{
  return line.find("-E- ") != std::string::npos ||
         line.find("Error loading script") != std::string::npos ||
         line.find("Error parsing script") != std::string::npos;
}

std::string idPrefix(const Json& req)
{
  const Json* id = req.get("id");
  if (!id) return {};
  std::ostringstream os;
  os << "\"id\":";
  if (id->type == Json::String) {
    os << '"' << jsonEscape(id->str) << '"';
  } else if (id->type == Json::Number && id->num == (int)id->num) {
    os << (int)id->num;
  } else if (id->type == Json::Number) {
    os << id->num;
  } else {
    return {};
  }
  os << ',';
  return os.str();
}

std::string fail(const Json& req, const char* code, const std::string& error)
{
  std::ostringstream os;
  os << '{' << idPrefix(req) << "\"ok\":false,\"code\":\"" << code
     << "\",\"error\":\"" << jsonEscape(error) << "\"}";
  return os.str();
}

std::string okHead(const Json& req)
{
  return std::string("{") + idPrefix(req) + "\"ok\":true";
}

std::string linesJson(const std::vector<std::string>& lines)
{
  std::ostringstream os;
  os << '[';
  for (size_t i = 0; i < lines.size(); i++) {
    if (i) os << ',';
    os << '"' << jsonEscape(lines[i]) << '"';
  }
  os << ']';
  return os.str();
}

std::string intsJson(const int* values, int n)
{
  std::ostringstream os;
  os << '[';
  for (int i = 0; i < n; i++) {
    if (i) os << ',';
    os << values[i];
  }
  os << ']';
  return os.str();
}

void appendLog(std::vector<std::string>& all, uint32_t& cursor)
{
  std::vector<std::string> lines;
  uint32_t next = cursor;
  simuTraceRead(cursor, lines, next);
  cursor = next;
  all.insert(all.end(), lines.begin(), lines.end());
}

bool logHasError(const std::vector<std::string>& lines, std::string& hit)
{
  for (const auto& line : lines) {
    if (luaErrorLine(line)) {
      hit = line;
      return true;
    }
  }
  return false;
}

std::string withLog(const Json& req, const char* code, const std::string& error,
                    const std::vector<std::string>& lines)
{
  std::ostringstream os;
  os << '{' << idPrefix(req) << "\"ok\":false,\"code\":\"" << code
     << "\",\"error\":\"" << jsonEscape(error) << "\",\"lines\":" << linesJson(lines)
     << '}';
  return os.str();
}

int timeoutMs(const Json& req, int fallback)
{
  int ms = fallback;
  if (!req.getInt("timeout", ms) || ms < 0) ms = fallback;
  return ms;
}

std::string waitForUi(const Json& req)
{
  uint32_t cursor = simuTraceCursor();
  std::vector<std::string> seen;
  auto start = std::chrono::steady_clock::now();
  int limit = timeoutMs(req, 5000);
  while (!g_stop.load()) {
    appendLog(seen, cursor);
    std::string hit;
    if (logHasError(seen, hit)) return withLog(req, "lua_error", hit, seen);
    int st = simuUiStatus();
    if (st == 2) return okHead(req) + "}";
    if (st == 3) return withLog(req, "lua_error", simuUiStatusError(), seen);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    if (elapsed >= limit) {
      simuUiCancel();
      return withLog(req, "timeout", "timed out waiting for script", seen);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  simuUiCancel();
  return fail(req, "stopped", "simulator stopping");
}

std::string cmdKey(const Json& req)
{
  std::string name = req.getString("name");
  uint8_t key = 0;
  if (!lookupKey(name, key)) return fail(req, "bad_name", "unknown key");
  std::string action = req.getString("action");
  if (action.empty()) action = "click";
  if (ieq(action, "down")) {
    simuSetKey(key, true);
  } else if (ieq(action, "up")) {
    simuSetKey(key, false);
  } else if (ieq(action, "click")) {
    simuSetKey(key, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    simuSetKey(key, false);
  } else {
    return fail(req, "bad_name", "action must be down, up, or click");
  }
  return okHead(req) + "}";
}

std::string cmdSwitch(const Json& req)
{
  uint8_t idx = 0;
  if (!lookupSwitch(req.getString("name"), idx))
    return fail(req, "bad_name", "unknown switch");
  int8_t state = 0;
  if (!switchState(req.getString("state"), state))
    return fail(req, "bad_name", "state must be up, mid, or down");
  simuSetSwitch(idx, state);
  return okHead(req) + "}";
}

std::string cmdStick(const Json& req)
{
  int value = 0;
  if (!req.getInt("value", value)) return fail(req, "bad_name", "stick needs an integer value");
  const char* error = nullptr;
  if (!simuInputsSetLogicalStick(req.getString("name").c_str(), value, &error))
    return fail(req, "bad_name", error ? error : "bad stick");
  return okHead(req) + "}";
}

std::string cmdPot(const Json& req)
{
  int value = 0;
  if (!req.getInt("value", value)) return fail(req, "bad_name", "pot needs an integer value");
  int idx = -1;
  if (!req.getInt("index", idx)) {
    std::string name = req.getString("name");
    int pots = adcGetMaxInputs(ADC_INPUT_FLEX);
    for (int i = 0; i < pots; i++) {
      const char* label = adcGetInputLabel(ADC_INPUT_FLEX, (uint8_t)i);
      if (label && ieq(name, label)) {
        idx = i;
        break;
      }
    }
  }
  const char* error = nullptr;
  if (!simuInputsSetPot(idx, value, &error))
    return fail(req, "bad_name", error ? error : "bad pot");
  return okHead(req) + "}";
}

std::string cmdAnalog(const Json& req)
{
  int idx = 0;
  int value = 0;
  if (!req.getInt("index", idx) || !req.getInt("value", value))
    return fail(req, "bad_name", "analog needs index and value");
  if (idx < 0 || idx >= MAX_ANALOG_INPUTS || value < 0 || value > 4096)
    return fail(req, "bad_name", "analog index or value is out of range");
  simuInputsSetAdc((uint8_t)idx, (uint16_t)value);
  return okHead(req) + "}";
}

std::string cmdTrim(const Json& req)
{
  int idx = 0;
  if (!req.getInt("index", idx) || idx < 0) return fail(req, "bad_name", "trim needs index");
  int value = 0;
  if (req.getInt("value", value)) {
    if (idx >= MAX_STICKS) return fail(req, "bad_name", "trim index out of range");
    simuSetTrimValue((uint8_t)idx, value);
    return okHead(req) + "}";
  }
  if (idx >= (int)keysGetMaxTrims() * 2) return fail(req, "bad_name", "trim index out of range");
  std::string action = req.getString("action");
  if (action.empty()) action = "click";
  if (ieq(action, "down")) simuSetTrim((uint8_t)idx, true);
  else if (ieq(action, "up")) simuSetTrim((uint8_t)idx, false);
  else if (ieq(action, "click")) {
    simuSetTrim((uint8_t)idx, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    simuSetTrim((uint8_t)idx, false);
  } else {
    return fail(req, "bad_name", "trim action must be down, up, or click");
  }
  return okHead(req) + "}";
}

std::string cmdEncoder(const Json& req)
{
  int steps = 0;
  if (!req.getInt("steps", steps)) return fail(req, "bad_name", "encoder needs steps");
  simuRotaryEncoderEvent(steps);
  return okHead(req) + "}";
}

std::string cmdTouch(const Json& req)
{
#if !defined(HARDWARE_TOUCH)
  return fail(req, "bad_cmd", "touch is not available on this firmware");
#else
  std::string action = req.getString("action");
  if (ieq(action, "up")) {
    simuTouchUp();
    return okHead(req) + "}";
  }
  int x = 0, y = 0;
  if (!req.getInt("x", x) || !req.getInt("y", y))
    return fail(req, "bad_name", "touch needs x and y");
  simuTouchDown((int16_t)x, (int16_t)y);
  return okHead(req) + "}";
#endif
}

std::string cmdVoltage(const Json& req)
{
  int value = 0;
  if (!req.getInt("value", value)) return fail(req, "bad_name", "voltage needs value");
  simuSetTxVoltage(value);
  return okHead(req) + "}";
}

std::string cmdWait(const Json& req)
{
  int ms = 0;
  if (!req.getInt("ms", ms) || ms < 0) return fail(req, "bad_name", "wait needs ms");
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  return okHead(req) + "}";
}

std::string cmdLog(const Json& req)
{
  int since = 0;
  req.getInt("since", since);
  if (since < 0) since = 0;
  std::vector<std::string> lines;
  uint32_t next = 0;
  simuTraceRead((uint32_t)since, lines, next);
  std::ostringstream os;
  os << okHead(req) << ",\"next\":" << next << ",\"lines\":" << linesJson(lines) << '}';
  return os.str();
}

bool captureRgb(std::vector<uint8_t>& rgb, int& w, int& h, std::string& error)
{
  w = (int)simuLcdGetWidth();
  h = (int)simuLcdGetHeight();
  if (w <= 0 || h <= 0) {
    error = "lcd not ready";
    return false;
  }
#if defined(COLORLCD)
  if (!simuLcdBuf) {
    error = "lcd not ready";
    return false;
  }
#endif
  rgb.assign((size_t)w * (size_t)h * 3, 255);
#if LCD_DEPTH == 16
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      pixel_t z = simuLcdBuf[y * w + x];
      uint8_t* px = &rgb[((size_t)y * w + x) * 3];
      px[0] = ((z & 0xF800) >> 8) + ((z & 0xE000) >> 13);
      px[1] = ((z & 0x07E0) >> 3) + ((z & 0x0600) >> 9);
      px[2] = ((z & 0x001F) << 3) + ((z & 0x001C) >> 2);
    }
  }
#elif LCD_DEPTH == 4
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      pixel_t p = simuLcdBuf[(y / 2) * w + x];
      uint8_t z = (y & 1) ? (p >> 4) : (p & 0x0F);
      uint8_t ink = (uint8_t)(255 - (z * 255) / 15);
      uint8_t* px = &rgb[((size_t)y * w + x) * 3];
      px[0] = px[1] = px[2] = ink;
    }
  }
#else
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      bool on = simuLcdBuf[x + (y / 8) * w] & (1 << (y % 8));
      uint8_t* px = &rgb[((size_t)y * w + x) * 3];
      px[0] = px[1] = px[2] = on ? 0 : 255;
    }
  }
#endif
  return true;
}

std::string cmdScreenshot(const Json& req)
{
  std::string path = req.getString("path");
  if (path.empty()) return fail(req, "bad_name", "screenshot needs path");
  std::vector<uint8_t> rgb;
  int w = 0, h = 0;
  std::string error;
  if (!captureRgb(rgb, w, h, error)) return fail(req, "lcd", error);
  if (!simuWriteRgbPng(path.c_str(), rgb.data(), w, h))
    return fail(req, "io", "could not write png");
  std::ostringstream os;
  os << okHead(req) << ",\"width\":" << w << ",\"height\":" << h << '}';
  return os.str();
}

std::string cmdChannels(const Json& req, bool mixes)
{
  int16_t buf[MAX_OUTPUT_CHANNELS];
  uint8_t n = mixes ? simuCopyMixOutputs(buf, MAX_OUTPUT_CHANNELS)
                    : simuCopyChannelOutputs(buf, MAX_OUTPUT_CHANNELS);
  int values[MAX_OUTPUT_CHANNELS];
  for (uint8_t i = 0; i < n; i++) values[i] = buf[i];
  std::ostringstream os;
  os << okHead(req) << ",\"" << (mixes ? "mixes" : "channels") << "\":"
     << intsJson(values, n) << '}';
  return os.str();
}

std::string cmdLogical(const Json& req)
{
  uint8_t buf[MAX_LOGICAL_SWITCHES];
  uint8_t n = simuCopyLogicalSwitches(buf, MAX_LOGICAL_SWITCHES);
  int values[MAX_LOGICAL_SWITCHES];
  for (uint8_t i = 0; i < n; i++) values[i] = buf[i];
  return okHead(req) + ",\"logical_switches\":" + intsJson(values, n) + "}";
}

std::string cmdTrims(const Json& req)
{
  int values[MAX_STICKS];
  int n = MAX_STICKS;
  for (int i = 0; i < n; i++) values[i] = simuGetTrimValue((uint8_t)i);
  return okHead(req) + ",\"trims\":" + intsJson(values, n) + "}";
}

std::string cmdFlightMode(const Json& req)
{
  std::ostringstream os;
  os << okHead(req) << ",\"flight_mode\":" << simuGetFlightMode() << '}';
  return os.str();
}

std::string cmdGvars(const Json& req)
{
  int fm = simuGetFlightMode();
  int n = simuGetNumGVars();
  std::vector<int> values(n);
  for (int i = 0; i < n; i++) {
    int32_t encoded = simuGetGVar((uint8_t)i, (uint8_t)fm);
    values[i] = (int16_t)(encoded & 0xFFFF);
  }
  std::ostringstream os;
  os << okHead(req) << ",\"flight_mode\":" << fm
     << ",\"gvars\":" << intsJson(values.data(), n) << '}';
  return os.str();
}

std::string cmdStatus(const Json& req)
{
  std::ostringstream os;
  os << okHead(req) << ",\"running\":" << (simuIsRunning() ? "true" : "false")
     << ",\"width\":" << simuLcdGetWidth() << ",\"height\":" << simuLcdGetHeight()
     << ",\"depth\":" << simuLcdGetDepth() << ",\"log_next\":" << simuTraceCursor()
     << '}';
  return os.str();
}

std::string jsonStringArray(const std::vector<std::string>& items)
{
  return linesJson(items);
}

std::string cmdHelp(const Json& req)
{
  std::vector<std::string> keys;
  for (const auto& item : kKeys) {
    if (item.key < MAX_KEYS && (keysGetSupported() & (1u << item.key)))
      keys.push_back(item.name);
  }
  std::vector<std::string> switches;
  int nsw = switchGetMaxSwitches();
  for (int i = 0; i < nsw; i++) {
    const char* sw = switchGetDefaultName((uint8_t)i);
    if (sw && sw[0]) switches.push_back(sw);
  }
  std::vector<std::string> sticks;
#if defined(SURFACE_RADIO)
  sticks = {"st", "th"};
#else
  sticks = {"rud", "ele", "thr", "ail"};
#endif
  std::vector<std::string> pots;
  int np = adcGetMaxInputs(ADC_INPUT_FLEX);
  for (int i = 0; i < np; i++) {
    const char* label = adcGetInputLabel(ADC_INPUT_FLEX, (uint8_t)i);
    if (label && label[0]) pots.push_back(label);
  }
  const char* commands =
      "[\"help\",\"status\",\"key\",\"switch\",\"stick\",\"pot\",\"analog\","
      "\"trim\",\"encoder\",\"touch\",\"voltage\",\"wait\",\"log\",\"trace\","
      "\"screenshot\",\"channels\",\"mixes\",\"logical_switches\",\"trims\","
      "\"flight_mode\",\"gvars\",\"reload_lua\",\"show_telemetry\",\"show_widget\","
      "\"stop\"]";
  std::ostringstream os;
  os << okHead(req) << ",\"commands\":" << commands
     << ",\"keys\":" << jsonStringArray(keys)
     << ",\"switches\":" << jsonStringArray(switches)
     << ",\"sticks\":" << jsonStringArray(sticks)
     << ",\"pots\":" << jsonStringArray(pots) << '}';
  return os.str();
}

std::string cmdShow(const Json& req, bool widget)
{
  std::string name = req.getString("name");
  if (name.empty()) name = req.getString("script");
  const char* error = nullptr;
  bool posted = widget ? simuUiPostWidget(name.c_str(), &error)
                       : simuUiPostTelemetry(name.c_str(), &error);
  if (!posted) return fail(req, "bad_name", error ? error : "could not open script");
  return waitForUi(req);
}

std::string handle(const Json& req)
{
  if (req.type != Json::Object) return fail(req, "bad_json", "request must be an object");
  std::string cmd = req.getString("cmd");
  if (cmd.empty()) return fail(req, "bad_cmd", "missing cmd");
  if (ieq(cmd, "help")) return cmdHelp(req);
  if (ieq(cmd, "status")) return cmdStatus(req);
  if (ieq(cmd, "key")) return cmdKey(req);
  if (ieq(cmd, "switch")) return cmdSwitch(req);
  if (ieq(cmd, "stick")) return cmdStick(req);
  if (ieq(cmd, "pot")) return cmdPot(req);
  if (ieq(cmd, "analog")) return cmdAnalog(req);
  if (ieq(cmd, "trim")) return cmdTrim(req);
  if (ieq(cmd, "encoder")) return cmdEncoder(req);
  if (ieq(cmd, "touch")) return cmdTouch(req);
  if (ieq(cmd, "voltage")) return cmdVoltage(req);
  if (ieq(cmd, "wait")) return cmdWait(req);
  if (ieq(cmd, "log") || ieq(cmd, "trace")) return cmdLog(req);
  if (ieq(cmd, "screenshot")) return cmdScreenshot(req);
  if (ieq(cmd, "channels")) return cmdChannels(req, false);
  if (ieq(cmd, "mixes")) return cmdChannels(req, true);
  if (ieq(cmd, "logical_switches")) return cmdLogical(req);
  if (ieq(cmd, "trims")) return cmdTrims(req);
  if (ieq(cmd, "flight_mode")) return cmdFlightMode(req);
  if (ieq(cmd, "gvars")) return cmdGvars(req);
  if (ieq(cmd, "reload_lua")) {
    simuLuaReloadPermanentScripts();
    return okHead(req) + "}";
  }
  if (ieq(cmd, "show_telemetry")) return cmdShow(req, false);
  if (ieq(cmd, "show_widget")) return cmdShow(req, true);
  if (ieq(cmd, "stop")) {
    g_stop.store(true);
    return okHead(req) + "}";
  }
  return fail(req, "bad_cmd", "unknown command");
}

void writeLine(FILE* out, const std::string& line)
{
  fwrite(line.data(), 1, line.size(), out);
  fputc('\n', out);
  fflush(out);
}

bool readStdioLine(std::string& line, bool& closed)
{
  return simuHostReadStdioLine(line, closed, g_stop);
}

void serveStdio()
{
  writeLine(stdout, std::string("{\"event\":\"ready\",\"control\":\"stdio\"}"));
  while (!g_stop.load()) {
    std::string line;
    bool closed = false;
    if (!readStdioLine(line, closed)) break;
    if (line.empty()) continue;
    Json req;
    std::string error;
    std::string response;
    if (!jsonParse(line, req, error)) response = std::string("{\"ok\":false,\"code\":\"bad_json\",\"error\":\"") + jsonEscape(error) + "\"}";
    else response = handle(req);
    writeLine(stdout, response);
  }
}

void serveTcp(const SimuControlConfig& cfg)
{
  std::string error;
  if (!simuHostNetStartup(error)) {
    fprintf(stderr, "%s\n", error.c_str());
    return;
  }
  if (!simuHostListen(cfg.host, cfg.port, error)) {
    fprintf(stderr, "%s on %s\n", error.c_str(), cfg.describe().c_str());
    simuHostNetShutdown();
    return;
  }
  fprintf(stdout, "{\"event\":\"ready\",\"control\":\"%s\"}\n", cfg.describe().c_str());
  fflush(stdout);

  while (!g_stop.load()) {
    if (!simuHostAccept(g_stop)) break;
    while (!g_stop.load()) {
      std::string line;
      bool closed = false;
      if (!simuHostReadClientLine(line, closed, g_stop)) break;
      if (line.empty()) continue;
      Json req;
      std::string parseError;
      std::string response;
      if (!jsonParse(line, req, parseError))
        response = std::string("{\"ok\":false,\"code\":\"bad_json\",\"error\":\"") + jsonEscape(parseError) + "\"}";
      else
        response = handle(req);
      if (!simuHostSendLine(response)) break;
      if (g_stop.load()) break;
    }
    simuHostCloseClient();
  }
  simuHostCloseListen();
  simuHostNetShutdown();
}

}  // namespace

std::string SimuControlConfig::describe() const
{
  if (mode == Mode::Stdio) return "stdio";
  if (mode == Mode::Tcp) {
    std::ostringstream os;
    os << "tcp://" << (host.empty() ? "127.0.0.1" : host) << ':' << port;
    return os.str();
  }
  return "none";
}

bool simuControlParse(const std::string& spec, SimuControlConfig& cfg, std::string& error)
{
  if (spec.empty() || spec == "stdio") {
    cfg.mode = SimuControlConfig::Mode::Stdio;
    return true;
  }
  std::string rest = spec;
  if (rest.rfind("tcp://", 0) == 0) rest = rest.substr(6);
  auto colon = rest.rfind(':');
  if (colon == std::string::npos) {
    error = "control must be stdio or tcp://host:port";
    return false;
  }
  cfg.mode = SimuControlConfig::Mode::Tcp;
  cfg.host = rest.substr(0, colon);
  if (cfg.host.empty()) cfg.host = "127.0.0.1";
  char* end = nullptr;
  long port = strtol(rest.c_str() + colon + 1, &end, 10);
  if (!end || *end || port <= 0 || port > 65535) {
    error = "invalid control port";
    return false;
  }
  cfg.port = (int)port;
  return true;
}

void simuControlUseStdout(const SimuControlConfig& cfg)
{
  simuTraceToStdout = cfg.mode != SimuControlConfig::Mode::Stdio;
}

int simuControlServe(const SimuControlConfig& cfg)
{
  g_stop.store(false);
  if (cfg.mode == SimuControlConfig::Mode::None) return 0;
  if (cfg.mode == SimuControlConfig::Mode::Stdio) serveStdio();
  else serveTcp(cfg);
  return 0;
}

void simuControlRequestStop()
{
  g_stop.store(true);
  if (g_client != kInvalidSock) {
#ifdef _WIN32
    shutdown(g_client, SD_BOTH);
#else
    shutdown(g_client, SHUT_RDWR);
#endif
  }
  if (g_listen != kInvalidSock) closeSock(g_listen);
}

bool simuControlStopRequested() { return g_stop.load(); }
