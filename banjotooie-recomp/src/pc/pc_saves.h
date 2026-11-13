// Backups automáticos e recuperação dos saves do jogo (proteção contra corrupção).
//
// O jogo grava em <dados do usuário>/<perfil>/58410955/00000001/BanjoSave.bin/savegame.dat.
// Este módulo (M04, etapa B, 2026-11-13):
//  - na ABERTURA, antes de o jogo começar, confere o save com as regras do próprio jogo
//    (pc_save_format.h). Se o jogo fosse descartar alguma coisa, guarda o arquivo como está em
//    backups/corrupt-*.dat e restaura o backup válido mais novo. Se o arquivo estiver preso por
//    outro programa (ex.: OneDrive sincronizando), não mexe em nada;
//  - durante o jogo, copia cada gravação nova e válida para backups/save-*.dat (fica com os N
//    válidos mais novos; arquivos inválidos não contam e nunca empurram os bons para fora);
//  - quando uma gravação tem menos jogos que a anterior, guarda a anterior em backups/keep-*.dat
//    (fora do rodízio);
//  - restaura o mais novo ou um backup escolhido, guardando antes o save atual. Toda escrita no
//    save é atômica (arquivo temporário + troca) e conferida depois.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace pc::saves {

// Confere o save (e restaura, se preciso) e inicia a vigia. Chamar ANTES de o jogo começar.
void Initialize(const std::filesystem::path& user_data_root);
void Shutdown();

struct BackupInfo {
  std::filesystem::path path;
  std::string name;   // nome do arquivo
  std::string kind;   // "save" (rodízio) ou "keep" (guardado antes de um jogo sumir)
  std::string stamp;  // "13.11 09:21" (dia.mês hora:min), para a tela do jogo
  int games = 0;      // blocos de jogo válidos no backup
};
// Backups válidos que dá para restaurar, do mais novo para o mais antigo.
std::vector<BackupInfo> ListBackups();

// Restaura o backup válido mais novo (o save atual é guardado antes). Devolve true se restaurou.
bool RestoreLatest();
// Restaura um backup escolhido (o save atual é guardado antes). Devolve true se restaurou.
bool RestoreBackup(const std::filesystem::path& backup);

// O que a conferência da abertura fez.
struct StartupResult {
  std::string action;  // sem_save, ok, em_branco, restaurado, invalido_sem_backup, bloqueado, erro
  std::string reason;  // por que o save foi recusado (vazio quando estava bom)
  bool interrupted = false;  // sobrou savegame.dat.port-prev: a última gravação não terminou
  std::filesystem::path save_file, corrupt_copy, restored_from;
};
StartupResult LastStartup();

// Texto para a tela de Configurações de PC.
std::string Status();
std::filesystem::path SaveFile();

}  // namespace pc::saves
