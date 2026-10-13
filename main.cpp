#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/fs.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "icon_data.h"

namespace fs = std::filesystem;

static constexpr int32_t DEFAULT_WINDOW_WIDTH = 300;
static constexpr int32_t DEFAULT_WINDOW_HEIGHT = 350;
static constexpr const char* WINDOW_TITLE = "SVT";
static constexpr const char* SVT_OPTIONS_FILE = "svt_config.txt";

// ============================================================================
// AppOptions (matches ui::AppOptions & svt_config.txt JSON schema 1:1)
// ============================================================================
struct AppOptions {
  std::string map = "";
  std::string inh_times = "";
  bool lin_sv = true;
  bool exp_sv = false;
  bool pol_sv = false;
  bool sin_sv = false;
  bool flat_sv = false;
  bool vol = false;
  bool hits = true;
  bool snappings = false;
  bool inh_lines = false;
  std::string offset = "-1";
  std::string buffer = "3";
  std::string min_spacing = "3";
  std::string pol_exp = "0.5";
  bool flat_scaling = false;
  std::string flat_change = "0.0";
  std::string flat_scaling_change = "1.0";
  std::string snapping_numer = "1";
  std::string snapping_denom = "1";
  bool ignore_bpm = false;
  int32_t pos_x = 400;
  int32_t pos_y = 300;
  uint32_t width = DEFAULT_WINDOW_WIDTH;
  uint32_t height = DEFAULT_WINDOW_HEIGHT;
  bool tooltips = true;
  std::string experimental = "";
};

// Minimal JSON escape / unescape & parser for AppOptions (matches serde_json::to_string_pretty)
static std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '\"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          out += std::format("\\u{:04x}", static_cast<unsigned int>(c));
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  return out;
}

static std::string serialize_options_json(const AppOptions& o) {
  std::ostringstream ss;
  ss << "{\n"
     << "  \"map\": \"" << json_escape(o.map) << "\",\n"
     << "  \"inh_times\": \"" << json_escape(o.inh_times) << "\",\n"
     << "  \"lin_sv\": " << (o.lin_sv ? "true" : "false") << ",\n"
     << "  \"exp_sv\": " << (o.exp_sv ? "true" : "false") << ",\n"
     << "  \"pol_sv\": " << (o.pol_sv ? "true" : "false") << ",\n"
     << "  \"sin_sv\": " << (o.sin_sv ? "true" : "false") << ",\n"
     << "  \"flat_sv\": " << (o.flat_sv ? "true" : "false") << ",\n"
     << "  \"vol\": " << (o.vol ? "true" : "false") << ",\n"
     << "  \"hits\": " << (o.hits ? "true" : "false") << ",\n"
     << "  \"snappings\": " << (o.snappings ? "true" : "false") << ",\n"
     << "  \"inh_lines\": " << (o.inh_lines ? "true" : "false") << ",\n"
     << "  \"offset\": \"" << json_escape(o.offset) << "\",\n"
     << "  \"buffer\": \"" << json_escape(o.buffer) << "\",\n"
     << "  \"min_spacing\": \"" << json_escape(o.min_spacing) << "\",\n"
     << "  \"pol_exp\": \"" << json_escape(o.pol_exp) << "\",\n"
     << "  \"flat_scaling\": " << (o.flat_scaling ? "true" : "false") << ",\n"
     << "  \"flat_change\": \"" << json_escape(o.flat_change) << "\",\n"
     << "  \"flat_scaling_change\": \"" << json_escape(o.flat_scaling_change) << "\",\n"
     << "  \"snapping_numer\": \"" << json_escape(o.snapping_numer) << "\",\n"
     << "  \"snapping_denom\": \"" << json_escape(o.snapping_denom) << "\",\n"
     << "  \"ignore_bpm\": " << (o.ignore_bpm ? "true" : "false") << ",\n"
     << "  \"pos_x\": " << o.pos_x << ",\n"
     << "  \"pos_y\": " << o.pos_y << ",\n"
     << "  \"width\": " << o.width << ",\n"
     << "  \"height\": " << o.height << ",\n"
     << "  \"tooltips\": " << (o.tooltips ? "true" : "false") << ",\n"
     << "  \"experimental\": \"" << json_escape(o.experimental) << "\"\n"
     << "}";
  return ss.str();
}

static bool extract_json_raw_value(const std::string& json, const std::string& key, size_t& val_pos) {
  std::string needle = "\"" + key + "\"";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  pos += needle.size();
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) pos++;
  if (pos >= json.size() || json[pos] != ':') return false;
  pos++;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) pos++;
  if (pos >= json.size()) return false;
  val_pos = pos;
  return true;
}

static bool extract_json_string(const std::string& json, const std::string& key, std::string& out) {
  size_t pos = 0;
  if (!extract_json_raw_value(json, key, pos)) return false;
  if (json[pos] != '\"') return false;
  pos++;
  std::string res;
  while (pos < json.size()) {
    char c = json[pos++];
    if (c == '\"') {
      out = res;
      return true;
    }
    if (c == '\\' && pos < json.size()) {
      char esc = json[pos++];
      switch (esc) {
        case '\"': res.push_back('\"'); break;
        case '\\': res.push_back('\\'); break;
        case '/':  res.push_back('/'); break;
        case 'b':  res.push_back('\b'); break;
        case 'f':  res.push_back('\f'); break;
        case 'n':  res.push_back('\n'); break;
        case 'r':  res.push_back('\r'); break;
        case 't':  res.push_back('\t'); break;
        default:   res.push_back(esc); break;
      }
    } else {
      res.push_back(c);
    }
  }
  return false;
}

static bool extract_json_bool(const std::string& json, const std::string& key, bool& out) {
  size_t pos = 0;
  if (!extract_json_raw_value(json, key, pos)) return false;
  if (json.compare(pos, 4, "true") == 0) {
    out = true;
    return true;
  }
  if (json.compare(pos, 5, "false") == 0) {
    out = false;
    return true;
  }
  return false;
}

static bool extract_json_int(const std::string& json, const std::string& key, int64_t& out) {
  size_t pos = 0;
  if (!extract_json_raw_value(json, key, pos)) return false;
  size_t end = pos;
  if (end < json.size() && (json[end] == '-' || json[end] == '+')) end++;
  while (end < json.size() && std::isdigit(static_cast<unsigned char>(json[end]))) end++;
  if (end == pos) return false;
  int64_t v = 0;
  auto [ptr, ec] = std::from_chars(json.data() + pos, json.data() + end, v);
  if (ec != std::errc{}) return false;
  out = v;
  return true;
}

static bool parse_options_json(const std::string& json, AppOptions& o) {
  // Require all fields to be present just like serde_json::from_str::<AppOptions>
  if (!extract_json_string(json, "map", o.map)) return false;
  if (!extract_json_string(json, "inh_times", o.inh_times)) return false;
  if (!extract_json_bool(json, "lin_sv", o.lin_sv)) return false;
  if (!extract_json_bool(json, "exp_sv", o.exp_sv)) return false;
  if (!extract_json_bool(json, "pol_sv", o.pol_sv)) return false;
  if (!extract_json_bool(json, "sin_sv", o.sin_sv)) return false;
  if (!extract_json_bool(json, "flat_sv", o.flat_sv)) return false;
  if (!extract_json_bool(json, "vol", o.vol)) return false;
  if (!extract_json_bool(json, "hits", o.hits)) return false;
  if (!extract_json_bool(json, "snappings", o.snappings)) return false;
  if (!extract_json_bool(json, "inh_lines", o.inh_lines)) return false;
  if (!extract_json_string(json, "offset", o.offset)) return false;
  if (!extract_json_string(json, "buffer", o.buffer)) return false;
  if (!extract_json_string(json, "min_spacing", o.min_spacing)) return false;
  if (!extract_json_string(json, "pol_exp", o.pol_exp)) return false;
  if (!extract_json_bool(json, "flat_scaling", o.flat_scaling)) return false;
  if (!extract_json_string(json, "flat_change", o.flat_change)) return false;
  if (!extract_json_string(json, "flat_scaling_change", o.flat_scaling_change)) return false;
  if (!extract_json_string(json, "snapping_numer", o.snapping_numer)) return false;
  if (!extract_json_string(json, "snapping_denom", o.snapping_denom)) return false;
  if (!extract_json_bool(json, "ignore_bpm", o.ignore_bpm)) return false;
  int64_t px = 0, py = 0, w = 0, h = 0;
  if (!extract_json_int(json, "pos_x", px)) return false;
  if (!extract_json_int(json, "pos_y", py)) return false;
  if (!extract_json_int(json, "width", w)) return false;
  if (!extract_json_int(json, "height", h)) return false;
  o.pos_x = static_cast<int32_t>(px);
  o.pos_y = static_cast<int32_t>(py);
  o.width = static_cast<uint32_t>(w);
  o.height = static_cast<uint32_t>(h);
  if (!extract_json_bool(json, "tooltips", o.tooltips)) return false;
  if (!extract_json_string(json, "experimental", o.experimental)) return false;
  return true;
}

