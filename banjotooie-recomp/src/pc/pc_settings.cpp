#include "pc_settings.h"

#include <rex/cvar.h>

#include "pc_log.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace pc {
namespace {
Settings g_settings;
std::filesystem::path g_path;
std::string g_last_serialized;  // última versão gravada/lida (registro de sessão: o que mudou)
bool g_migrated_monitor = false;  // arquivo de antes da linha MONITOR (versão < 4)

bool ParseBool(const std::string& v) { return v == "true" || v == "1" || v == "sim"; }

int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

float ClampFloat(const std::string& v, float lo, float hi, float def) {
  try {
    float f = std::stof(v);
    return f < lo ? lo : (f > hi ? hi : f);
  } catch (...) {
    return def;
  }
}

std::string TrimQuotes(std::string x) {
  while (!x.empty() && (x.back() == ' ' || x.back() == '\r' || x.back() == '"')) x.pop_back();
  size_t i = 0;
  while (i < x.size() && (x[i] == ' ' || x[i] == '"')) ++i;
  return x.substr(i);
}
}  // namespace

Settings& Get() { return g_settings; }
void SetPath(const std::filesystem::path& file) { g_path = file; }
const std::filesystem::path& Path() { return g_path; }

std::string Serialize(const Settings& s);

const char* DisplayModeName(DisplayMode d) {
  switch (d) {
    case DisplayMode::SemBordas: return "SemBordas";
    case DisplayMode::TelaCheia: return "TelaCheia";
    default: return "Janela";
  }
}

const char* ResolutionName(Resolution r) {
  switch (r) {
    case Resolution::R1080p: return "1080p";
    case Resolution::R1440p: return "1440p";
    default: return "4k";
  }
}

const char* DialogSpeedName(int speed) {
  switch (speed) {
    case 1: return "Rápida (2x)";
    case 2: return "Muito rápida (4x)";
    case 3: return "Instantânea";
    default: return "Original";
  }
}

float DialogSpeedFactor(int speed) {
  switch (speed) {
    case 1: return 2.0f;
    case 2: return 4.0f;
    case 3: return 1000.0f;
    default: return 1.0f;
  }
}

const char* DrawDistanceName(DrawDistance d) {
  switch (d) {
    case DrawDistance::Longe: return "Longe";
    case DrawDistance::Total: return "Total";
    default: return "Original";
  }
}

