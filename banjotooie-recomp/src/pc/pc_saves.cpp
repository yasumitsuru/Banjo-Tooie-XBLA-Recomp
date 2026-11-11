#include "pc_saves.h"

#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "pc_settings.h"

namespace pc::saves {
namespace {

constexpr size_t kSaveSize = 3072;
constexpr size_t kMaxBackupsHardLimit = 100;

std::atomic<bool> g_running{false};
std::thread g_thread;
std::mutex g_mutex;
std::filesystem::path g_root;
std::filesystem::path g_save_file;
std::filesystem::path g_backup_dir;
std::string g_status = "Backups ainda não iniciados.";
std::string g_last_hash;

void SetStatus(const std::string& s) {
  std::lock_guard lock(g_mutex);
  g_status = s;
}

std::string Timestamp() {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const auto t = system_clock::to_time_t(now);
  std::tm tm{};
  localtime_s(&tm, &t);
  char buf[64];
  std::snprintf(buf, sizeof buf, "%04d%02d%02d-%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1,
                tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

uint64_t HashBytes(const std::vector<uint8_t>& data) {
  uint64_t h = 1469598103934665603ull;
  for (uint8_t b : data) {
    h ^= b;
    h *= 1099511628211ull;
  }
  return h;
}

std::string Hex64(uint64_t v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)v);
  return buf;
}

bool ReadFile(const std::filesystem::path& path, std::vector<uint8_t>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return in.good() || in.eof();
}

bool LooksValid(const std::vector<uint8_t>& data) {
  if (data.size() != kSaveSize) return false;
  size_t zeros = 0, des = 0;
  for (uint8_t b : data) {
    if (b == 0x00) ++zeros;
    if (b == 0xDE) ++des;
  }
  // um save em branco é válido; um arquivo todo 0xFF ou truncado não é
  if (zeros == data.size()) return true;
  if (des == data.size()) return false;
  return true;
}

std::filesystem::path FindSaveFile() {
  std::error_code ec;
  if (!std::filesystem::exists(g_root, ec)) return {};
  for (auto it = std::filesystem::recursive_directory_iterator(
           g_root, std::filesystem::directory_options::skip_permission_denied, ec);
       it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (it.depth() > 6) {
      it.disable_recursion_pending();
      continue;
    }
    if (it->is_regular_file(ec) && it->path().filename() == L"savegame.dat") return it->path();
  }
  return {};
}

std::vector<std::filesystem::path> Backups() {
  std::vector<std::filesystem::path> list;
  std::error_code ec;
  if (!std::filesystem::exists(g_backup_dir, ec)) return list;
  for (const auto& e : std::filesystem::directory_iterator(g_backup_dir, ec)) {
    if (!e.is_regular_file(ec)) continue;
    const auto name = e.path().filename().wstring();
    if (name.rfind(L"save-", 0) == 0 && e.path().extension() == L".dat")
      list.push_back(e.path());
  }
  std::sort(list.begin(), list.end());
  return list;
}

// Copia o save para a pasta de backups, verificando o conteúdo.
bool MakeBackup(const std::filesystem::path& save_file, const std::string& tag = "save") {
  std::vector<uint8_t> data;
  if (!ReadFile(save_file, data) || !LooksValid(data)) return false;
  std::error_code ec;
  std::filesystem::create_directories(g_backup_dir, ec);
  const std::string hash = Hex64(HashBytes(data));
  const std::string ts = Timestamp();
  const std::wstring wts(ts.begin(), ts.end());
  const std::wstring whash(hash.begin(), hash.end());
  const auto name = std::filesystem::path(L"save-" + wts + L"-" + whash + L".dat");
  const auto dest = g_backup_dir / name;
  std::filesystem::copy_file(save_file, dest, std::filesystem::copy_options::overwrite_existing,
                             ec);
  if (ec) return false;
  // confere a cópia
  std::vector<uint8_t> check;
  if (!ReadFile(dest, check) || HashBytes(check) != HashBytes(data)) return false;
  {
    std::ofstream log(g_backup_dir / "backups.log", std::ios::app);
    log << Timestamp() << " backup " << tag << " " << hash << "\n";
  }
  // mantém no máximo o número configurado (entre 1 e 50)
  auto list = Backups();
  const int keep = std::clamp(pc::Get().backup_count, 1, 50);
  while (int(list.size()) > keep) {
    std::filesystem::remove(list.front(), ec);
    list.erase(list.begin());
  }
  return true;
}

void StartupRecovery() {
  g_save_file = FindSaveFile();
  if (g_save_file.empty()) {
    SetStatus("Save do jogo ainda não existe; os backups começam quando ele aparecer.");
    return;
  }
  std::vector<uint8_t> data;
  const bool exists = ReadFile(g_save_file, data);
  if (!exists || !LooksValid(data)) {
    if (RestoreLatest()) {
      SetStatus("Save ausente ou inválido: backup mais recente restaurado.");
    } else {
      SetStatus("Save inválido e nenhum backup válido encontrado.");
    }
    return;
  }
  g_last_hash = Hex64(HashBytes(data));
  if (Backups().empty()) MakeBackup(g_save_file, "initial");
  SetStatus("Backups automáticos ativos.");
}

void WatchLoop() {
  using namespace std::chrono;
  auto last_check = steady_clock::now() - seconds(10);
  uintmax_t last_size = 0;
  auto last_write = std::filesystem::file_time_type::min();
  auto changed_at = steady_clock::now();
  bool pending = false;
  while (g_running.load()) {
    std::this_thread::sleep_for(milliseconds(500));
    if (!pc::Get().save_backup) continue;
    const auto now = steady_clock::now();
    if (now - last_check < seconds(2)) continue;
    last_check = now;
    if (g_save_file.empty() || !std::filesystem::exists(g_save_file)) {
      g_save_file = FindSaveFile();
      if (g_save_file.empty()) continue;
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(g_save_file, ec);
    if (ec || size == 0) continue;
    const auto write = std::filesystem::last_write_time(g_save_file, ec);
    if (ec) continue;
    if (size != last_size || write != last_write) {
      last_size = size;
      last_write = write;
      changed_at = now;
      pending = true;
      continue;
    }
    // só copia quando o arquivo ficou estável por ~1,5 s
    if (!pending || now - changed_at < milliseconds(1500)) continue;
    std::vector<uint8_t> data;
    if (!ReadFile(g_save_file, data) || !LooksValid(data)) {
      pending = false;
      continue;
    }
    const std::string hash = Hex64(HashBytes(data));
    if (hash == g_last_hash) {
      pending = false;
      continue;
    }
    if (MakeBackup(g_save_file)) {
      g_last_hash = hash;
      SetStatus("Backup do save feito em " + Timestamp() + ".");
    }
    pending = false;
  }
}

}  // namespace

void Initialize(const std::filesystem::path& user_data_root) {
  if (g_running.load()) return;
  g_root = user_data_root;
  g_backup_dir = user_data_root / "backups";
  std::error_code ec;
  std::filesystem::create_directories(g_backup_dir, ec);
  StartupRecovery();
  g_running.store(true);
  g_thread = std::thread(WatchLoop);
}

void Shutdown() {
  g_running.store(false);
  if (g_thread.joinable()) g_thread.join();
}

bool RestoreLatest() {
  auto list = Backups();
  std::error_code ec;
  for (auto it = list.rbegin(); it != list.rend(); ++it) {
    std::vector<uint8_t> data;
    if (!ReadFile(*it, data) || !LooksValid(data)) continue;
    if (!g_save_file.empty() && std::filesystem::exists(g_save_file, ec)) {
      // preserva o que está lá antes de sobrescrever
      MakeBackup(g_save_file, "before-restore");
    }
    std::filesystem::create_directories(g_save_file.parent_path(), ec);
    std::filesystem::copy_file(*it, g_save_file, std::filesystem::copy_options::overwrite_existing,
                               ec);
    if (ec) continue;
    g_last_hash = Hex64(HashBytes(data));
    std::ofstream log(g_backup_dir / "backups.log", std::ios::app);
    log << Timestamp() << " restore " << it->filename().string() << "\n";
    return true;
  }
  return false;
}

std::string Status() {
  std::lock_guard lock(g_mutex);
  return g_status;
}

}  // namespace pc::saves
