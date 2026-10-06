/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#include "simu_trace.h"

#include "simulib.h"

#include <deque>
#include <mutex>

namespace {

std::mutex g_mu;
std::deque<std::string> g_lines;
std::string g_partial;
uint32_t g_first = 1;  // sequence number of g_lines.front()
constexpr size_t kMaxLines = 4000;

void pushLine(const std::string& line)
{
  g_lines.push_back(line);
  if (g_lines.size() > kMaxLines) {
    g_lines.pop_front();
    g_first++;
  }
}

}  // namespace

void simuTrace(const char* text)
{
  if (!text) return;
  std::lock_guard<std::mutex> lock(g_mu);
  for (const char* p = text; *p; ++p) {
    if (*p == '\n') {
      pushLine(g_partial);
      g_partial.clear();
    } else if (*p != '\r') {
      if (g_partial.size() < 2000) g_partial.push_back(*p);
    }
  }
}

uint32_t simuTraceCursor()
{
  std::lock_guard<std::mutex> lock(g_mu);
  return g_first + (uint32_t)g_lines.size();
}

void simuTraceRead(uint32_t since, std::vector<std::string>& lines, uint32_t& next)
{
  std::lock_guard<std::mutex> lock(g_mu);
  lines.clear();
  uint32_t last = g_first + (uint32_t)g_lines.size();
  if (since < g_first) since = g_first;
  if (since > last) since = last;
  for (uint32_t seq = since; seq < last; seq++) {
    lines.push_back(g_lines[seq - g_first]);
  }
  next = last;
}
