/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#include "simu_hostio.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
using socket_t = SOCKET;
static const socket_t kInvalidSock = INVALID_SOCKET;
static void closeSock(socket_t s)
{
  if (s != INVALID_SOCKET) closesocket(s);
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static const socket_t kInvalidSock = -1;
static void closeSock(socket_t s)
{
  if (s >= 0) close(s);
}
#endif

static socket_t g_listen = kInvalidSock;
static socket_t g_client = kInvalidSock;

static bool waitReadable(socket_t sock, int timeoutMs)
{
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(sock, &fds);
  timeval tv;
  tv.tv_sec = timeoutMs / 1000;
  tv.tv_usec = (timeoutMs % 1000) * 1000;
#ifdef _WIN32
  int rc = select(0, &fds, nullptr, nullptr, &tv);
#else
  int rc = select(sock + 1, &fds, nullptr, nullptr, &tv);
#endif
  return rc > 0;
}

bool simuHostNetStartup(std::string& error)
{
#ifdef _WIN32
  WSADATA data;
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    error = "WSAStartup failed";
    return false;
  }
#else
  (void)error;
#endif
  return true;
}

void simuHostNetShutdown()
{
#ifdef _WIN32
  WSACleanup();
#endif
}

bool simuHostListen(const std::string& host, int port, std::string& error)
{
  g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (g_listen == kInvalidSock) {
    error = "could not create socket";
    return false;
  }
  int yes = 1;
  setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  if (host.empty() || host == "0.0.0.0") addr.sin_addr.s_addr = htonl(INADDR_ANY);
  else inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
  if (bind(g_listen, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(g_listen, 1) != 0) {
    error = "could not listen";
    closeSock(g_listen);
    g_listen = kInvalidSock;
    return false;
  }
  return true;
}

void simuHostCloseListen()
{
  closeSock(g_listen);
  g_listen = kInvalidSock;
}

void simuHostWake()
{
  if (g_client != kInvalidSock) {
#ifdef _WIN32
    shutdown(g_client, SD_BOTH);
#else
    shutdown(g_client, SHUT_RDWR);
#endif
  }
  if (g_listen != kInvalidSock) closeSock(g_listen);
}

bool simuHostAccept(const std::atomic<bool>& stop)
{
  while (!stop.load()) {
    if (!waitReadable(g_listen, 200)) continue;
    g_client = accept(g_listen, nullptr, nullptr);
    if (g_client == kInvalidSock) continue;
    return true;
  }
  return false;
}

void simuHostCloseClient()
{
  closeSock(g_client);
  g_client = kInvalidSock;
}

bool simuHostSendLine(const std::string& line)
{
  std::string data = line;
  data.push_back('\n');
  size_t off = 0;
  while (off < data.size()) {
#ifdef _WIN32
    int n = send(g_client, data.data() + off, (int)(data.size() - off), 0);
#else
    ssize_t n = send(g_client, data.data() + off, data.size() - off, 0);
#endif
    if (n <= 0) return false;
    off += (size_t)n;
  }
  return true;
}

bool simuHostReadClientLine(std::string& line, bool& closed, const std::atomic<bool>& stop)
{
  line.clear();
  while (!stop.load()) {
    if (!waitReadable(g_client, 100)) continue;
    char c = 0;
#ifdef _WIN32
    int n = recv(g_client, &c, 1, 0);
#else
    ssize_t n = recv(g_client, &c, 1, 0);
#endif
    if (n <= 0) {
      closed = true;
      return false;
    }
    if (c == '\n') return true;
    if (c != '\r') line.push_back(c);
    if (line.size() > 1000000) {
      closed = true;
      return false;
    }
  }
  return false;
}

bool simuHostReadStdioLine(std::string& line, bool& closed, const std::atomic<bool>& stop)
{
  line.clear();
  while (!stop.load()) {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD avail = 0;
    if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) {
      int c = getchar();
      if (c == EOF) {
        closed = true;
        return false;
      }
      if (c == '\n') return true;
      if (c != '\r') line.push_back((char)c);
      continue;
    }
    if (avail == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      continue;
    }
#else
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    int rc = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
    if (rc == 0) continue;
    if (rc < 0) {
      closed = true;
      return false;
    }
#endif
    int c = getchar();
    if (c == EOF) {
      closed = true;
      return false;
    }
    if (c == '\n') return true;
    if (c != '\r') line.push_back((char)c);
  }
  return false;
}