bool Load() {
  if (g_path.empty()) return false;
  std::ifstream in(g_path);
  if (!in) return false;
  Settings s;
  int version = 1;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#' || line[0] == '[') continue;
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    auto trim = [](std::string x) {
      while (!x.empty() && (x.back() == ' ' || x.back() == '\r' || x.back() == '"')) x.pop_back();
      size_t i = 0;
      while (i < x.size() && (x[i] == ' ' || x[i] == '"')) ++i;
      return x.substr(i);
    };
    std::string rest = line.substr(eq + 1);
    // comentário no fim da linha ("valor  # texto"), fora de aspas
    {
      bool quoted = false;
      for (size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == '"') quoted = !quoted;
        else if (rest[i] == '#' && !quoted) {
          rest.resize(i);
          break;
        }
      }
    }
    std::string k = trim(line.substr(0, eq)), v = trim(rest);
    if (k == "settings_version") version = std::atoi(v.c_str());
    else if (k == "fps60") s.fps60 = ParseBool(v);
    else if (k == "lock60") s.lock60 = ParseBool(v);
    else if (k == "scenes60") s.scenes60 = ParseBool(v);
    else if (k == "draw_distance") s.draw_distance = v == "Total" ? DrawDistance::Total
                                                   : v == "Longe" ? DrawDistance::Longe
                                                                  : DrawDistance::Original;
    else if (k == "pause_on_focus_loss") s.pause_on_focus_loss = ParseBool(v);
    else if (k == "skip_intro_logos") s.skip_intro_logos = ParseBool(v);
    else if (k == "show_fps") s.show_fps = ParseBool(v);
    else if (k == "show_perf_stats") s.show_perf_stats = ParseBool(v);
    else if (k == "show_collectibles") s.show_collectibles = ParseBool(v);
    else if (k == "pc_menu_entry") s.pc_menu_entry = ParseBool(v);
    else if (k == "dialog_speed") s.dialog_speed = ClampInt(std::atoi(v.c_str()), 0, 3);
    else if (k == "volume_master") s.volume_master = ClampInt(std::atoi(v.c_str()), 0, 100);
    else if (k == "display_mode") s.display_mode = v == "TelaCheia" ? DisplayMode::TelaCheia
                                                 : v == "SemBordas" ? DisplayMode::SemBordas
                                                                    : DisplayMode::Janela;
    else if (k == "resolution") s.resolution = v == "1080p" ? Resolution::R1080p
                                             : v == "1440p" ? Resolution::R1440p
                                                            : Resolution::R2160p;
    else if (k == "resolution_auto") s.resolution_auto = ParseBool(v);
    else if (k == "monitor") s.monitor = TrimQuotes(v);
    else if (k == "mouse_look") s.mouse_look = ParseBool(v);
    else if (k == "cam_sens_x") s.cam_sens_x = ClampFloat(v, 0.1f, 3.0f, 1.0f);
    else if (k == "cam_sens_y") s.cam_sens_y = ClampFloat(v, 0.1f, 3.0f, 1.0f);
    else if (k == "cam_invert_x") s.cam_invert_x = ParseBool(v);
    else if (k == "cam_invert_y") s.cam_invert_y = ParseBool(v);
    else if (k == "cam_smoothing") s.cam_smoothing = ClampFloat(v, 0.0f, 0.9f, 0.25f);
    else if (k == "cam_key_up") s.cam_key_up = TrimQuotes(v);
    else if (k == "cam_key_down") s.cam_key_down = TrimQuotes(v);
    else if (k == "cam_key_left") s.cam_key_left = TrimQuotes(v);
    else if (k == "cam_key_right") s.cam_key_right = TrimQuotes(v);
    else if (k == "pad_deadzone") s.pad_deadzone = ClampFloat(v, 0.0f, 0.9f, 0.12f);
    else if (k == "pad_sens_x") s.pad_sens_x = ClampFloat(v, 0.1f, 3.0f, 1.0f);
    else if (k == "pad_sens_y") s.pad_sens_y = ClampFloat(v, 0.1f, 3.0f, 1.0f);
    else if (k == "pad_invert_x") s.pad_invert_x = ParseBool(v);
    else if (k == "pad_invert_y") s.pad_invert_y = ParseBool(v);
    else if (k == "pad_vibration") s.pad_vibration = ParseBool(v);
    else if (k == "save_backup") s.save_backup = ParseBool(v);
    else if (k == "backup_count") s.backup_count = ClampInt(std::atoi(v.c_str()), 1, 50);
    else if (k == "log_detailed") s.log_detailed = ParseBool(v);
  }
  // Versão 1 gravava "lock60 = true  # comentário", que era lido como falso: o valor falso
  // desses arquivos não foi escolha do jogador, então volta ao padrão (travado a 60).
  if (version < 2) s.lock60 = true;
  // Versão 2: lock60 também acelerava as cenas; agora isso é scenes60 (desligado por padrão).
  if (version < 3) s.scenes60 = false;
  // Versão 4 (M04): linha MONITOR. Quem já jogava (arquivo de versão anterior) abria sempre no
  // monitor principal: continua assim (preenchido na abertura com o principal); o automático
  // pelo monitor do cursor vale para quem começa agora.
  g_migrated_monitor = version < 4;
  g_settings = s;
  g_last_serialized = Serialize(g_settings);
  return true;
}

