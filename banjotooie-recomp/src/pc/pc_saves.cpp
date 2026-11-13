// Backups e recuperação dos saves (ver pc_saves.h). Regras do save: pc_save_format.h.
#include "pc_saves.h"

#include <rex/logging.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include "pc_save_format.h"
#include "pc_settings.h"

namespace fs = std::filesystem;

namespace pc::saves {
namespace {

constexpr int kMaxCorruptCopies = 20;  // backups/corrupt-*.dat (arquivos recusados, como estavam)
constexpr int kMaxKeepCopies = 20;     // backups/keep-*.dat (antes de um jogo sumir)
constexpr uint64_t kMaxReadBytes = 1u << 20;
// Perfil fixo do runtime: onde o save fica quando ainda não existe nenhum.
constexpr const wchar_t* kDefaultSaveRel =
    L"B13EBABEBABEBABE\\58410955\\00000001\\BanjoSave.bin\\savegame.dat";

std::atomic<bool> g_running{false};
std::thread g_thread;
std::mutex g_status_mu;
std::recursive_mutex g_io;  // vigia x restauração pedida no menu
fs::path g_root;
fs::path g_save_file;
fs::path g_backup_dir;
std::string g_status = "Backups ainda não iniciados.";
std::string g_last_hash;      // último conteúdo do save já guardado (ou restaurado)
std::string g_rejected_hash;  // última gravação recusada (avisada uma vez só)
StartupResult g_startup;

void SetStatus(const std::string& s) {
  std::lock_guard lock(g_status_mu);
  g_status = s;
}

std::string Timestamp() {
  const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
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

std::string HashOf(const std::vector<uint8_t>& data) { return Hex64(HashBytes(data)); }

std::string Utf8(const fs::path& p) {
  const std::u8string s = p.u8string();
  return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

// Uma linha em backups/backups.log e no log do runtime.
void Log(const std::string& line) {
  std::error_code ec;
  fs::create_directories(g_backup_dir, ec);
  std::ofstream log(g_backup_dir / "backups.log", std::ios::app | std::ios::binary);
  log << Timestamp() << " " << line << "\n";
  REXLOG_INFO("saves: {}", line);
}

enum class Read { Ok, Missing, Locked, Error };

// Lê o arquivo inteiro deixando outros programas lerem e escreverem nele. Se ele estiver preso
// (outro programa com acesso exclusivo, como o OneDrive sincronizando), tenta por ~2 s.
Read ReadAll(const fs::path& path, std::vector<uint8_t>& out) {
  for (int attempt = 0; attempt < 10; ++attempt) {
    if (attempt) Sleep(200);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      const DWORD e = GetLastError();
      if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return Read::Missing;
      if (e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION) continue;
      return Read::Error;
    }
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && uint64_t(size.QuadPart) <= kMaxReadBytes;
    std::vector<uint8_t> data;
    DWORD read_error = 0;
    if (ok) {
      data.resize(size_t(size.QuadPart));
      size_t done = 0;
      while (ok && done < data.size()) {
        DWORD got = 0;
        ok = ReadFile(h, data.data() + done, DWORD(data.size() - done), &got, nullptr) && got > 0;
        if (!ok) read_error = GetLastError();
        done += got;
      }
    }
    CloseHandle(h);
    if (ok) {
      out = std::move(data);
      return Read::Ok;
    }
    if (read_error == ERROR_LOCK_VIOLATION) continue;  // trecho preso: tenta de novo
    return Read::Error;
  }
  return Read::Locked;
}

// Escreve tudo num temporário ao lado e troca pelo destino (o destino nunca fica pela metade).
// Se a troca não for possível (destino aberto por outro programa sem permitir a troca), escreve
// no próprio arquivo. Confere o resultado relendo.
bool WriteAtomic(const fs::path& dest, const std::vector<uint8_t>& data) {
  std::error_code ec;
  fs::create_directories(dest.parent_path(), ec);
  fs::path tmp = dest;
  tmp += L".port-tmp";
  auto write_to = [&](const fs::path& p, DWORD disposition) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    bool ok = WriteFile(h, data.data(), DWORD(data.size()), &put, nullptr) && put == data.size();
    ok = ok && SetEndOfFile(h) && FlushFileBuffers(h);
    CloseHandle(h);
    return ok;
  };
  bool ok = write_to(tmp, CREATE_ALWAYS) &&
            MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
  if (!ok) {
    DeleteFileW(tmp.c_str());
    ok = write_to(dest, OPEN_ALWAYS);
  }
  std::vector<uint8_t> check;
  return ok && ReadAll(dest, check) == Read::Ok && check == data;
}

fs::path FindSaveFile() {
  std::error_code ec;
  if (!fs::exists(g_root, ec)) return {};
  for (auto it = fs::recursive_directory_iterator(g_root, fs::directory_options::skip_permission_denied, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (it.depth() > 6 || it->path().filename() == L"backups") {
      it.disable_recursion_pending();
      continue;
    }
    if (it->is_regular_file(ec) && it->path().filename() == L"savegame.dat") return it->path();
  }
  return {};
}

// Arquivos de backup com o prefixo dado, do mais antigo para o mais novo (o nome tem a data).
std::vector<fs::path> Files(const wchar_t* prefix) {
  std::vector<fs::path> list;
  std::error_code ec;
  if (!fs::exists(g_backup_dir, ec)) return list;
  for (const auto& e : fs::directory_iterator(g_backup_dir, ec)) {
    if (!e.is_regular_file(ec)) continue;
    const auto name = e.path().filename().wstring();
    if (name.rfind(prefix, 0) == 0 && e.path().extension() == L".dat") list.push_back(e.path());
  }
  std::sort(list.begin(), list.end());
  return list;
}

// Apaga os mais antigos de uma lista até sobrar 'keep'.
void Trim(std::vector<fs::path> list, int keep) {
  std::error_code ec;
  while (int(list.size()) > keep) {
    fs::remove(list.front(), ec);
    list.erase(list.begin());
  }
}

std::string NameStamp(const std::string& name) {
  // save-AAAAMMDD-HHMMSS-hash.dat -> "DD.MM HH:MM"
  const size_t d = name.find('-');
  if (d == std::string::npos || name.size() < d + 16) return name;
  const std::string date = name.substr(d + 1, 8), time = name.substr(d + 10, 6);
  return date.substr(6, 2) + "." + date.substr(4, 2) + " " + time.substr(0, 2) + ":" +
         time.substr(2, 2);
}

// Sufixo de data de um nome (para ordenar save-* e keep-* juntos).
std::string NameDate(const std::string& name) {
  const size_t d = name.find('-');
  return d == std::string::npos ? name : name.substr(d + 1);
}

// Guarda um arquivo recusado exatamente como estava, para diagnóstico. Devolve o caminho.
fs::path PreserveCorrupt(const std::vector<uint8_t>& data, const std::string& reason) {
  const fs::path dest = g_backup_dir / ("corrupt-" + Timestamp() + "-" + HashOf(data) + ".dat");
  if (!WriteAtomic(dest, data)) {
    Log("ERRO ao guardar a copia do save recusado (" + reason + ")");
    return {};
  }
  Log("save recusado guardado como " + Utf8(dest.filename()) + " (" + reason + ", " +
      std::to_string(data.size()) + " bytes)");
  Trim(Files(L"corrupt-"), kMaxCorruptCopies);
  return dest;
}

// Último backup save-* válido (para comparar quantos jogos havia).
bool LatestValidSave(std::vector<uint8_t>& out, fs::path* path) {
  auto list = Files(L"save-");
  for (auto it = list.rbegin(); it != list.rend(); ++it) {
    std::vector<uint8_t> d;
    if (ReadAll(*it, d) == Read::Ok && savefmt::Inspect(d).usable) {
      out = std::move(d);
      if (path) *path = *it;
      return true;
    }
  }
  return false;
}

// Guarda uma gravação válida. Mantém os 'backup_count' save-* VÁLIDOS mais novos; arquivos
// inválidos (de versões antigas do port) não contam e saem junto com os antigos.
bool MakeBackup(const std::vector<uint8_t>& data, const std::string& tag) {
  const savefmt::Report rep = savefmt::Inspect(data);
  if (!rep.usable) return false;
  std::error_code ec;
  fs::create_directories(g_backup_dir, ec);
  // Um jogo sumiu desde o último backup (apagado no jogo ou perdido): o anterior fica guardado
  // fora do rodízio, para não sair junto com os antigos.
  std::vector<uint8_t> prev;
  fs::path prev_path;
  if (LatestValidSave(prev, &prev_path) &&
      savefmt::Inspect(prev).ValidSlotCount() > rep.ValidSlotCount()) {
    const std::string prev_hash = HashOf(prev);
    bool already = false;
    for (const auto& k : Files(L"keep-"))
      already = already || k.filename().string().find(prev_hash) != std::string::npos;
    if (!already) {
      const fs::path keep = g_backup_dir / ("keep-" + NameDate(Utf8(prev_path.filename())));
      if (WriteAtomic(keep, prev)) {
        Log("menos jogos gravados que no backup anterior (" +
            std::to_string(savefmt::Inspect(prev).ValidSlotCount()) + " -> " +
            std::to_string(rep.ValidSlotCount()) + "): anterior guardado como " +
            Utf8(keep.filename()));
        Trim(Files(L"keep-"), kMaxKeepCopies);
      }
    }
  }
  const std::string hash = HashOf(data);
  const fs::path dest = g_backup_dir / ("save-" + Timestamp() + "-" + hash + ".dat");
  if (!WriteAtomic(dest, data)) {
    Log("ERRO ao gravar o backup " + Utf8(dest.filename()));
    return false;
  }
  Log("backup " + tag + " " + hash + " (" + std::to_string(rep.ValidSlotCount()) + " jogo(s))");
  // Rodízio: do mais novo para o mais antigo, conta só os válidos; depois do N-ésimo válido,
  // tudo o que é mais antigo sai.
  const int keep = std::clamp(pc::Get().backup_count, 1, 50);
  auto list = Files(L"save-");
  int valid = 0;
  for (auto it = list.rbegin(); it != list.rend(); ++it) {
    if (valid >= keep) {
      fs::remove(*it, ec);
      continue;
    }
    std::vector<uint8_t> d;
    if (ReadAll(*it, d) == Read::Ok && savefmt::Inspect(d).usable) ++valid;
  }
  return true;
}

// Troca o save pelo conteúdo dado. O save atual é guardado antes: como backup, se válido, ou
// como corrupt-*, se não (a menos que já tenha sido guardado).
bool RestoreData(const std::vector<uint8_t>& data, const std::string& source, bool keep_current) {
  std::lock_guard lock(g_io);
  if (g_save_file.empty()) g_save_file = FindSaveFile();
  if (g_save_file.empty()) g_save_file = g_root / kDefaultSaveRel;
  std::vector<uint8_t> current;
  const Read rr = ReadAll(g_save_file, current);
  if (rr == Read::Locked) {
    Log("restauracao de " + source + " cancelada: o save esta preso por outro programa");
    return false;
  }
  if (keep_current && rr == Read::Ok && HashOf(current) != HashOf(data)) {
    const savefmt::Report cur = savefmt::Inspect(current);
    if (cur.usable)
      MakeBackup(current, "before-restore");
    else
      PreserveCorrupt(current, cur.reason);
  }
  if (!WriteAtomic(g_save_file, data)) {
    Log("ERRO ao restaurar " + source);
    return false;
  }
  g_last_hash = HashOf(data);
  Log("restore " + source + " " + g_last_hash);
  return true;
}

void StartupRecovery() {
  g_startup = {};
  g_save_file = FindSaveFile();
  g_startup.save_file = g_save_file;
  if (g_root.wstring().find(L"OneDrive") != std::wstring::npos)
    Log("pasta dos saves sincronizada pelo OneDrive: " + Utf8(g_root));
  if (g_save_file.empty()) {
    g_startup.action = "sem_save";
    SetStatus("Save do jogo ainda não existe; os backups começam quando ele aparecer.");
    return;
  }
  // Cópias de conflito de sincronização (ex.: "savegame-PC.dat") não são lidas pelo jogo.
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(g_save_file.parent_path(), ec)) {
    const auto n = e.path().filename().wstring();
    if (n != L"savegame.dat" && n.rfind(L"savegame", 0) == 0 &&
        e.path().extension() != L".port-prev" && e.path().extension() != L".port-tmp")
      Log("arquivo extra ao lado do save (conflito de sincronizacao?): " + Utf8(e.path().filename()));
  }
  std::vector<uint8_t> data;
  const Read rr = ReadAll(g_save_file, data);
  if (rr == Read::Locked) {
    g_startup.action = "bloqueado";
    Log("save preso por outro programa na abertura: nada foi restaurado nem apagado");
    SetStatus("O save estava em uso por outro programa (ex.: OneDrive) na abertura: nada foi "
              "restaurado nem apagado.");
    return;
  }
  if (rr != Read::Ok) {
    g_startup.action = "erro";
    Log("save ilegivel na abertura (erro do Windows); nada foi mudado");
    SetStatus("Não foi possível ler o save na abertura; nada foi mudado.");
    return;
  }
  savefmt::Report rep = savefmt::Inspect(data);
  const auto backups = ListBackups();
  // Gravação interrompida: o runtime guarda o conteúdo anterior em savegame.dat.port-prev
  // durante cada gravação do jogo e apaga ao fechar o arquivo (host_path_file.cpp). Se ele
  // sobrou, a última gravação não terminou: o save atual pode estar pela metade.
  fs::path prev_path = g_save_file;
  prev_path += L".port-prev";
  std::vector<uint8_t> prev;
  const bool interrupted = fs::exists(prev_path, ec) && ReadAll(prev_path, prev) == Read::Ok;
  const bool prev_ok = interrupted && savefmt::Inspect(prev).usable;
  if (interrupted) {
    g_startup.interrupted = true;
    Log(std::string("a ultima gravacao do jogo foi interrompida (sobrou savegame.dat.port-prev, ") +
        (prev_ok ? "valido" : "invalido") + ")");
  }
  auto drop_prev = [&] {
    std::error_code e2;
    fs::remove(prev_path, e2);
  };
  // Um save todo zerado com backups que têm jogos não é um save novo: é uma gravação perdida.
  if (rep.usable && rep.blank && !backups.empty() && backups.front().games > 0) {
    rep.usable = false;
    rep.reason = "save todo zerado, mas ha backups com jogos gravados";
  }
  if (rep.usable) {
    g_last_hash = HashOf(data);
    g_startup.action = rep.blank ? "em_branco" : "ok";
    if (!rep.blank && backups.empty()) MakeBackup(data, "initial");
    // O save atual passou, mas a gravação não terminou: o estado anterior (completo) fica
    // guardado fora do rodízio, caso a gravação tenha misturado os dois.
    if (prev_ok && HashOf(prev) != g_last_hash) {
      const fs::path keep = g_backup_dir / ("keep-" + Timestamp() + "-" + HashOf(prev) + ".dat");
      if (WriteAtomic(keep, prev))
        Log("estado anterior a gravacao interrompida guardado como " + Utf8(keep.filename()));
    }
    if (interrupted) drop_prev();
    SetStatus(interrupted ? "A última gravação do jogo foi interrompida; o save passou na "
                            "verificação e o estado anterior ficou guardado em backups."
                          : "Backups automáticos ativos.");
    return;
  }
  g_startup.reason = rep.reason;
  g_startup.corrupt_copy = PreserveCorrupt(data, rep.reason);
  // O estado completo de antes da gravação interrompida é o mais recente que existe.
  if (prev_ok && RestoreData(prev, "savegame.dat.port-prev", /*keep_current=*/false)) {
    drop_prev();
    g_startup.action = "restaurado";
    g_startup.restored_from = prev_path;
    SetStatus("A última gravação do jogo foi interrompida (" + rep.reason + "); cópia guardada em "
              "backups e restaurado o estado de antes dela.");
    return;
  }
  for (const auto& b : backups) {
    std::vector<uint8_t> good;
    if (ReadAll(b.path, good) != Read::Ok || !savefmt::Inspect(good).usable) continue;
    if (RestoreData(good, Utf8(b.path.filename()), /*keep_current=*/false)) {
      g_startup.action = "restaurado";
      g_startup.restored_from = b.path;
      SetStatus("Save inválido na abertura (" + rep.reason + "): cópia guardada em backups; "
                "restaurado o backup de " + b.stamp + ".");
      return;
    }
  }
  g_startup.action = "invalido_sem_backup";
  SetStatus("Save inválido na abertura (" + rep.reason + ") e nenhum backup válido; cópia "
            "guardada em backups.");
}

void WatchLoop() {
  using namespace std::chrono;
  auto last_check = steady_clock::now() - seconds(10);
  uintmax_t last_size = 0;
  auto last_write = fs::file_time_type::min();
  auto changed_at = steady_clock::now();
  bool pending = false;
  while (g_running.load()) {
    std::this_thread::sleep_for(milliseconds(500));
    if (!pc::Get().save_backup) continue;
    const auto now = steady_clock::now();
    if (now - last_check < seconds(2)) continue;
    last_check = now;
    std::lock_guard lock(g_io);
    std::error_code ec;
    if (g_save_file.empty() || !fs::exists(g_save_file, ec)) {
      g_save_file = FindSaveFile();
      if (g_save_file.empty()) continue;
    }
    const auto size = fs::file_size(g_save_file, ec);
    if (ec || size == 0) continue;
    const auto write = fs::last_write_time(g_save_file, ec);
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
    if (ReadAll(g_save_file, data) != Read::Ok) continue;  // preso: tenta no próximo ciclo
    pending = false;
    const std::string hash = HashOf(data);
    if (hash == g_last_hash) continue;
    const savefmt::Report rep = savefmt::Inspect(data);
    if (!rep.usable) {
      if (hash != g_rejected_hash) {
        g_rejected_hash = hash;
        PreserveCorrupt(data, "gravacao do jogo recusada: " + rep.reason);
        SetStatus("Uma gravação do jogo não passou na verificação (" + rep.reason +
                  "); cópia guardada em backups.");
      }
      continue;
    }
    if (MakeBackup(data, "save")) {
      g_last_hash = hash;
      SetStatus("Backup do save feito em " + NameStamp("save-" + Timestamp()) + ".");
    }
  }
}

}  // namespace

void Initialize(const fs::path& user_data_root) {
  if (g_running.load()) return;
  g_root = user_data_root;
  g_backup_dir = user_data_root / "backups";
  std::error_code ec;
  fs::create_directories(g_backup_dir, ec);
  {
    std::lock_guard lock(g_io);
    StartupRecovery();
  }
  g_running.store(true);
  g_thread = std::thread(WatchLoop);
}

void Shutdown() {
  g_running.store(false);
  if (g_thread.joinable()) g_thread.join();
}

std::vector<BackupInfo> ListBackups() {
  std::vector<BackupInfo> out;
  std::vector<fs::path> all = Files(L"save-");
  for (const auto& k : Files(L"keep-")) all.push_back(k);
  for (const auto& p : all) {
    std::vector<uint8_t> d;
    if (ReadAll(p, d) != Read::Ok) continue;
    const savefmt::Report rep = savefmt::Inspect(d);
    if (!rep.usable) continue;
    BackupInfo b;
    b.path = p;
    b.name = Utf8(p.filename());
    b.kind = b.name.rfind("keep-", 0) == 0 ? "keep" : "save";
    b.stamp = NameStamp(b.name);
    b.games = rep.ValidSlotCount();
    out.push_back(std::move(b));
  }
  std::sort(out.begin(), out.end(), [](const BackupInfo& a, const BackupInfo& b) {
    return NameDate(a.name) > NameDate(b.name);
  });
  return out;
}

bool RestoreBackup(const fs::path& backup) {
  std::vector<uint8_t> data;
  if (ReadAll(backup, data) != Read::Ok || !savefmt::Inspect(data).usable) return false;
  return RestoreData(data, Utf8(backup.filename()), /*keep_current=*/true);
}

bool RestoreLatest() {
  for (const auto& b : ListBackups())
    if (RestoreBackup(b.path)) return true;
  return false;
}

StartupResult LastStartup() {
  std::lock_guard lock(g_io);
  return g_startup;
}

std::string Status() {
  std::lock_guard lock(g_status_mu);
  return g_status;
}

fs::path SaveFile() {
  std::lock_guard lock(g_io);
  return g_save_file;
}

}  // namespace pc::saves