// ============================================================================
// Parsing helpers matching Rust str::parse::<f32>() / str::parse::<i32>()
// ============================================================================
static std::optional<float> parse_f32(const std::string& s) {
  if (s.empty()) return std::nullopt;
  float val = 0.0f;
  const char* begin = s.data();
  const char* end = s.data() + s.size();
  auto [ptr, ec] = std::from_chars(begin, end, val);
  if (ec != std::errc{} || ptr != end || !std::isfinite(val)) {
    return std::nullopt;
  }
  return val;
}

static std::optional<int32_t> parse_i32(const std::string& s) {
  if (s.empty()) return std::nullopt;
  int32_t val = 0;
  const char* begin = s.data();
  const char* end = s.data() + s.size();
  if (*begin == '+') begin++;
  auto [ptr, ec] = std::from_chars(begin, end, val);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return val;
}

static std::vector<std::string> split_char(const std::string& s, char delim) {
  std::vector<std::string> tokens;
  size_t start = 0;
  while (true) {
    size_t pos = s.find(delim, start);
    if (pos == std::string::npos) {
      tokens.push_back(s.substr(start));
      break;
    }
    tokens.push_back(s.substr(start, pos - start));
    start = pos + 1;
  }
  return tokens;
}

static bool read_lines(const std::string& full_path, std::vector<std::string>& out_lines) {
  std::ifstream file(full_path, std::ios::binary);
  if (!file.is_open()) return false;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    out_lines.push_back(std::move(line));
  }
  return true;
}

// ============================================================================
// SVT Core Logic (1:1 port of src/svt.rs v1.0.3)
// ============================================================================
struct MapObject {
  int32_t obj_class = 0;
  int32_t time = 0;
  float beatlength = 0.0f;
  int32_t meter = 0;
  int32_t sampleset = 0;
  int32_t sampleindex = 0;
  int32_t volume = 0;
  int32_t uninherited = 0;
  int32_t effects = 0;
  std::string data;
};

static std::optional<MapObject> create_map_object(const std::string& p, bool timingpoint, std::string* err_out = nullptr) {
  auto p_tokens = split_char(p, ',');

  if (timingpoint) {
    if (p_tokens.size() != 8) {
      if (err_out) *err_out = std::format("[create] timing: incorrect format {}", p);
      return std::nullopt;
    }
    auto time_raw = parse_f32(p_tokens[0]);
    auto beatlength = parse_f32(p_tokens[1]);
    auto meter = parse_i32(p_tokens[2]);
    auto sampleset = parse_i32(p_tokens[3]);
    auto sampleindex = parse_i32(p_tokens[4]);
    auto volume = parse_i32(p_tokens[5]);
    auto uninherited = parse_i32(p_tokens[6]);
    auto effects = parse_i32(p_tokens[7]);

    if (!time_raw || !beatlength || !meter || !sampleset || !sampleindex || !volume || !uninherited || !effects) {
      if (err_out) *err_out = "[apply] timing point format error";
      return std::nullopt;
    }

    int32_t time = static_cast<int32_t>(std::roundf(*time_raw));
    MapObject obj;
    obj.obj_class = (*uninherited) ^ 1;
    obj.time = time;
    obj.beatlength = *beatlength;
    obj.meter = *meter;
    obj.sampleset = *sampleset;
    obj.sampleindex = *sampleindex;
    obj.volume = *volume;
    obj.uninherited = *uninherited;
    obj.effects = *effects;
    obj.data = p;
    return obj;
  } else {
    if (p_tokens.size() < 3) {
      if (err_out) *err_out = std::format("[create] hit: incorrect format {}", p);
      return std::nullopt;
    }
    auto time_raw = parse_f32(p_tokens[2]);
    if (!time_raw) {
      if (err_out) *err_out = std::format("[create] hit: incorrect format {}", p);
      return std::nullopt;
    }
    int32_t time = static_cast<int32_t>(std::roundf(*time_raw));
    MapObject obj;
    obj.obj_class = 3;
    obj.time = time;
    obj.data = "";
    return obj;
  }
}

struct SVT {
  std::vector<MapObject> all_objs;
  std::vector<MapObject> new_objs;

  std::optional<std::string> apply_two_point_fn(const std::string& start_line, const std::string& end_line, const AppOptions& opt) {
    float pol_exp = 1.0f;
    if (opt.pol_sv) {
      auto v = parse_f32(opt.pol_exp);
      if (!v) return "[apply] invalid exponent";
      pol_exp = *v;
    }

    float flat_sv = 0.0f;
    if (opt.flat_sv && !opt.flat_scaling) {
      auto v = parse_f32(opt.flat_change);
      if (!v) return "[apply] invalid flat sv";
      flat_sv = *v;
    }

    float flat_sv_scaling = 1.0f;
    if (opt.flat_sv && opt.flat_scaling) {
      auto v = parse_f32(opt.flat_scaling_change);
      if (!v) return "[apply] invalid flat sv scaling";
      flat_sv_scaling = *v;
    }

    auto t_off_opt = parse_i32(opt.offset);
    if (!t_off_opt) return "[apply] invalid offset";
    int32_t t_off = *t_off_opt;

    auto t_buf_opt = parse_i32(opt.buffer);
    if (!t_buf_opt) return "[apply] invalid buffer";
    int32_t t_buf = *t_buf_opt;

    bool sv_change_bool = opt.lin_sv || opt.pol_sv || opt.sin_sv || opt.exp_sv || opt.flat_sv;
    if (!sv_change_bool && !opt.vol) {
      return "[apply] nothing to apply (sv, vol)";
    }

    auto start_obj_opt = create_map_object(start_line, true);
    if (!start_obj_opt) return "[apply] timing point format error";
    auto end_obj_opt = create_map_object(end_line, true);
    if (!end_obj_opt) return "[apply] timing point format error";

    const MapObject& start_obj = *start_obj_opt;
    const MapObject& end_obj = *end_obj_opt;

    float s_bpm = 0.0f;
    float e_bpm = 0.0f;
    for (const auto& obj : all_objs) {
      if (obj.obj_class == 0) {
        if (obj.time <= start_obj.time) {
          s_bpm = 60000.0f / obj.beatlength;
        }
        if (obj.time <= end_obj.time) {
          e_bpm = 60000.0f / obj.beatlength;
        }
      }
    }

    if (s_bpm == 0.0f) {
      return "[apply] no uninherited lines detected";
    }

    float s_sv_raw = -100.0f * s_bpm / start_obj.beatlength;
    float e_sv_raw = opt.ignore_bpm
      ? (-100.0f * s_bpm / end_obj.beatlength)
      : (-100.0f * e_bpm / end_obj.beatlength);

    std::cout << std::format("[apply] t:{}->{} raw sv:{}->{} vol:{}->{}\n",
                             start_obj.time, end_obj.time, s_sv_raw, e_sv_raw, start_obj.volume, end_obj.volume);

    if (start_obj.time > end_obj.time) {
      return "[apply] invalid times (end <= start)";
    }
    if (s_sv_raw <= 0.0f || e_sv_raw <= 0.0f) {
      return "[apply] invalid sv value(s) (sv <= 0)";
    }
    if (start_obj.volume < 0 || start_obj.volume > 100 || end_obj.volume < 0 || end_obj.volume > 100) {
      return "[apply] invalid volumes (vol < 0 or vol > 100)";
    }

    int32_t t_diff = end_obj.time - start_obj.time;
    float sv_diff = e_sv_raw - s_sv_raw;
    float sv_ratio = e_sv_raw / s_sv_raw;
    int32_t vol_diff = end_obj.volume - start_obj.volume;
    float sv_per_ms = sv_diff / static_cast<float>(t_diff);
    float vol_per_ms = static_cast<float>(vol_diff) / static_cast<float>(t_diff);

    int32_t last_uni_time = 0;
    float bpm = 160.0f;
    float beatlength = -100.0f;
    int32_t meter = 4;
    int32_t sample_set = 0;
    int32_t sample_index = 0;
    int32_t volume = 100;
    int32_t effects = 0;
    int32_t kiai_change_time = 0;

    for (const auto& obj : all_objs) {
      if (opt.flat_sv && obj.obj_class > 1) {
        continue;
      }

      if (obj.obj_class == 0) {
        if ((obj.effects & 1) != (effects & 1)) {
          kiai_change_time = obj.time;
        }
        last_uni_time = obj.time;
        bpm = 60000.0f / obj.beatlength;
        meter = obj.meter;
        beatlength = obj.beatlength;
        sample_set = obj.sampleset;
        sample_index = obj.sampleindex;
        volume = obj.volume;
        effects = obj.effects;
        continue;
      } else if (obj.obj_class == 1) {
        if ((obj.effects & 1) != (effects & 1)) {
          kiai_change_time = obj.time;
        }
        beatlength = obj.beatlength;
        sample_set = obj.sampleset;
        sample_index = obj.sampleindex;
        volume = obj.volume;
        effects = obj.effects;
      }

      int32_t obj_time = obj.time;
      if (obj_time >= start_obj.time - t_buf && obj_time <= end_obj.time + t_buf) {
        int32_t new_t = std::max(std::max(obj_time + t_off, last_uni_time), kiai_change_time);
        float new_sv = -100.0f;
        if (opt.lin_sv) {
          new_sv = s_sv_raw + static_cast<float>(obj_time - start_obj.time) * sv_per_ms;
        } else if (opt.exp_sv) {
          new_sv = s_sv_raw * std::exp(static_cast<float>(obj_time - start_obj.time) * std::log(sv_ratio) / static_cast<float>(t_diff));
        } else if (opt.pol_sv) {
          new_sv = s_sv_raw + sv_diff * std::pow(static_cast<float>(std::max(0, obj_time - start_obj.time)) / static_cast<float>(t_diff), pol_exp);
        } else if (opt.sin_sv) {
          constexpr float PI_F = 3.14159265358979323846f;
          new_sv = s_sv_raw + sv_diff * (1.0f - std::cos(PI_F * static_cast<float>(obj_time - start_obj.time) / static_cast<float>(t_diff))) / 2.0f;
        } else if (opt.flat_sv) {
          if (opt.flat_scaling) {
            new_sv = (-100.0f / obj.beatlength) * bpm * flat_sv_scaling;
          } else {
            new_sv = (-100.0f / obj.beatlength + flat_sv) * bpm;
          }
        }

        float new_b = -100.0f / (new_sv / bpm);
        uint32_t new_vol = static_cast<uint32_t>(std::roundf(static_cast<float>(start_obj.volume) + static_cast<float>(obj_time - start_obj.time) * vol_per_ms));

        std::string new_point;
        if (sv_change_bool && opt.vol) {
          new_point = std::format("{},{},{},{},{},{},{},{}", new_t, new_b, meter, sample_set, sample_index, new_vol, 0, effects);
        } else if (sv_change_bool && !opt.vol) {
          new_point = std::format("{},{},{},{},{},{},{},{}", new_t, new_b, meter, sample_set, sample_index, volume, 0, effects);
        } else if (!sv_change_bool && opt.vol) {
          new_point = std::format("{},{},{},{},{},{},{},{}", new_t, beatlength, meter, sample_set, sample_index, new_vol, 0, effects);
        }

        if (obj.obj_class == 0) {
          std::cout << "[apply] shouldn't get here, class 1\n";
        } else if (obj.obj_class == 1) {
          if (opt.inh_lines || opt.flat_sv) {
            std::cout << "[new] inh " << new_point << "\n";
            MapObject m;
            m.time = new_t;
            m.obj_class = 4;
            m.data = new_point;
            new_objs.push_back(std::move(m));
          }
        } else if (obj.obj_class == 2) {
          if (opt.snappings) {
            std::cout << "[new] snp " << new_point << "\n";
            MapObject m;
            m.time = new_t;
            m.obj_class = 4;
            m.data = new_point;
            new_objs.push_back(std::move(m));
          }
        } else if (obj.obj_class == 3) {
          if (opt.hits) {
            std::cout << "[new] hit " << new_point << "\n";
            MapObject m;
            m.time = new_t;
            m.obj_class = 4;
            m.data = new_point;
            new_objs.push_back(std::move(m));
          }
        } else {
          std::cout << "[apply] unknown class " << obj.obj_class << "\n";
        }
      }
    }

    return std::nullopt;
  }

