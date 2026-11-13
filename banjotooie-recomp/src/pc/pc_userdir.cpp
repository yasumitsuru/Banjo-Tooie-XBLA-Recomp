// Pasta dos saves e das opções de PC de cada conta do Windows (pc_userdir.h).
#include "pc/pc_userdir.h"

#include <rex/filesystem.h>

#include <ctime>
#include <fstream>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace pc::userdir {
namespace {

constexpr const wchar_t* kMarker = L"MIGRADO-PARA-MY-GAMES.txt";

std::string Utf8(const fs::path& p) {
  const std::u8string s = p.u8string();
  return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

// savegame.dat mais novo dentro de 'root' (<perfil>\58410955\00000001\BanjoSave.bin\savegame.dat).
// Os backups se chamam save-*.dat e não contam.
bool NewestSave(const fs::path& root, fs::file_time_type* newest) {
  std::error_code ec;
  if (!fs::is_directory(root, ec)) return false;
  bool found = false;
  int guard = 0;
  fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
  for (; !ec && it != fs::recursive_directory_iterator() && guard < 20000; it.increment(ec), ++guard) {
    std::error_code e2;
    if (it->path().filename() != L"savegame.dat" || !it->is_regular_file(e2)) continue;
    const auto t = it->last_write_time(e2);
    if (e2) continue;
    if (!found || t > *newest) *newest = t;
    found = true;
  }
  return found;
}

std::string Now() {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_s(&tm, &t);
  char buf[32] = {};
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  return buf;
}

}  // namespace

fs::path DefaultRoot() {
  const fs::path docs = rex::filesystem::GetUserFolder();
  if (docs.empty()) return {};
  return docs / L"My Games" / L"Banjo-Tooie XBLA";
}

bool HasData(const fs::path& root) {
  std::error_code ec;
  if (fs::exists(root / L"pc_settings.toml", ec)) return true;
  fs::file_time_type t{};
  return NewestSave(root, &t);
}

Migration MigrateLegacy(const fs::path& exe_dir, const fs::path& dest) {
  Migration result;
  if (dest.empty() || HasData(dest)) return result;

  // Onde as versões anteriores gravavam: a pasta compartilhada do lançador do Craft e do seletor
  // (app\out\craft-runtime\user, o exe fica em app\out\native) e o padrão antigo do runtime.
  std::vector<fs::path> candidates = {exe_dir.parent_path() / L"craft-runtime" / L"user"};
  if (const fs::path docs = rex::filesystem::GetUserFolder(); !docs.empty())
    candidates.push_back(docs / L"banjotooie");

  std::error_code ec;
  fs::path best;
  fs::file_time_type best_time{};
  for (const auto& c : candidates) {
    if (fs::exists(c / kMarker, ec)) continue;  // já copiada (talvez por outra conta)
    if (fs::equivalent(c, dest, ec)) continue;
    fs::file_time_type t{};
    if (!NewestSave(c, &t)) continue;
    if (best.empty() || t > best_time) {
      best = c;
      best_time = t;
    }
  }
  if (best.empty()) return result;
  result.from = best;

  // Copia para uma pasta provisória e só então move para o lugar: uma cópia pela metade nunca
  // parece um save completo.
  const fs::path staging = dest / L"_migracao_em_andamento";
  fs::remove_all(staging, ec);  // sobra de uma tentativa interrompida (pasta só deste código)
  ec.clear();
  fs::create_directories(staging, ec);
  int files = 0;
  if (!ec) {
    fs::recursive_directory_iterator it(best, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      const fs::path rel = fs::relative(it->path(), best, ec);
      if (ec) break;
      // crashes\ são relatórios de falhas das versões antigas, não dados da pessoa.
      if (!rel.empty() && *rel.begin() == L"crashes") {
        if (it->is_directory(ec)) it.disable_recursion_pending();
        ec.clear();
        continue;
      }
      if (rel.filename() == kMarker) continue;
      const fs::path to = staging / rel;
      if (it->is_directory(ec)) {
        fs::create_directories(to, ec);
      } else if (!ec && it->is_regular_file(ec)) {
        fs::create_directories(to.parent_path(), ec);
        if (!ec) fs::copy_file(it->path(), to, fs::copy_options::overwrite_existing, ec);
        if (!ec) ++files;
      }
      if (ec) break;
    }
  }
  if (!ec) {
    for (fs::directory_iterator it(staging, ec); !ec && it != fs::directory_iterator();
         it.increment(ec)) {
      fs::rename(it->path(), dest / it->path().filename(), ec);
      if (ec) break;
    }
  }
  if (ec) {
    result.error = ec.message();
    std::error_code ignore;
    fs::remove_all(staging, ignore);
    return result;
  }
  fs::remove(staging, ec);

  std::ofstream marker(best / kMarker, std::ios::binary);
  if (marker) {
    marker << "Os saves e as opções desta pasta foram COPIADOS em " << Now() << " para\r\n"
           << Utf8(dest) << "\r\n\r\n"
           << "Desde então o jogo usa a pasta acima (Documentos\\My Games\\Banjo-Tooie XBLA de cada "
              "conta do Windows).\r\nEsta pasta não é mais usada e fica como cópia de segurança; "
              "por causa deste aviso ela não é copiada de novo para outra conta.\r\n";
  }
  result.copied = true;
  result.files = files;
  return result;
}

}  // namespace pc::userdir
