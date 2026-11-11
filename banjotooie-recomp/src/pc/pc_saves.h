// Backups automáticos e recuperação dos saves do jogo (proteção contra corrupção).
//
// O jogo grava em <dados do usuário>/<perfil>/58410955/00000001/BanjoSave.bin/savegame.dat.
// Este módulo vigia esse arquivo, guarda cópias verificadas em <dados do usuário>/backups
// e, ao iniciar, restaura a cópia mais nova se o save estiver faltando ou inválido.
#pragma once

#include <filesystem>
#include <string>

namespace pc::saves {

void Initialize(const std::filesystem::path& user_data_root);
void Shutdown();

// Restaura a cópia mais recente que passa na verificação (o save atual é preservado
// antes, como backup "corrupt-..."). Devolve true se restaurou.
bool RestoreLatest();

// Texto para a tela de Configurações de PC.
std::string Status();

}  // namespace pc::saves