  std::optional<std::string> load_osu(const std::string& filename, const AppOptions& opt) {
    bool bool_timing = false;
    bool bool_hit = false;

    auto snapping_numer_opt = parse_f32(opt.snapping_numer);
    if (!snapping_numer_opt) return "[load] invalid snapping numer";
    auto snapping_denom_opt = parse_f32(opt.snapping_denom);
    if (!snapping_denom_opt) return "[load] invalid snapping denom";

    float snapping_numer = *snapping_numer_opt;
    float snapping_denom = *snapping_denom_opt;

    if (snapping_denom <= 0.0f) {
      return "[load] snapping denom should be > 0";
    }
    if (snapping_numer <= 0.0f) {
      return "[load] snapping numer should be > 0";
    }

    float snap_time = 1000000.0f;
    float snap_inc = 1000000.0f;
    float beat_inc = 0.0f;

    all_objs.clear();
    new_objs.clear();

    std::vector<std::string> lines;
    if (read_lines(filename, lines)) {
      for (const auto& s : lines) {
        if (s == "[General]" || s == "[Editor]" || s == "[Metadata]" ||
            s == "[Difficulty]" || s == "[Events]" || s == "[Colours]") {
          bool_timing = false;
          bool_hit = false;
        } else if (s == "[TimingPoints]") {
          bool_timing = true;
          bool_hit = false;
          std::cout << "[load] found [TimingPoints], reading\n";
        } else if (s == "[HitObjects]") {
          bool_timing = false;
          bool_hit = true;
          std::cout << "[load] found [HitObjects], reading\n";
        } else {
          if (bool_timing) {
            if (auto map_obj_opt = create_map_object(s, true)) {
              MapObject map_obj = std::move(*map_obj_opt);
              while (snap_time + snap_inc < static_cast<float>(map_obj.time)) {
                snap_time += snap_inc;
                MapObject snp;
                snp.time = static_cast<int32_t>(std::roundf(snap_time));
                snp.obj_class = 2;
                all_objs.push_back(std::move(snp));
              }
              if (map_obj.uninherited == 1) {
                snap_time = static_cast<float>(map_obj.time);
                beat_inc = map_obj.beatlength;
                snap_inc = beat_inc * snapping_numer / snapping_denom;
                MapObject snp;
                snp.time = static_cast<int32_t>(snap_time);
                snp.obj_class = 2;
                all_objs.push_back(std::move(snp));
              }
              all_objs.push_back(std::move(map_obj));
            }
          } else if (bool_hit) {
            if (auto map_obj_opt = create_map_object(s, false)) {
              all_objs.push_back(std::move(*map_obj_opt));
            }
          }
        }
      }
    }

    float end_snap_time = snap_time * 1.25f;
    while (snap_time + snap_inc < end_snap_time) {
      snap_time += snap_inc;
      MapObject snp;
      snp.time = static_cast<int32_t>(std::roundf(snap_time));
      snp.obj_class = 2;
      all_objs.push_back(std::move(snp));
    }

    std::stable_sort(all_objs.begin(), all_objs.end(), [](const MapObject& a, const MapObject& b) {
      if (a.time != b.time) return a.time < b.time;
      return a.obj_class < b.obj_class;
    });

    return std::nullopt;
  }

  std::optional<std::string> write_output_points(
      const std::string& min_spacing_text,
      const std::string& in_filename,
      const std::string& out_filename,
      bool preview,
      size_t& applied_count_out) {
    if (new_objs.empty()) {
      return "[write] no new objects to apply";
    }

    auto min_spacing_opt = parse_i32(min_spacing_text);
    if (!min_spacing_opt) return "[apply] invalid spacing";
    int32_t min_spacing = *min_spacing_opt;

    if (min_spacing < 0 || min_spacing > 1000) {
      return "[write] min spacing cannot be negative or excessively high";
    }

    std::stable_sort(new_objs.begin(), new_objs.end(), [](const MapObject& a, const MapObject& b) {
      if (a.time != b.time) return a.time < b.time;
      return a.obj_class < b.obj_class;
    });
    new_objs.erase(
      std::unique(new_objs.begin(), new_objs.end(), [](const MapObject& a, const MapObject& b) {
        return a.time == b.time && a.obj_class == b.obj_class;
      }),
      new_objs.end()
    );

    std::vector<MapObject> svt_objs;
    std::vector<MapObject> out_objs;

    int32_t last_obj_time = -1000;
    for (const auto& obj : new_objs) {
      if (obj.time - last_obj_time > min_spacing) {
        svt_objs.push_back(obj);
        last_obj_time = obj.time;
      }
    }

    if (svt_objs.empty()) {
      return "[write] no new objects to apply";
    }

    out_objs.insert(out_objs.end(), svt_objs.begin(), svt_objs.end());

    size_t svt_idx = 0;
    last_obj_time = svt_objs[0].time;

    for (const auto& obj : all_objs) {
      if (obj.obj_class == 0) {
        out_objs.push_back(obj);
      } else if (obj.obj_class == 1) {
        while (obj.time - last_obj_time > min_spacing) {
          svt_idx++;
          if (svt_idx >= svt_objs.size()) {
            break;
          }
          last_obj_time = svt_objs[svt_idx].time;
        }
        if (std::abs(obj.time - last_obj_time) > min_spacing) {
          out_objs.push_back(obj);
        }
      }
    }

    std::stable_sort(out_objs.begin(), out_objs.end(), [](const MapObject& a, const MapObject& b) {
      if (a.time != b.time) return a.time < b.time;
      return (a.uninherited ^ 1) < (b.uninherited ^ 1);
    });
    out_objs.erase(
      std::unique(out_objs.begin(), out_objs.end(), [](const MapObject& a, const MapObject& b) {
        return a.time == b.time && a.uninherited == b.uninherited;
      }),
      out_objs.end()
    );

    std::error_code ec;
    fs::copy_file(in_filename, "backup.osu", fs::copy_options::overwrite_existing, ec);
    if (ec) {
      return std::format("[backup] error backing up file {}", ec.message());
    }

    std::vector<std::string> lines;
    if (!read_lines(in_filename, lines)) {
      return "[write] input file/filename invalid";
    }

    std::string out_string;
    bool bool_timing = false;
    for (const auto& s : lines) {
      if (s == "[General]" || s == "[Editor]" || s == "[Metadata]" ||
          s == "[Difficulty]" || s == "[Events]" || s == "[Colours]" || s == "[HitObjects]") {
        bool_timing = false;
        out_string += s;
        out_string += "\n";
      } else if (s == "[TimingPoints]") {
        bool_timing = true;
        out_string += s;
        out_string += "\n";
        for (const auto& out_obj : out_objs) {
          out_string += out_obj.data;
          out_string += "\n";
        }
        out_string += "\n";
      } else {
        auto s_tokens = split_char(s, ':');
        if (preview && s_tokens.size() == 2 && s_tokens[0] == "Version") {
          out_string += "Version:preview\n";
        } else if (!bool_timing) {
          out_string += s;
          out_string += "\n";
        }
      }
    }

    std::ofstream out_file(out_filename, std::ios::binary | std::ios::trunc);
    if (!out_file.is_open()) {
      return "[write] failed to open output file";
    }
    out_file.write(out_string.data(), static_cast<std::streamsize>(out_string.size()));
    applied_count_out = svt_objs.size();
    return std::nullopt;
  }

