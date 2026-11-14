// CONQUISTAS no PC (M04, item C-6).
//
// No Xbox 360, a entrada CONQUISTAS (ação 0x822DD628 do jogo) só chama XamShowAchievementsUI, o
// painel do Xbox LIVE, que não existe no PC: a entrada não fazia nada. Agora:
//  - quando o jogo registra uma conquista (XGIUserWriteAchievements), o port a guarda em
//    <dados do usuário>\conquistas.txt (id e data) e no registro de sessão;
//  - a entrada CONQUISTAS abre, dentro do menu do próprio jogo, a lista das conquistas do jogo
//    (nomes e descrições da tabela XDBF do default.xex, no idioma do jogo: português na versão
//    PT-BR), com quais já foram obtidas neste PC.
// O resto do menu fica igual. Conquistas obtidas antes desta versão (ou em outro aparelho) não
// aparecem: o jogo só as informa no momento em que acontecem.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pc::achievements {

struct Item {
  uint16_t id = 0;
  std::string name;         // UTF-8, como no jogo
  std::string description;  // UTF-8
  uint16_t points = 0;
  bool unlocked = false;
  std::string when;         // "AAAA-MM-DD HH:MM" quando obtida
};

// Pasta da conta (chamado na abertura) e o aviso do runtime.
void Install(const std::filesystem::path& user_data_root);

// Lista do jogo (vazia se o jogo ainda não carregou a tabela). Thread-safe.
std::vector<Item> List();

// Para testes: marca como obtida como se o jogo tivesse informado (sonda --probe-conquistas).
void SimulateUnlock(uint32_t id);

const std::filesystem::path& File();

}  // namespace pc::achievements
