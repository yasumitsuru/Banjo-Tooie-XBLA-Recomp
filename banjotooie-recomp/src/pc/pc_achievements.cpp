// CONQUISTAS no PC (M04, C-6). Ver pc_achievements.h.
#include "pc_achievements.h"

#include <rex/kernel/xam/apps/xgi_app.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/util/xdbf_utils.h>
#include <rex/system/xcontent.h>

#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#include "pc_lang.h"
#include "pc_log.h"

namespace fs = std::filesystem;

namespace pc::achievements {
namespace {

std::mutex g_mu;
fs::path g_file;
std::map<uint32_t, std::string> g_unlocked;  // id -> data

void LoadLocked() {
  g_unlocked.clear();
  std::ifstream in(g_file);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream s(line);
    uint32_t id = 0;
    if (!(s >> id)) continue;
    std::string when;
    std::getline(s >> std::ws, when);
    g_unlocked[id] = when;
  }
}

void SaveLocked() {
  if (g_file.empty()) return;
  std::error_code ec;
  fs::create_directories(g_file.parent_path(), ec);
  fs::path tmp = g_file;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << "# Conquistas obtidas neste PC (id e data). Banjo-Tooie PT-BR Recompiled.\n";
    for (const auto& [id, when] : g_unlocked) out << id << '\t' << when << '\n';
  }
  fs::rename(tmp, g_file, ec);
}

std::string Now() {
  std::time_t t = std::time(nullptr);
  std::tm lt{};
  localtime_s(&lt, &t);
  char b[32];
  std::strftime(b, sizeof b, "%Y-%m-%d %H:%M", &lt);
  return b;
}

void Record(uint32_t id, const char* how) {
  bool fresh = false;
  {
    std::lock_guard lk(g_mu);
    if (!g_unlocked.count(id)) {
      g_unlocked[id] = Now();
      SaveLocked();
      fresh = true;
    }
  }
  pc::log::Event("conquista", "id " + std::to_string(id) + (fresh ? " obtida" : " já obtida") +
                                  " (" + how + ")");
}

void OnWritten(uint32_t user_index, uint32_t id) {
  (void)user_index;
  Record(id, "XGIUserWriteAchievements");
}

}  // namespace

void Install(const fs::path& user_data_root) {
  {
    std::lock_guard lk(g_mu);
    g_file = user_data_root / "conquistas.txt";
    LoadLocked();
  }
  rex::kernel::xam::apps::SetAchievementWrittenCallback(&OnWritten);
}

std::vector<Item> List() {
  std::vector<Item> out;
  auto* ks = rex::system::kernel_state();
  if (!ks) return out;
  const rex::system::util::XdbfGameData db = ks->title_xdbf();
  if (!db.is_valid()) return out;
  const auto want = pc::lang::English() ? rex::system::XLanguage::kEnglish
                                        : rex::system::XLanguage::kPortuguese;
  const auto lang = db.GetExistingLanguage(want);
  std::lock_guard lk(g_mu);
  for (const auto& e : db.GetAchievements()) {
    Item it;
    it.id = e.id;
    it.name = db.GetStringTableEntry(lang, e.label_id);
    it.description = db.GetStringTableEntry(lang, e.description_id);
    it.points = e.gamerscore;
    const auto f = g_unlocked.find(e.id);
    it.unlocked = f != g_unlocked.end();
    if (it.unlocked) it.when = f->second;
    out.push_back(std::move(it));
  }
  return out;
}

void SimulateUnlock(uint32_t id) { Record(id, "teste"); }

const fs::path& File() { return g_file; }

}  // namespace pc::achievements