  void print_debug() const {
    std::cout << "\n[svt] DEBUG all_objs:\n";
    int uni_count = 0, inh_count = 0, snp_count = 0, hit_count = 0;
    for (const auto& map_obj : all_objs) {
      switch (map_obj.obj_class) {
        case 0:
          std::cout << "[svt] uni " << map_obj.data << "\n";
          uni_count++;
          break;
        case 1:
          std::cout << "[svt] inh " << map_obj.data << "\n";
          inh_count++;
          break;
        case 2:
          std::cout << "[svt] snp " << map_obj.time << "\n";
          snp_count++;
          break;
        case 3:
          hit_count++;
          break;
        default:
          std::cout << "[svt] ???\n";
          break;
      }
    }
    std::cout << std::format("[svt] counts:\nuni: {}\ninh: {}\nsnp: {}\nhit: {}\n\n",
                             uni_count, inh_count, snp_count, hit_count);
  }
};

// Exchange two files on Linux (matches libxch::xch)
static bool exchange_files(const std::string& path_a, const std::string& path_b, std::string& err_msg) {
  int ret = syscall(SYS_renameat2, AT_FDCWD, path_a.c_str(), AT_FDCWD, path_b.c_str(), RENAME_EXCHANGE);
  if (ret == 0) {
    return true;
  }
  // Fallback if across filesystems
  std::error_code ec;
  fs::copy_file(path_a, "temp.osu", fs::copy_options::overwrite_existing, ec);
  if (ec) { err_msg = ec.message(); return false; }
  fs::copy_file(path_b, path_a, fs::copy_options::overwrite_existing, ec);
  if (ec) { err_msg = ec.message(); return false; }
  fs::copy_file("temp.osu", path_b, fs::copy_options::overwrite_existing, ec);
  if (ec) { err_msg = ec.message(); return false; }
  fs::remove("temp.osu", ec);
  return true;
}

// ============================================================================
// UI State & GTK3 Implementation (1:1 port of src/ui.rs + src/main.rs)
// ============================================================================
struct UI {
  GtkWidget* window = nullptr;
  GtkWidget* inherited_text = nullptr; // GtkTextView
  GtkTextBuffer* inherited_buf = nullptr;

  GtkWidget* lin_sv_check = nullptr;
  GtkWidget* exp_sv_check = nullptr;
  GtkWidget* pol_sv_check = nullptr;
  GtkWidget* sin_sv_check = nullptr;
  GtkWidget* flat_sv_check = nullptr;
  GtkWidget* vol_check = nullptr;

  GtkWidget* hit_check = nullptr;
  GtkWidget* snapping_check = nullptr;
  GtkWidget* inh_check = nullptr;

  GtkWidget* offset_text = nullptr;
  GtkWidget* offset_label = nullptr;
  GtkWidget* buffer_text = nullptr;
  GtkWidget* buffer_label = nullptr;
  GtkWidget* min_spacing_text = nullptr;
  GtkWidget* min_spacing_label = nullptr;

  GtkWidget* pol_exp_text = nullptr;
  GtkWidget* pol_exp_label = nullptr;
  GtkWidget* flat_sv_text = nullptr;
  GtkWidget* flat_sv_label = nullptr;
  GtkWidget* flat_sv_scale_text = nullptr;
  GtkWidget* flat_sv_scale_label = nullptr;
  GtkWidget* flat_sv_scale_check = nullptr;

  GtkWidget* snapping_numer_text = nullptr;
  GtkWidget* snapping_prefix_label = nullptr;
  GtkWidget* snapping_denom_text = nullptr;
  GtkWidget* snapping_label = nullptr;

  GtkWidget* ign_bpm_check = nullptr;

  GtkWidget* open_button = nullptr;
  GtkWidget* in_filename = nullptr;
  GtkWidget* preview_check = nullptr;
  GtkWidget* out_filename = nullptr;

  GtkWidget* apply_button = nullptr;
  GtkWidget* undo_button = nullptr;
  GtkWidget* status_label = nullptr;

  AppOptions options;
  SVT svt;
  bool suppress_signals = false;

  std::string get_inherited_text() const {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(inherited_buf, &start, &end);
    char* text = gtk_text_buffer_get_text(inherited_buf, &start, &end, FALSE);
    std::string res = text ? text : "";
    g_free(text);
    return res;
  }

  void set_inherited_text(const std::string& s) {
    gtk_text_buffer_set_text(inherited_buf, s.c_str(), -1);
  }

  void set_status(const std::string& s) {
    gtk_label_set_text(GTK_LABEL(status_label), s.c_str());
  }

