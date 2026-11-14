// Hearth family — shared UI skin loader
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace hearth {
namespace theme {

struct Color {
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  double a = 1.0;
};

struct Theme {
  std::string id = "classic";
  std::string name = "Classic";
  std::string dir;
  std::string preview;
  int schema = 1;
  bool light = false;

  Color background{0.050, 0.060, 0.072, 1.0};
  Color background_top{0.095, 0.115, 0.135, 1.0};
  Color background_bottom{0.022, 0.028, 0.035, 1.0};
  Color header_top{0.105, 0.135, 0.165, 1.0};
  Color header_bottom{0.060, 0.080, 0.100, 1.0};
  Color panel_top{0.285, 0.355, 0.425, 1.0};
  Color panel_bottom{0.195, 0.255, 0.315, 1.0};
  Color panel_edge{0.430, 0.505, 0.580, 1.0};
  Color panel_inner{0.560, 0.630, 0.690, 1.0};
  Color panel_header_top{0.070, 0.090, 0.110, 1.0};
  Color panel_header_bottom{0.030, 0.040, 0.050, 1.0};
  Color panel_header_text{0.960, 0.915, 0.795, 1.0};
  Color text{0.945, 0.915, 0.835, 1.0};
  Color cream{0.960, 0.915, 0.795, 1.0};
  Color muted{0.740, 0.710, 0.620, 1.0};
  Color accent{0.720, 0.250, 0.215, 1.0};
  Color accent_dark{0.330, 0.100, 0.090, 1.0};
  Color amber{0.850, 0.650, 0.350, 1.0};
  Color knob_top{0.200, 0.220, 0.245, 1.0};
  Color knob_bottom{0.045, 0.052, 0.060, 1.0};
  Color knob_ring{0.560, 0.610, 0.660, 1.0};
  Color silver{0.720, 0.740, 0.750, 1.0};
  Color track{0.035, 0.042, 0.050, 1.0};
  Color meter_face{0.930, 0.890, 0.780, 1.0};
  Color meter_ink{0.090, 0.080, 0.065, 1.0};
  Color green{0.400, 0.680, 0.430, 1.0};
  Color verdigris{0.355, 0.635, 0.520, 1.0};
  Color scope_bg_top{0.030, 0.078, 0.062, 1.0};
  Color scope_bg_bottom{0.008, 0.028, 0.022, 1.0};
  Color scope_grid{0.170, 0.300, 0.255, 1.0};
  Color scope_trace{0.400, 0.830, 0.570, 1.0};
  Color chassis_light{0.300, 0.350, 0.400, 1.0};
  Color chassis_mid{0.160, 0.200, 0.240, 1.0};
  Color chassis_dark{0.060, 0.080, 0.100, 1.0};
  Color highlight{1.0, 1.0, 1.0, 1.0};
  Color shadow{0.0, 0.0, 0.0, 1.0};

  double texture_opacity = 0.06;
  std::string texture = "metal.png";
  std::string knob = "knob.png";
};

struct SkinInfo {
  std::string id;
  std::string name;
  std::string dir;
  std::string preview;
};

inline std::string trim(const std::string& s) {
  size_t begin = 0;
  while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) {
    ++begin;
  }
  size_t end = s.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return s.substr(begin, end - begin);
}

inline std::string unquote(const std::string& s) {
  if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                        (s.front() == '\'' && s.back() == '\''))) {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

inline std::string envOr(const char* name, const std::string& fallback = {}) {
  const char* value = std::getenv(name);
  return (value && *value) ? std::string(value) : fallback;
}

inline std::string homeDir() { return envOr("HOME"); }

inline std::string configHome() {
  return envOr("XDG_CONFIG_HOME", homeDir().empty() ? "" : homeDir() + "/.config");
}

inline std::string dataHome() {
  return envOr("XDG_DATA_HOME",
               homeDir().empty() ? "" : homeDir() + "/.local/share");
}

inline std::string joinPath(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  if (a.back() == '/') return a + b;
  return a + "/" + b;
}

inline bool fileExists(const std::string& path) {
  struct stat st {};
  return !path.empty() && ::stat(path.c_str(), &st) == 0;
}

inline bool dirExists(const std::string& path) {
  struct stat st {};
  return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

inline long long mtimeOf(const std::string& path) {
  struct stat st {};
  if (path.empty() || ::stat(path.c_str(), &st) != 0) return 0;
  return static_cast<long long>(st.st_mtime);
}

inline bool ensureDir(const std::string& path) {
  if (path.empty()) return false;
  std::string current;
  for (char c : path) {
    current += c;
    if (c == '/' && current.size() > 1) {
      ::mkdir(current.c_str(), 0755);
    }
  }
  return ::mkdir(path.c_str(), 0755) == 0 || dirExists(path);
}

inline bool parseColor(const std::string& text, Color& out) {
  std::string s = trim(text);
  if (s.empty()) return false;
  if (s.front() == '#') s.erase(s.begin());
  if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s = s.substr(2);
  }

  if (s.find(',') != std::string::npos) {
    std::vector<double> parts;
    size_t start = 0;
    while (start <= s.size()) {
      const size_t comma = s.find(',', start);
      const std::string part = trim(s.substr(
          start, comma == std::string::npos ? std::string::npos : comma - start));
      if (!part.empty()) {
        parts.push_back(std::strtod(part.c_str(), nullptr));
      }
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
    if (parts.size() < 3) return false;
    bool overOne = false;
    for (double v : parts) {
      if (v > 1.0) overOne = true;
    }
    const double scale = overOne ? 255.0 : 1.0;
    out = Color{parts[0] / scale, parts[1] / scale, parts[2] / scale,
                parts.size() >= 4 ? parts[3] / scale : 1.0};
    return true;
  }

  if (s.size() != 6 && s.size() != 8) return false;
  unsigned long value = 0;
  for (char c : s) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
  }
  value = std::strtoul(s.c_str(), nullptr, 16);
  if (s.size() == 6) {
    out = Color{static_cast<double>((value >> 16) & 0xff) / 255.0,
                static_cast<double>((value >> 8) & 0xff) / 255.0,
                static_cast<double>(value & 0xff) / 255.0, 1.0};
  } else {
    out = Color{static_cast<double>((value >> 24) & 0xff) / 255.0,
                static_cast<double>((value >> 16) & 0xff) / 255.0,
                static_cast<double>((value >> 8) & 0xff) / 255.0,
                static_cast<double>(value & 0xff) / 255.0};
  }
  return true;
}

inline bool parseBool(const std::string& text, bool& out) {
  const std::string s = trim(text);
  if (s == "1" || s == "true" || s == "yes" || s == "on") {
    out = true;
    return true;
  }
  if (s == "0" || s == "false" || s == "no" || s == "off") {
    out = false;
    return true;
  }
  return false;
}

inline bool setColorKey(Theme& t, const std::string& key,
                        const std::string& value) {
  Color c;
  if (!parseColor(value, c)) return false;
  if (key == "background" || key == "bg") t.background = c;
  else if (key == "background_top" || key == "bg_top") t.background_top = c;
  else if (key == "background_bottom" || key == "bg_bottom")
    t.background_bottom = c;
  else if (key == "header_top") t.header_top = c;
  else if (key == "header_bottom") t.header_bottom = c;
  else if (key == "panel_top") t.panel_top = c;
  else if (key == "panel_bottom") t.panel_bottom = c;
  else if (key == "panel_edge") t.panel_edge = c;
  else if (key == "panel_inner") t.panel_inner = c;
  else if (key == "panel_header_top") t.panel_header_top = c;
  else if (key == "panel_header_bottom") t.panel_header_bottom = c;
  else if (key == "panel_header_text") t.panel_header_text = c;
  else if (key == "text") t.text = c;
  else if (key == "cream") t.cream = c;
  else if (key == "muted") t.muted = c;
  else if (key == "accent" || key == "red") t.accent = c;
  else if (key == "accent_dark" || key == "red_dark") t.accent_dark = c;
  else if (key == "amber") t.amber = c;
  else if (key == "knob_top") t.knob_top = c;
  else if (key == "knob_bottom") t.knob_bottom = c;
  else if (key == "knob_ring") t.knob_ring = c;
  else if (key == "silver") t.silver = c;
  else if (key == "track") t.track = c;
  else if (key == "meter_face") t.meter_face = c;
  else if (key == "meter_ink") t.meter_ink = c;
  else if (key == "green") t.green = c;
  else if (key == "verdigris") t.verdigris = c;
  else if (key == "scope_bg_top") t.scope_bg_top = c;
  else if (key == "scope_bg_bottom") t.scope_bg_bottom = c;
  else if (key == "scope_grid") t.scope_grid = c;
  else if (key == "scope_trace") t.scope_trace = c;
  else if (key == "chassis_light") t.chassis_light = c;
  else if (key == "chassis_mid") t.chassis_mid = c;
  else if (key == "chassis_dark") t.chassis_dark = c;
  else if (key == "highlight") t.highlight = c;
  else if (key == "shadow") t.shadow = c;
  else return false;
  return true;
}

inline bool loadIniFile(const std::string& path, Theme& t) {
  std::ifstream input(path);
  if (!input) return false;
  std::string line;
  while (std::getline(input, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    const size_t equals = line.find('=');
    if (equals == std::string::npos) continue;
    const std::string key = trim(line.substr(0, equals));
    const std::string value = trim(line.substr(equals + 1));
    if (key.empty() || value.empty()) continue;

    if (key == "name") t.name = unquote(value);
    else if (key == "schema") t.schema = std::atoi(value.c_str());
    else if (key == "light") parseBool(value, t.light);
    else if (key == "preview") t.preview = unquote(value);
    else if (key == "texture") t.texture = unquote(value);
    else if (key == "knob") t.knob = unquote(value);
    else if (key == "texture_opacity") {
      t.texture_opacity = std::strtod(value.c_str(), nullptr);
    } else {
      setColorKey(t, key, value);
    }
  }
  return true;
}

inline std::string readIniValue(const std::string& path, const std::string& want) {
  std::ifstream input(path);
  if (!input) return {};
  std::string line;
  while (std::getline(input, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    const size_t equals = line.find('=');
    if (equals == std::string::npos) continue;
    if (trim(line.substr(0, equals)) == want) {
      return unquote(trim(line.substr(equals + 1)));
    }
  }
  return {};
}

inline std::vector<std::string> skinRoots(const std::string& bundlePath) {
  std::vector<std::string> roots;
  const std::string env = envOr("HEARTH_SKIN_DIR");
  if (!env.empty()) roots.push_back(env);
  const std::string data = dataHome();
  if (!data.empty()) roots.push_back(joinPath(data, "hearth/skins"));
  if (!bundlePath.empty()) roots.push_back(joinPath(bundlePath, "skins"));
  roots.push_back("/usr/share/hearth/skins");
  return roots;
}

inline std::string findSkinDir(const std::string& bundlePath,
                               const std::string& id) {
  if (id.empty() || id == "classic") return {};
  for (const std::string& root : skinRoots(bundlePath)) {
    const std::string dir = joinPath(root, id);
    if (fileExists(joinPath(dir, "palette.ini"))) return dir;
  }
  return {};
}

inline std::vector<SkinInfo> scanSkins(const std::string& bundlePath) {
  std::vector<SkinInfo> found;
  std::vector<std::string> seen;

  for (const std::string& root : skinRoots(bundlePath)) {
    DIR* dir = ::opendir(root.c_str());
    if (!dir) continue;
    while (dirent* entry = ::readdir(dir)) {
      const std::string id(entry->d_name);
      if (id.empty() || id == "." || id == ".." || id[0] == '.') continue;
      if (std::find(seen.begin(), seen.end(), id) != seen.end()) continue;
      const std::string skinDir = joinPath(root, id);
      const std::string palette = joinPath(skinDir, "palette.ini");
      if (!fileExists(palette)) continue;
      Theme t;
      loadIniFile(palette, t);
      SkinInfo info;
      info.id = id;
      info.name = t.name.empty() ? id : t.name;
      info.dir = skinDir;
      info.preview = t.preview;
      if (info.preview.empty() && fileExists(joinPath(skinDir, "preview.png"))) {
        info.preview = joinPath(skinDir, "preview.png");
      }
      found.push_back(info);
      seen.push_back(id);
    }
    ::closedir(dir);
  }

  std::sort(found.begin(), found.end(),
            [](const SkinInfo& a, const SkinInfo& b) {
              return a.name < b.name;
            });

  std::vector<SkinInfo> result;
  result.push_back(SkinInfo{"classic", "Classic", {}, {}});
  result.insert(result.end(), found.begin(), found.end());
  return result;
}

inline Theme loadTheme(const std::string& bundlePath, const std::string& pluginId,
                       const std::string& id, std::string* outDir = nullptr) {
  Theme t;
  if (!id.empty() && id != "classic") {
    const std::string dir = findSkinDir(bundlePath, id);
    if (!dir.empty()) {
      loadIniFile(joinPath(dir, "palette.ini"), t);
      loadIniFile(joinPath(dir, pluginId + ".ini"), t);
      t.id = id;
      t.dir = dir;
      if (t.name.empty()) t.name = id;
      if (outDir) *outDir = dir;
      return t;
    }
  }
  t.id = "classic";
  t.name = "Classic";
  t.dir.clear();
  if (outDir) outDir->clear();
  return t;
}

class Manager {
 public:
  void init(const std::string& bundlePath, const std::string& pluginId) {
    bundlePath_ = bundlePath;
    pluginId_ = pluginId;
    skins_ = scanSkins(bundlePath_);
    std::string id = configDefault();
    const std::string env = envOr("HEARTH_SKIN");
    if (!env.empty()) id = env;
    if (id.empty()) id = "classic";
    apply(id);
    lastConfigMtime_ = mtimeOf(configPath());
  }

  const Theme& theme() const { return theme_; }
  const std::vector<SkinInfo>& skins() const { return skins_; }
  const std::string& currentId() const { return currentId_; }
  const std::string& bundlePath() const { return bundlePath_; }
  const std::string& pluginId() const { return pluginId_; }
  std::string currentSkinDir() const { return theme_.dir; }

  std::string configPath() const {
    const std::string base = configHome();
    return base.empty() ? std::string() : joinPath(base, "hearth/skins.conf");
  }

  std::string configDefault() const {
    const std::string path = configPath();
    if (path.empty()) return {};
    return readIniValue(path, "default");
  }

  bool apply(const std::string& id) {
    std::string dir;
    Theme next = loadTheme(bundlePath_, pluginId_, id, &dir);
    if (next.id == "classic" && id != "classic") {
      // Requested skin was not found; keep Classic as a safe fallback.
      next.id = "classic";
      next.name = "Classic";
      next.dir.clear();
    }
    theme_ = next;
    currentId_ = theme_.id;
    return currentId_ == id;
  }

  bool set(const std::string& id, bool persist = true) {
    if (id.empty() || id == currentId_) return false;
    if (id != "classic" && findSkinDir(bundlePath_, id).empty()) return false;
    if (!apply(id)) return false;
    if (persist) writeConfigDefault(currentId_);
    lastConfigMtime_ = mtimeOf(configPath());
    return true;
  }

  bool cycle(int direction) {
    if (skins_.empty()) return false;
    if (direction == 0) direction = 1;
    size_t index = 0;
    for (size_t i = 0; i < skins_.size(); ++i) {
      if (skins_[i].id == currentId_) {
        index = i;
        break;
      }
    }
    const size_t count = skins_.size();
    const size_t next = (index + (direction > 0 ? 1 : count - 1)) % count;
    return set(skins_[next].id);
  }

  bool reloadIfConfigChanged() {
    const long long mtime = mtimeOf(configPath());
    if (mtime == lastConfigMtime_) return false;
    lastConfigMtime_ = mtime;
    const std::string id = configDefault();
    if (id.empty() || id == currentId_) return false;
    skins_ = scanSkins(bundlePath_);
    return set(id, false);
  }

 private:
  void writeConfigDefault(const std::string& id) {
    const std::string path = configPath();
    if (path.empty()) return;
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) ensureDir(path.substr(0, slash));

    std::vector<std::string> lines;
    bool wroteDefault = false;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
      const std::string trimmed = trim(line);
      if (trimmed.rfind("default=", 0) == 0 || trimmed.rfind("skin=", 0) == 0) {
        lines.push_back("default=" + id);
        wroteDefault = true;
      } else {
        lines.push_back(line);
      }
    }
    if (!wroteDefault) {
      lines.push_back("default=" + id);
    }

    const std::string temp = path + ".tmp";
    {
      std::ofstream output(temp, std::ios::trunc);
      if (!output) return;
      output << "# Hearth suite skin\n";
      for (const std::string& entry : lines) {
        if (trim(entry).empty()) continue;
        output << entry << "\n";
      }
    }
    std::rename(temp.c_str(), path.c_str());
  }

  std::string bundlePath_;
  std::string pluginId_;
  std::vector<SkinInfo> skins_;
  Theme theme_;
  std::string currentId_ = "classic";
  long long lastConfigMtime_ = 0;
};

inline Manager& global() {
  static Manager manager;
  return manager;
}

} // namespace theme
} // namespace hearth
