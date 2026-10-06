/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * Flat JSON parser for the simulator control protocol. Objects and arrays
 * may nest, but the commands themselves are single-level objects.
 */

#pragma once

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

struct Json {
  enum Type { Null, Bool, Number, String, Array, Object } type = Null;
  bool b = false;
  double num = 0;
  std::string str;
  std::vector<Json> arr;
  std::map<std::string, Json> obj;

  bool has(const std::string& key) const { return obj.find(key) != obj.end(); }

  const Json* get(const std::string& key) const
  {
    auto it = obj.find(key);
    return it == obj.end() ? nullptr : &it->second;
  }

  std::string getString(const std::string& key) const
  {
    const Json* v = get(key);
    if (!v) return {};
    if (v->type == String) return v->str;
    if (v->type == Number) {
      std::ostringstream os;
      os << v->num;
      return os.str();
    }
    if (v->type == Bool) return v->b ? "true" : "false";
    return {};
  }

  bool getInt(const std::string& key, int& out) const
  {
    const Json* v = get(key);
    if (!v || v->type != Number) return false;
    out = (int)v->num;
    return true;
  }

  bool isInt() const { return type == Number; }
};

inline std::string jsonEscape(const std::string& s)
{
  std::string out;
  out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out.push_back((char)c);
        }
        break;
    }
  }
  return out;
}

inline bool jsonParse(const std::string& text, Json& out, std::string& error)
{
  size_t i = 0;
  auto skip = [&]() {
    while (i < text.size() && isspace((unsigned char)text[i])) i++;
  };
  std::function<bool(Json&)> parse = [&](Json& node) -> bool {
    skip();
    if (i >= text.size()) {
      error = "unexpected end";
      return false;
    }
    char c = text[i];
    if (c == '"') {
      node.type = Json::String;
      ++i;
      while (i < text.size() && text[i] != '"') {
        if (text[i] == '\\' && i + 1 < text.size()) {
          char e = text[++i];
          if (e == 'n') node.str.push_back('\n');
          else if (e == 'r') node.str.push_back('\r');
          else if (e == 't') node.str.push_back('\t');
          else node.str.push_back(e);
          ++i;
        } else {
          node.str.push_back(text[i++]);
        }
      }
      if (i >= text.size() || text[i] != '"') {
        error = "unterminated string";
        return false;
      }
      ++i;
      return true;
    }
    if (c == '{') {
      node.type = Json::Object;
      ++i;
      skip();
      if (i < text.size() && text[i] == '}') {
        ++i;
        return true;
      }
      while (i < text.size()) {
        Json key;
        if (!parse(key) || key.type != Json::String) {
          error = "object key must be a string";
          return false;
        }
        skip();
        if (i >= text.size() || text[i] != ':') {
          error = "expected ':'";
          return false;
        }
        ++i;
        Json val;
        if (!parse(val)) return false;
        node.obj.emplace(key.str, std::move(val));
        skip();
        if (i < text.size() && text[i] == ',') {
          ++i;
          continue;
        }
        if (i < text.size() && text[i] == '}') {
          ++i;
          return true;
        }
        error = "expected ',' or '}'";
        return false;
      }
      error = "unterminated object";
      return false;
    }
    if (c == '[') {
      node.type = Json::Array;
      ++i;
      skip();
      if (i < text.size() && text[i] == ']') {
        ++i;
        return true;
      }
      while (i < text.size()) {
        Json val;
        if (!parse(val)) return false;
        node.arr.push_back(std::move(val));
        skip();
        if (i < text.size() && text[i] == ',') {
          ++i;
          continue;
        }
        if (i < text.size() && text[i] == ']') {
          ++i;
          return true;
        }
        error = "expected ',' or ']'";
        return false;
      }
      error = "unterminated array";
      return false;
    }
    if (text.compare(i, 4, "true") == 0) {
      node.type = Json::Bool;
      node.b = true;
      i += 4;
      return true;
    }
    if (text.compare(i, 5, "false") == 0) {
      node.type = Json::Bool;
      node.b = false;
      i += 5;
      return true;
    }
    if (text.compare(i, 4, "null") == 0) {
      node.type = Json::Null;
      i += 4;
      return true;
    }
    if (c == '-' || isdigit((unsigned char)c)) {
      char* end = nullptr;
      node.num = strtod(text.c_str() + i, &end);
      if (end == text.c_str() + i) {
        error = "bad number";
        return false;
      }
      node.type = Json::Number;
      i = (size_t)(end - text.c_str());
      return true;
    }
    error = "unexpected token";
    return false;
  };

  if (!parse(out)) return false;
  skip();
  if (i != text.size()) {
    error = "trailing data";
    return false;
  }
  return true;
}