  bool is_checked(GtkWidget* check) const {
    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)) != FALSE;
  }

  void set_checked(GtkWidget* check, bool state) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), state ? TRUE : FALSE);
  }

  std::string get_entry(GtkWidget* entry) const {
    const char* t = gtk_entry_get_text(GTK_ENTRY(entry));
    return t ? t : "";
  }

  void set_entry(GtkWidget* entry, const std::string& s) {
    gtk_entry_set_text(GTK_ENTRY(entry), s.c_str());
  }

  void set_flat_scaling() {
    if (!is_checked(flat_sv_check)) {
      gtk_widget_set_visible(flat_sv_scale_text, false);
      gtk_widget_set_visible(flat_sv_scale_label, false);
      gtk_widget_set_visible(flat_sv_text, false);
      gtk_widget_set_visible(flat_sv_label, false);
      return;
    }
    bool sv_scale_visible = is_checked(flat_sv_scale_check);
    gtk_widget_set_visible(flat_sv_scale_text, sv_scale_visible);
    gtk_widget_set_visible(flat_sv_scale_label, sv_scale_visible);
    gtk_widget_set_visible(flat_sv_text, !sv_scale_visible);
    gtk_widget_set_visible(flat_sv_label, !sv_scale_visible);
  }

  void set_snapping() {
    bool snapping_enabled = is_checked(snapping_check);
    gtk_widget_set_visible(snapping_numer_text, snapping_enabled);
    gtk_widget_set_visible(snapping_denom_text, snapping_enabled);
    gtk_widget_set_visible(snapping_prefix_label, snapping_enabled);
    gtk_widget_set_visible(snapping_label, snapping_enabled);
  }

  void set_sv_mode(GtkWidget* ctrl) {
    suppress_signals = true;
    if (ctrl == lin_sv_check) {
      if (is_checked(lin_sv_check)) {
        set_checked(pol_sv_check, false);
        set_checked(exp_sv_check, false);
        set_checked(flat_sv_check, false);
        set_checked(sin_sv_check, false);
      }
    } else if (ctrl == pol_sv_check) {
      if (is_checked(pol_sv_check)) {
        set_checked(lin_sv_check, false);
        set_checked(exp_sv_check, false);
        set_checked(flat_sv_check, false);
        set_checked(sin_sv_check, false);
      }
    } else if (ctrl == exp_sv_check) {
      if (is_checked(exp_sv_check)) {
        set_checked(lin_sv_check, false);
        set_checked(pol_sv_check, false);
        set_checked(flat_sv_check, false);
        set_checked(sin_sv_check, false);
      }
    } else if (ctrl == flat_sv_check) {
      if (is_checked(flat_sv_check)) {
        set_checked(lin_sv_check, false);
        set_checked(pol_sv_check, false);
        set_checked(exp_sv_check, false);
        set_checked(sin_sv_check, false);
      }
    } else if (ctrl == sin_sv_check) {
      if (is_checked(sin_sv_check)) {
        set_checked(lin_sv_check, false);
        set_checked(pol_sv_check, false);
        set_checked(exp_sv_check, false);
        set_checked(flat_sv_check, false);
      }
    }
    suppress_signals = false;

    bool pol_visible = is_checked(pol_sv_check);
    gtk_widget_set_visible(pol_exp_text, pol_visible);
    gtk_widget_set_visible(pol_exp_label, pol_visible);

    if (is_checked(flat_sv_check)) {
      gtk_widget_set_visible(flat_sv_scale_check, true);
      gtk_widget_set_sensitive(hit_check, false);
      gtk_widget_set_sensitive(snapping_check, false);
      gtk_widget_set_sensitive(inh_check, false);
      gtk_widget_set_sensitive(ign_bpm_check, false);

      set_flat_scaling();
      set_snapping();
    } else {
      gtk_widget_set_visible(flat_sv_scale_check, false);
      gtk_widget_set_visible(flat_sv_text, false);
      gtk_widget_set_visible(flat_sv_label, false);
      gtk_widget_set_visible(flat_sv_scale_text, false);
      gtk_widget_set_visible(flat_sv_scale_label, false);

      gtk_widget_set_sensitive(hit_check, true);
      gtk_widget_set_sensitive(snapping_check, true);
      gtk_widget_set_sensitive(inh_check, true);
      gtk_widget_set_sensitive(ign_bpm_check, true);
    }

    bool any_non_flat_sv = is_checked(lin_sv_check) || is_checked(exp_sv_check) ||
                           is_checked(pol_sv_check) || is_checked(sin_sv_check);
    gtk_widget_set_sensitive(ign_bpm_check, any_non_flat_sv && !is_checked(flat_sv_check));
  }

  void update_config() {
    if (suppress_signals) return;

    gint x = options.pos_x, y = options.pos_y;
    gint w = options.width, h = options.height;
    if (gtk_widget_get_realized(window)) {
      gtk_window_get_position(GTK_WINDOW(window), &x, &y);
      gtk_window_get_size(GTK_WINDOW(window), &w, &h);
    }

    suppress_signals = true;
    auto num_v = parse_i32(get_entry(snapping_numer_text));
    if (!num_v || *num_v <= 0) {
      set_entry(snapping_numer_text, "1");
    }
    auto den_v = parse_i32(get_entry(snapping_denom_text));
    if (!den_v || *den_v <= 0) {
      set_entry(snapping_denom_text, "1");
    }
    suppress_signals = false;

    options.map = get_entry(in_filename);
    options.inh_times = get_inherited_text();
    options.lin_sv = is_checked(lin_sv_check);
    options.pol_sv = is_checked(pol_sv_check);
    options.sin_sv = is_checked(sin_sv_check);
    options.exp_sv = is_checked(exp_sv_check);
    options.flat_sv = is_checked(flat_sv_check);
    options.vol = is_checked(vol_check);
    options.hits = is_checked(hit_check);
    options.snappings = is_checked(snapping_check);
    options.inh_lines = is_checked(inh_check);
    options.offset = get_entry(offset_text);
    options.buffer = get_entry(buffer_text);
    options.min_spacing = get_entry(min_spacing_text);
    options.pol_exp = get_entry(pol_exp_text);
    options.flat_scaling = is_checked(flat_sv_scale_check);
    options.flat_change = get_entry(flat_sv_text);
    options.flat_scaling_change = get_entry(flat_sv_scale_text);
    options.snapping_numer = get_entry(snapping_numer_text);
    options.snapping_denom = get_entry(snapping_denom_text);
    options.ignore_bpm = is_checked(ign_bpm_check);
    options.pos_x = x;
    options.pos_y = y;
    options.width = static_cast<uint32_t>( std::max(300, w) );
    options.height = static_cast<uint32_t>( std::max(350, h) );
  }

  bool save_config() {
    std::ofstream out_file(SVT_OPTIONS_FILE, std::ios::binary | std::ios::trunc);
    if (!out_file.is_open()) {
      return false;
    }
    std::string s = serialize_options_json(options);
    out_file.write(s.data(), static_cast<std::streamsize>(s.size()));
    return true;
  }

  void fill_out_filename() {
    suppress_signals = true;
    if (is_checked(preview_check)) {
      std::string in_fn = get_entry(in_filename);
      fs::path p(in_fn);
      if (p.has_parent_path() && p.has_filename()) {
        std::string folder = p.parent_path().string();
        std::string name_osu = p.filename().string();
        std::string preview_cut;
        auto bracket_pos = name_osu.find('[');
        if (bracket_pos != std::string::npos) {
          preview_cut = name_osu.substr(0, bracket_pos);
        } else {
          auto dot_pos = name_osu.find('.');
          preview_cut = (dot_pos != std::string::npos) ? name_osu.substr(0, dot_pos) : name_osu;
        }
        set_entry(out_filename, std::format("{}/{}[preview].osu", folder, preview_cut));
      } else {
        std::cout << std::format("[fof] issue with either file directory or name: [{}]\n", in_fn);
        set_status("[fof] issue with input filename");
      }
    } else {
      set_entry(out_filename, get_entry(in_filename));
    }
    suppress_signals = false;
  }

  void load_file() {
    std::string filename = get_entry(in_filename);
    if (filename.empty()) {
      std::cout << "[load] empty filename\n";
      return;
    }

    fs::path p(filename);
    if (p.extension() != ".osu") {
      std::cout << "[load] invalid file\n";
      gtk_widget_set_sensitive(apply_button, false);
      set_status("[load] please select a .osu file");
      return;
    }

    if (p.has_parent_path() && p.has_filename()) {
      std::string folder = p.parent_path().string();
      std::string name_osu = p.filename().string();
      std::cout << "[load] folder: " << folder << "\n";
      std::cout << "[load] file: " << name_osu << "\n";
      std::cout << "[load] load starting\n";

      if (auto err = svt.load_osu(filename, options)) {
        set_status(*err);
        return;
      }

      update_config();
      if (!save_config()) {
        std::cout << "[load] failed to save config file\n";
      }
      gtk_widget_set_sensitive(apply_button, true);
      set_status(std::format("editing {}", name_osu));
    } else {
      set_status("[load] issue with either file directory or name");
    }
  }

  void load_config() {
    std::ifstream f(SVT_OPTIONS_FILE, std::ios::binary);
    std::string content;
    if (f.is_open()) {
      std::ostringstream ss;
      ss << f.rdbuf();
      content = ss.str();
    }

    AppOptions loaded;
    if (!content.empty() && parse_options_json(content, loaded)) {
      options = loaded;
    } else {
      options = AppOptions{};
    }

    suppress_signals = true;
    set_inherited_text(options.inh_times);
    set_entry(in_filename, options.map);
    set_checked(lin_sv_check, options.lin_sv);
    set_checked(pol_sv_check, options.pol_sv);
    set_checked(sin_sv_check, options.sin_sv);
    set_checked(exp_sv_check, options.exp_sv);
    set_checked(flat_sv_check, options.flat_sv);
    set_checked(vol_check, options.vol);
    set_checked(hit_check, options.hits);
    set_checked(snapping_check, options.snappings);
    set_checked(inh_check, options.inh_lines);
    set_entry(offset_text, options.offset);
    set_entry(buffer_text, options.buffer);
    set_entry(min_spacing_text, options.min_spacing);
    set_entry(pol_exp_text, options.pol_exp);
    set_checked(flat_sv_scale_check, options.flat_scaling);
    set_entry(flat_sv_text, options.flat_change);
    set_entry(flat_sv_scale_text, options.flat_scaling_change);
    set_entry(snapping_numer_text, options.snapping_numer);
    set_entry(snapping_denom_text, options.snapping_denom);
    set_checked(ign_bpm_check, options.ignore_bpm);
    suppress_signals = false;

    int32_t h = std::max<int32_t>(350, static_cast<int32_t>(options.height));
    gtk_window_resize(GTK_WINDOW(window), 300, h);
    if (options.pos_x >= 0 && options.pos_y >= 0) {
      gtk_window_move(GTK_WINDOW(window), options.pos_x, options.pos_y);
    }

    fill_out_filename();
    if (get_entry(in_filename).empty()) {
      gtk_widget_set_sensitive(apply_button, false);
    } else {
      load_file();
    }
  }

  void apply_changes() {
    update_config();
    load_file();

    if (svt.all_objs.empty()) {
      set_status("[apply] no objects loaded, please check map is valid");
      return;
    }

    svt.print_debug();

    std::string cmd = get_inherited_text();
    std::istringstream iss(cmd);
    std::string start_l, end_l;
    while (iss >> start_l >> end_l) {
      if (auto err = svt.apply_two_point_fn(start_l, end_l, options)) {
        std::cout << std::format("[apply] error applying timing {} -> {}\n", start_l, end_l);
        set_status(*err);
        return;
      }
    }
    std::cout << "[apply] no more lines\n";

    size_t applied_count = 0;
    if (auto err = svt.write_output_points(
          get_entry(min_spacing_text),
          get_entry(in_filename),
          get_entry(out_filename),
          is_checked(preview_check),
          applied_count)) {
      std::cout << "[apply] error writing output\n";
      set_status(*err);
      return;
    }

    if (!save_config()) {
      std::cout << "[apply] failed to save config file\n";
    }

    gtk_widget_set_sensitive(undo_button, true);
    set_status(std::format("[apply] {} lines applied", applied_count));
  }

  void undo() {
    std::string err_msg;
    if (!exchange_files(get_entry(in_filename), "backup.osu", err_msg)) {
      set_status(std::format("[undo] failed {}", err_msg));
      return;
    }
    gtk_widget_set_sensitive(undo_button, false);
    set_status("[undo] successful");
  }

  void open_file_browser() {
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
      "Open File",
      GTK_WINDOW(window),
      GTK_FILE_CHOOSER_ACTION_OPEN,
      "_Cancel", GTK_RESPONSE_CANCEL,
      "_Open", GTK_RESPONSE_ACCEPT,
      nullptr
    );

    std::error_code ec;
    auto cwd = fs::current_path(ec);
    if (!ec) {
      gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), cwd.c_str());
    }

    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "osu(*.osu)");
    gtk_file_filter_add_pattern(filter, "*.osu");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
      if (filename) {
        set_entry(in_filename, filename);
        g_free(filename);
        fill_out_filename();
        load_file();
      } else {
        set_status("[load] failed to load file: get_selected_item failed");
      }
    }
    gtk_widget_destroy(dialog);
  }

  void handle_file_drop_path(const std::string& dropped_path) {
    set_entry(in_filename, dropped_path);
    fill_out_filename();
    load_file();
  }
};