std::string Serialize(const Settings& s) {
  std::ostringstream o;
  o << "# Configurações de PC — Banjo-Tooie PT-BR (independente dos saves do jogo)\n"
    << "settings_version = 4\n"
    << "[jogo]\n"
    << "fps60 = " << (s.fps60 ? "true" : "false") << "\n"
    << "lock60 = " << (s.lock60 ? "true" : "false") << "  # ritmo da tela travado em 60\n"
    << "scenes60 = " << (s.scenes60 ? "true" : "false") << "  # cenas também a 60 (experimental)\n"
    << "draw_distance = \"" << DrawDistanceName(s.draw_distance) << "\"\n"
    << "pause_on_focus_loss = " << (s.pause_on_focus_loss ? "true" : "false") << "\n"
    << "skip_intro_logos = " << (s.skip_intro_logos ? "true" : "false") << "\n"
    << "show_fps = " << (s.show_fps ? "true" : "false") << "\n"
    << "show_perf_stats = " << (s.show_perf_stats ? "true" : "false") << "\n"
    << "show_collectibles = " << (s.show_collectibles ? "true" : "false") << "\n"
    << "pc_menu_entry = " << (s.pc_menu_entry ? "true" : "false") << "  # entrada de PC nos menus do jogo\n"
    << "dialog_speed = " << s.dialog_speed << "  # 0 Original, 1 Rápida, 2 Muito rápida, 3 Instantânea\n"
    << "[video]\n"
    << "resolution = \"" << ResolutionName(s.resolution) << "\"\n"
    << "resolution_auto = " << (s.resolution_auto ? "true" : "false") << "\n"
    << "display_mode = \"" << DisplayModeName(s.display_mode) << "\"\n"
    << "monitor = \"" << s.monitor << "\"  # vazio = automático (monitor do cursor na abertura)\n"
    << "[camera]\n"
    << "mouse_look = " << (s.mouse_look ? "true" : "false") << "\n"
    << "cam_sens_x = " << s.cam_sens_x << "\n"
    << "cam_sens_y = " << s.cam_sens_y << "\n"
    << "cam_invert_x = " << (s.cam_invert_x ? "true" : "false") << "\n"
    << "cam_invert_y = " << (s.cam_invert_y ? "true" : "false") << "\n"
    << "cam_smoothing = " << s.cam_smoothing << "\n"
    << "cam_key_up = \"" << s.cam_key_up << "\"\n"
    << "cam_key_down = \"" << s.cam_key_down << "\"\n"
    << "cam_key_left = \"" << s.cam_key_left << "\"\n"
    << "cam_key_right = \"" << s.cam_key_right << "\"\n"
    << "[controle]\n"
    << "pad_deadzone = " << s.pad_deadzone << "\n"
    << "pad_sens_x = " << s.pad_sens_x << "\n"
    << "pad_sens_y = " << s.pad_sens_y << "\n"
    << "pad_invert_x = " << (s.pad_invert_x ? "true" : "false") << "\n"
    << "pad_invert_y = " << (s.pad_invert_y ? "true" : "false") << "\n"
    << "pad_vibration = " << (s.pad_vibration ? "true" : "false") << "\n"
    << "[saves]\n"
    << "save_backup = " << (s.save_backup ? "true" : "false") << "\n"
    << "backup_count = " << s.backup_count << "\n"
    << "[audio]\n"
    << "volume_master = " << s.volume_master << "\n"
    << "[registro]\n"
    << "log_detailed = " << (s.log_detailed ? "true" : "false")
    << "  # registro de sessão detalhado (logs\\sessao-*.txt)\n";
  return o.str();
}

bool Save() {
  if (g_path.empty()) return false;
  std::error_code ec;
  std::filesystem::create_directories(g_path.parent_path(), ec);
  const std::string now = Serialize(g_settings);
  // Registro de sessão (M04, C-10): quais opções mudaram desde a última gravação/leitura.
  {
    std::string& last = g_last_serialized;
    if (!last.empty() && last != now) {
      std::istringstream a(last), b(now);
      std::string la, lb, diff;
      while (std::getline(b, lb)) {
        if (!std::getline(a, la)) la.clear();
        if (la != lb && lb.find('=') != std::string::npos) {
          if (!diff.empty()) diff += "; ";
          diff += lb.substr(0, lb.find('#'));
        }
      }
      pc::log::Event("opcoes", "gravadas: " + diff);
    } else if (last.empty()) {
      pc::log::Event("opcoes", "gravadas em " + g_path.filename().string());
    }
    last = now;
  }
  auto tmp = g_path;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << now;
    out.flush();
    if (!out) return false;
  }
  if (std::filesystem::exists(g_path, ec)) {
    auto bak = g_path;
    bak += ".bak";
    std::filesystem::copy_file(g_path, bak, std::filesystem::copy_options::overwrite_existing, ec);
  }
  std::filesystem::rename(tmp, g_path, ec);
  return !ec;
}

bool MigratedFromOldMonitorRule() { return g_migrated_monitor; }

const char* InternalScaleText(Resolution r) { return r == Resolution::R2160p ? "3" : "2"; }

void ApplyRuntimeOptions() {
  const Settings& s = g_settings;
  rex::cvar::SetFlagByName("pad_deadzone", std::to_string(s.pad_deadzone));
  rex::cvar::SetFlagByName("pad_sens_x", std::to_string(s.pad_sens_x));
  rex::cvar::SetFlagByName("pad_sens_y", std::to_string(s.pad_sens_y));
  rex::cvar::SetFlagByName("pad_invert_x", s.pad_invert_x ? "true" : "false");
  rex::cvar::SetFlagByName("pad_invert_y", s.pad_invert_y ? "true" : "false");
  rex::cvar::SetFlagByName("pad_vibration", s.pad_vibration ? "true" : "false");
  rex::cvar::SetFlagByName("audio_volume", std::to_string(s.volume_master));
}

}  // namespace pc