// ============================================================================
// GTK Signal Callbacks
// ============================================================================
static void on_sv_check_toggled(GtkToggleButton* btn, gpointer user_data) {
  auto* ui = static_cast<UI*>(user_data);
  if (ui->suppress_signals) return;
  ui->set_sv_mode(GTK_WIDGET(btn));
  ui->update_config();
}

static void on_generic_option_changed(GtkWidget*, gpointer user_data) {
  auto* ui = static_cast<UI*>(user_data);
  if (ui->suppress_signals) return;
  ui->update_config();
}

static void on_snapping_toggled(GtkToggleButton*, gpointer user_data) {
  auto* ui = static_cast<UI*>(user_data);
  if (ui->suppress_signals) return;
  ui->set_snapping();
  ui->update_config();
}

static void on_flat_scaling_toggled(GtkToggleButton*, gpointer user_data) {
  auto* ui = static_cast<UI*>(user_data);
  if (ui->suppress_signals) return;
  ui->update_config();
  ui->set_flat_scaling();
}

static void on_preview_toggled(GtkToggleButton*, gpointer user_data) {
  auto* ui = static_cast<UI*>(user_data);
  if (ui->suppress_signals) return;
  ui->fill_out_filename();
  ui->update_config();
}

static void on_open_clicked(GtkButton*, gpointer user_data) {
  auto* ui = static_cast<UI*>(user_data);
  ui->open_file_browser();
}

static void on_apply_clicked(GtkButton*, gpointer user_data) {
  auto* ui = static_cast<UI*>(user_data);
  ui->apply_changes();
}

static void on_undo_clicked(GtkButton*, gpointer user_data) {
  auto* ui = static_cast<UI*>(user_data);
  ui->undo();
}

static gboolean on_window_delete(GtkWidget*, GdkEvent*, gpointer user_data) {
  auto* ui = static_cast<UI*>(user_data);
  ui->update_config();
  if (!ui->save_config()) {
    std::cout << "[close] failed to save config file\n";
  }
  gtk_main_quit();
  return FALSE;
}

static void on_drag_data_received(
    GtkWidget*,
    GdkDragContext* context,
    gint,
    gint,
    GtkSelectionData* data,
    guint,
    guint time,
    gpointer user_data) {
  auto* ui = static_cast<UI*>(user_data);
  gboolean success = FALSE;

  gchar** uris = gtk_selection_data_get_uris(data);
  if (uris) {
    for (int i = 0; uris[i] != nullptr; i++) {
      gchar* filename = g_filename_from_uri(uris[i], nullptr, nullptr);
      if (filename) {
        ui->handle_file_drop_path(filename);
        g_free(filename);
        success = TRUE;
        break;
      }
    }
    g_strfreev(uris);
  } else {
    guchar* raw_text = gtk_selection_data_get_text(data);
    if (raw_text) {
      std::string text(reinterpret_cast<char*>(raw_text));
      g_free(raw_text);
      while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
      }
      if (text.rfind("file://", 0) == 0) {
        gchar* filename = g_filename_from_uri(text.c_str(), nullptr, nullptr);
        if (filename) {
          ui->handle_file_drop_path(filename);
          g_free(filename);
          success = TRUE;
        }
      } else if (text.size() > 4 && text.substr(text.size() - 4) == ".osu") {
        ui->handle_file_drop_path(text);
        success = TRUE;
      }
    }
  }
  gtk_drag_finish(context, success, FALSE, time);
}

// Helper to place widget at exact pixel coordinate and size inside a GtkFixed
static void put_fixed(GtkWidget* fixed, GtkWidget* child, int x, int y, int w, int h) {
  gtk_widget_set_size_request(child, w, h);
  gtk_fixed_put(GTK_FIXED(fixed), child, x, y);
}

static GtkWidget* make_left_label(const char* text) {
  GtkWidget* lbl = gtk_label_new(text);
  gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
  gtk_label_set_yalign(GTK_LABEL(lbl), 0.5f);
  return lbl;
}

static GtkWidget* make_compact_entry() {
  GtkWidget* entry = gtk_entry_new();
  gtk_entry_set_width_chars(GTK_ENTRY(entry), 1);
  return entry;
}

static void load_window_icon(GtkWindow* win) {
  GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
  if (gdk_pixbuf_loader_write(loader, SVT_ICO_BYTES, SVT_ICO_LEN, nullptr)) {
    gdk_pixbuf_loader_close(loader, nullptr);
    GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
    if (pixbuf) {
      gtk_window_set_icon(win, pixbuf);
    }
  } else {
    gdk_pixbuf_loader_close(loader, nullptr);
  }
  g_object_unref(loader);
}

// Draw the 3 Win32-style 1px frames
static gboolean on_options_draw(GtkWidget* widget, cairo_t* cr, gpointer) {
  GtkStyleContext* ctx = gtk_widget_get_style_context(widget);
  GdkRGBA color{};
  gtk_style_context_get_color(ctx, GTK_STATE_FLAG_NORMAL, &color);
  cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.28);
  cairo_set_line_width(cr, 1.0);

  // Outer box (0..290, 0..140) + shared vertical dividers at x=63.5 and x=135.5
  cairo_rectangle(cr, 0.5, 0.5, 289.0, 139.0);
  cairo_move_to(cr, 63.5, 0.5);
  cairo_line_to(cr, 63.5, 139.5);
  cairo_move_to(cr, 135.5, 0.5);
  cairo_line_to(cr, 135.5, 139.5);
  cairo_stroke(cr);
  return FALSE;
}

int main(int argc, char** argv) {
  gtk_init(&argc, &argv);

  // Apply compact Win32-like CSS so every widget matches the 300x350 layout 1:1
  GtkCssProvider* provider = gtk_css_provider_new();
  const char* css = R"CSS(
    window, label, button, entry, checkbutton, textview {
      font-family: "Segoe UI", "Noto Sans", "DejaVu Sans", sans-serif;
      font-size: 10.5px;
    }
    entry {
      min-height: 0px;
      min-width: 0px;
      padding: 0px 2px;
      margin: 0px;
      border-radius: 2px;
    }
    button {
      min-height: 0px;
      min-width: 0px;
      padding: 0px 2px;
      margin: 0px;
      border-radius: 2px;
    }
    checkbutton {
      min-height: 0px;
      min-width: 0px;
      padding: 0px;
      margin: 0px;
    }
    checkbutton check {
      min-height: 10px;
      min-width: 10px;
      margin-right: 3px;
      padding: 0px;
      border-radius: 2px;
    }
    .svt-statusbar {
      border-top: 1px solid alpha(currentColor, 0.25);
      padding: 2px 4px;
    }
  )CSS";
  gtk_css_provider_load_from_data(provider, css, -1, nullptr);
  gtk_style_context_add_provider_for_screen(
    gdk_screen_get_default(),
    GTK_STYLE_PROVIDER(provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
  );

  UI ui;

  // Main Window
  ui.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(ui.window), WINDOW_TITLE);
  gtk_window_set_default_size(GTK_WINDOW(ui.window), DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT);
  gtk_window_set_type_hint(GTK_WINDOW(ui.window), GDK_WINDOW_TYPE_HINT_DIALOG);
  load_window_icon(GTK_WINDOW(ui.window));

  // Enforce 300x350 size so tiling WMs float it at the exact 300x350 size
  GdkGeometry geom{};
  geom.min_width = 300;
  geom.max_width = 300;
  geom.min_height = 350;
  geom.max_height = 350;
  gtk_window_set_geometry_hints(
    GTK_WINDOW(ui.window),
    nullptr,
    &geom,
    static_cast<GdkWindowHints>(GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE)
  );

  // Enable drag-and-drop of .osu files onto the window
  gtk_drag_dest_set(ui.window, GTK_DEST_DEFAULT_ALL, nullptr, 0, GDK_ACTION_COPY);
  gtk_drag_dest_add_uri_targets(ui.window);
  gtk_drag_dest_add_text_targets(ui.window);
  g_signal_connect(ui.window, "drag-data-received", G_CALLBACK(on_drag_data_received), &ui);
  g_signal_connect(ui.window, "delete-event", G_CALLBACK(on_window_delete), &ui);

  // Root vertical flex layout
  GtkWidget* root_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(ui.window), root_vbox);

  // 1. Top timing points TextBox (flex_grow = 1.0, margin: 5px left/right/top)
  GtkWidget* text_scroll = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(text_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(text_scroll), GTK_SHADOW_IN);
  gtk_widget_set_margin_start(text_scroll, 5);
  gtk_widget_set_margin_end(text_scroll, 5);
  gtk_widget_set_margin_top(text_scroll, 5);

  ui.inherited_text = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ui.inherited_text), GTK_WRAP_CHAR);
  ui.inherited_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ui.inherited_text));
  g_signal_connect(ui.inherited_buf, "changed", G_CALLBACK(on_generic_option_changed), &ui);
  gtk_container_add(GTK_CONTAINER(text_scroll), ui.inherited_text);
  gtk_box_pack_start(GTK_BOX(root_vbox), text_scroll, TRUE, TRUE, 0);

  // 2. options_frame (height: 140, margin: 5px left/right/top)
  GtkWidget* options_fixed = gtk_fixed_new();
  gtk_widget_set_size_request(options_fixed, 290, 140);
  gtk_widget_set_margin_start(options_fixed, 5);
  gtk_widget_set_margin_end(options_fixed, 5);
  gtk_widget_set_margin_top(options_fixed, 5);
  g_signal_connect_after(options_fixed, "draw", G_CALLBACK(on_options_draw), nullptr);
  gtk_box_pack_start(GTK_BOX(root_vbox), options_fixed, FALSE, FALSE, 0);

  // 2a. apply_frame controls (offset x = 0)
  GtkWidget* apply_label = make_left_label("Apply:");
  ui.lin_sv_check = gtk_check_button_new_with_label("Lin. SV");
  ui.exp_sv_check = gtk_check_button_new_with_label("Exp. SV");
  ui.pol_sv_check = gtk_check_button_new_with_label("Pol. SV");
  ui.sin_sv_check = gtk_check_button_new_with_label("Sin. SV");
  ui.flat_sv_check = gtk_check_button_new_with_label("Flat SV");
  ui.vol_check = gtk_check_button_new_with_label("Lin. Vol");

  put_fixed(options_fixed, apply_label, 3, 1, 58, 18);
  put_fixed(options_fixed, ui.lin_sv_check, 3, 20, 58, 19);
  put_fixed(options_fixed, ui.exp_sv_check, 3, 40, 58, 19);
  put_fixed(options_fixed, ui.pol_sv_check, 3, 60, 58, 19);
  put_fixed(options_fixed, ui.sin_sv_check, 3, 80, 58, 19);
  put_fixed(options_fixed, ui.flat_sv_check, 3, 100, 58, 19);
  put_fixed(options_fixed, ui.vol_check, 3, 119, 58, 19);

  // 2b. apply_to_frame controls (offset x = 63)
  GtkWidget* apply_to_label = make_left_label("To:");
  ui.hit_check = gtk_check_button_new_with_label("Hits");
  ui.snapping_check = gtk_check_button_new_with_label("Snaps");
  ui.inh_check = gtk_check_button_new_with_label("Inh. lines");

  put_fixed(options_fixed, apply_to_label, 63 + 3, 1, 68, 18);
  put_fixed(options_fixed, ui.hit_check, 63 + 3, 20, 68, 19);
  put_fixed(options_fixed, ui.snapping_check, 63 + 3, 40, 68, 19);
  put_fixed(options_fixed, ui.inh_check, 63 + 3, 60, 68, 19);

  // 2c. advanced_options_frame controls (offset x = 135)
  GtkWidget* advanced_options_label = make_left_label("Advanced Options:");
  ui.offset_text = make_compact_entry();
  ui.offset_label = make_left_label("Offset");
  ui.buffer_text = make_compact_entry();
  ui.buffer_label = make_left_label("Buffer");
  ui.min_spacing_text = make_compact_entry();
  ui.min_spacing_label = make_left_label("Min. Spacing");

  ui.pol_exp_text = make_compact_entry();
  ui.pol_exp_label = make_left_label("Exp.");
  ui.flat_sv_text = make_compact_entry();
  ui.flat_sv_label = make_left_label("SV Change");
  ui.flat_sv_scale_text = make_compact_entry();
  ui.flat_sv_scale_label = make_left_label("SV Scaling");
  ui.flat_sv_scale_check = gtk_check_button_new_with_label("Mult. Scaling");

  ui.snapping_numer_text = make_compact_entry();
  ui.snapping_prefix_label = make_left_label("/");
  ui.snapping_denom_text = make_compact_entry();
  ui.snapping_label = make_left_label("Snapping");
  ui.ign_bpm_check = gtk_check_button_new_with_label("Ignore BPM");

  put_fixed(options_fixed, advanced_options_label, 135 + 3, 1, 150, 18);
  put_fixed(options_fixed, ui.offset_text, 135 + 3, 20, 21, 18);
  put_fixed(options_fixed, ui.offset_label, 135 + 26, 20, 44, 18);
  put_fixed(options_fixed, ui.ign_bpm_check, 135 + 71, 20, 82, 19);

  put_fixed(options_fixed, ui.buffer_text, 135 + 3, 40, 21, 18);
  put_fixed(options_fixed, ui.buffer_label, 135 + 26, 40, 44, 18);
  put_fixed(options_fixed, ui.flat_sv_scale_check, 135 + 71, 40, 82, 19);

  put_fixed(options_fixed, ui.min_spacing_text, 135 + 3, 60, 21, 18);
  put_fixed(options_fixed, ui.min_spacing_label, 135 + 26, 60, 85, 18);

  put_fixed(options_fixed, ui.pol_exp_text, 135 + 3, 80, 21, 18);
  put_fixed(options_fixed, ui.pol_exp_label, 135 + 26, 80, 60, 18);
  put_fixed(options_fixed, ui.flat_sv_text, 135 + 3, 80, 21, 18);
  put_fixed(options_fixed, ui.flat_sv_label, 135 + 26, 80, 100, 18);
  put_fixed(options_fixed, ui.flat_sv_scale_text, 135 + 3, 80, 21, 18);
  put_fixed(options_fixed, ui.flat_sv_scale_label, 135 + 26, 80, 100, 18);

  put_fixed(options_fixed, ui.snapping_numer_text, 135 + 3, 117, 19, 18);
  put_fixed(options_fixed, ui.snapping_prefix_label, 135 + 24, 117, 6, 18);
  put_fixed(options_fixed, ui.snapping_denom_text, 135 + 31, 117, 19, 18);
  put_fixed(options_fixed, ui.snapping_label, 135 + 53, 117, 95, 18);

  // 3. mapselect_frame (height: 55, margin: 5px left/right/top)
  GtkWidget* mapselect_fixed = gtk_fixed_new();
  gtk_widget_set_size_request(mapselect_fixed, 290, 55);
  gtk_widget_set_margin_start(mapselect_fixed, 5);
  gtk_widget_set_margin_end(mapselect_fixed, 5);
  gtk_widget_set_margin_top(mapselect_fixed, 5);
  gtk_box_pack_start(GTK_BOX(root_vbox), mapselect_fixed, FALSE, FALSE, 0);

  ui.open_button = gtk_button_new_with_label("Select Map");
  ui.in_filename = make_compact_entry();
  gtk_widget_set_sensitive(ui.in_filename, FALSE);
  ui.preview_check = gtk_check_button_new_with_label("Preview Diff");
  ui.out_filename = make_compact_entry();

  put_fixed(mapselect_fixed, ui.open_button, 0, 0, 87, 25);
  put_fixed(mapselect_fixed, ui.in_filename, 90, 1, 200, 23);
  put_fixed(mapselect_fixed, ui.preview_check, 0, 30, 87, 25);
  put_fixed(mapselect_fixed, ui.out_filename, 90, 31, 200, 23);

  // 4. applyundo_frame (height: 25, margin: 5px left/right/top)
  GtkWidget* applyundo_fixed = gtk_fixed_new();
  gtk_widget_set_size_request(applyundo_fixed, 290, 25);
  gtk_widget_set_margin_start(applyundo_fixed, 5);
  gtk_widget_set_margin_end(applyundo_fixed, 5);
  gtk_widget_set_margin_top(applyundo_fixed, 5);
  gtk_box_pack_start(GTK_BOX(root_vbox), applyundo_fixed, FALSE, FALSE, 0);

  ui.apply_button = gtk_button_new_with_label("Apply");
  gtk_widget_set_sensitive(ui.apply_button, FALSE);
  ui.undo_button = gtk_button_new_with_label("Undo");
  gtk_widget_set_sensitive(ui.undo_button, FALSE);

  put_fixed(applyundo_fixed, ui.apply_button, 0, 0, 242, 25);
  put_fixed(applyundo_fixed, ui.undo_button, 245, 0, 45, 25);

  // 5. Status bar at the bottom
  GtkWidget* status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_style_context_add_class(gtk_widget_get_style_context(status_box), "svt-statusbar");
  gtk_widget_set_margin_top(status_box, 5);
  ui.status_label = make_left_label("[map] no map selected (Select Map or drag one in)");
  gtk_label_set_ellipsize(GTK_LABEL(ui.status_label), PANGO_ELLIPSIZE_END);
  gtk_box_pack_start(GTK_BOX(status_box), ui.status_label, TRUE, TRUE, 2);
  gtk_box_pack_end(GTK_BOX(root_vbox), status_box, FALSE, FALSE, 0);

  // Connect signals
  g_signal_connect(ui.lin_sv_check, "toggled", G_CALLBACK(on_sv_check_toggled), &ui);
  g_signal_connect(ui.exp_sv_check, "toggled", G_CALLBACK(on_sv_check_toggled), &ui);
  g_signal_connect(ui.pol_sv_check, "toggled", G_CALLBACK(on_sv_check_toggled), &ui);
  g_signal_connect(ui.sin_sv_check, "toggled", G_CALLBACK(on_sv_check_toggled), &ui);
  g_signal_connect(ui.flat_sv_check, "toggled", G_CALLBACK(on_sv_check_toggled), &ui);
  g_signal_connect(ui.vol_check, "toggled", G_CALLBACK(on_generic_option_changed), &ui);

  g_signal_connect(ui.hit_check, "toggled", G_CALLBACK(on_generic_option_changed), &ui);
  g_signal_connect(ui.snapping_check, "toggled", G_CALLBACK(on_snapping_toggled), &ui);
  g_signal_connect(ui.inh_check, "toggled", G_CALLBACK(on_generic_option_changed), &ui);

  g_signal_connect(ui.offset_text, "changed", G_CALLBACK(on_generic_option_changed), &ui);
  g_signal_connect(ui.buffer_text, "changed", G_CALLBACK(on_generic_option_changed), &ui);
  g_signal_connect(ui.min_spacing_text, "changed", G_CALLBACK(on_generic_option_changed), &ui);
  g_signal_connect(ui.pol_exp_text, "changed", G_CALLBACK(on_generic_option_changed), &ui);
  g_signal_connect(ui.flat_sv_text, "changed", G_CALLBACK(on_generic_option_changed), &ui);
  g_signal_connect(ui.flat_sv_scale_text, "changed", G_CALLBACK(on_generic_option_changed), &ui);
  g_signal_connect(ui.flat_sv_scale_check, "toggled", G_CALLBACK(on_flat_scaling_toggled), &ui);

  g_signal_connect(ui.snapping_numer_text, "changed", G_CALLBACK(on_generic_option_changed), &ui);
  g_signal_connect(ui.snapping_denom_text, "changed", G_CALLBACK(on_generic_option_changed), &ui);
  g_signal_connect(ui.ign_bpm_check, "toggled", G_CALLBACK(on_generic_option_changed), &ui);

  g_signal_connect(ui.open_button, "clicked", G_CALLBACK(on_open_clicked), &ui);
  g_signal_connect(ui.preview_check, "toggled", G_CALLBACK(on_preview_toggled), &ui);
  g_signal_connect(ui.out_filename, "changed", G_CALLBACK(on_generic_option_changed), &ui);

  g_signal_connect(ui.apply_button, "clicked", G_CALLBACK(on_apply_clicked), &ui);
  g_signal_connect(ui.undo_button, "clicked", G_CALLBACK(on_undo_clicked), &ui);

  // Show all widgets first, then apply mode-based visibility and config
  gtk_widget_show_all(ui.window);

  ui.load_config();
  gtk_widget_set_sensitive(ui.undo_button, false);
  ui.set_sv_mode(ui.lin_sv_check);
  ui.set_flat_scaling();
  ui.set_snapping();

  // Register tooltips if enabled in config (matching src/main.rs 1:1)
  if (ui.options.tooltips) {
    gtk_widget_set_tooltip_text(ui.inherited_text, "Paste timing point start/end pair(s) here. Copy/paste from timing panel. These timing points contain the start/end times, SVs, and volumes which are interpolated for the selected objects. (Example format: 111376,-76.92308,4,1,0,100,0,1)");
    gtk_widget_set_tooltip_text(ui.lin_sv_check, "Change slider velocity linearly for selected objects (hits/snappings/inh. lines)");
    gtk_widget_set_tooltip_text(ui.exp_sv_check, "Change slider velocity exponentially for selected objects (hits/snappings/inh. lines)");
    gtk_widget_set_tooltip_text(ui.pol_sv_check, "Change slider velocity polynomially using exp. factor for selected objects (hits/snappings/inh. lines)");
    gtk_widget_set_tooltip_text(ui.sin_sv_check, "Change slider velocity sinusoidally for selected objects (hits/snappings/inh. lines)");
    gtk_widget_set_tooltip_text(ui.flat_sv_check, "Flat SV change for selected inh. lines");
    gtk_widget_set_tooltip_text(ui.flat_sv_scale_check, "Flat multiplicative SV change for selected inh. lines");
    gtk_widget_set_tooltip_text(ui.vol_check, "Change volume smoothly for selected objects (hits/snappings/inh. lines)");
    gtk_widget_set_tooltip_text(ui.hit_check, "Change hitobjects (notes, spinners, sliders) between start/end points");
    gtk_widget_set_tooltip_text(ui.snapping_check, "Change all snappings between start/end points");
    gtk_widget_set_tooltip_text(ui.inh_check, "Change current inherited lines between start/end points");
    gtk_widget_set_tooltip_text(ui.offset_label, "(integer) Place new timing points at offset (in ms) from map object (negative offset for before, positive for after)");
    gtk_widget_set_tooltip_text(ui.offset_text, "(integer) Place new timing points at offset (in ms) from map object (negative offset for before, positive for after)");
    gtk_widget_set_tooltip_text(ui.buffer_label, "(integer) Include map objects (in ms) before and after the start/end timing points, useful if objects are not perfectly snapped");
    gtk_widget_set_tooltip_text(ui.buffer_text, "(integer) Include map objects (in ms) before and after the start/end timing points, useful if objects are not perfectly snapped");
    gtk_widget_set_tooltip_text(ui.min_spacing_label, "(integer) Minimum spacing around tool-placed points (in ms) where other points must either follow social distancing or be removed");
    gtk_widget_set_tooltip_text(ui.min_spacing_text, "(integer) Minimum spacing around tool-placed points (in ms) where other points must either follow social distancing or be removed");
    gtk_widget_set_tooltip_text(ui.pol_exp_label, "(decimal) Exponent for polynomial SV. Recommended values are [0.5, 1) for slowdowns and (1.0, 2.0] for speedups. Applied following a (sv_diff) * (t / t_diff)^exp curve");
    gtk_widget_set_tooltip_text(ui.pol_exp_text, "(decimal) Exponent for polynomial SV. Recommended values are [0.5, 1) for slowdowns and (1.0, 2.0] for speedups. Applied following a (sv_diff) * (t / t_diff)^exp curve");
    gtk_widget_set_tooltip_text(ui.flat_sv_label, "(decimal) Amount of SV change to apply to each inherited line");
    gtk_widget_set_tooltip_text(ui.flat_sv_text, "(decimal) Amount of SV change to apply to each inherited line");
    gtk_widget_set_tooltip_text(ui.flat_sv_scale_label, "(decimal) Scaling factor to apply to each inherited line");
    gtk_widget_set_tooltip_text(ui.flat_sv_scale_text, "(decimal) Scaling factor to apply to each inherited line");
    gtk_widget_set_tooltip_text(ui.ign_bpm_check, "End timing point SV is normally relative to end timing point BPM, but if checked, can be made relative to start timing point BPM");
    gtk_widget_set_tooltip_text(ui.open_button, "Select map to change");
    gtk_widget_set_tooltip_text(ui.in_filename, "Map being edited");
    gtk_widget_set_tooltip_text(ui.out_filename, "Output location");
    gtk_widget_set_tooltip_text(ui.preview_check, "If enabled, creates a preview diff alongside your current diff which shows how the changes would potentially look without touching the original");
    gtk_widget_set_tooltip_text(ui.apply_button, "Apply SV/vol changes");
    gtk_widget_set_tooltip_text(ui.undo_button, "Undo most recent change");
  }

  // Optional --screenshot=<path> for visual inspection
  for (int i = 1; i < argc; i++) {
    std::string arg(argv[i]);
    if (arg.rfind("--screenshot=", 0) == 0) {
      std::string out_png = arg.substr(13);
      while (gtk_events_pending()) {
        gtk_main_iteration();
      }
      GtkAllocation alloc{};
      gtk_widget_get_allocation(ui.window, &alloc);
      cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, alloc.width, alloc.height);
      cairo_t* cr = cairo_create(surface);
      gtk_widget_draw(ui.window, cr);
      cairo_destroy(cr);
      cairo_surface_write_to_png(surface, out_png.c_str());
      cairo_surface_destroy(surface);
      return 0;
    }
  }

  gtk_main();
  return 0;
}
