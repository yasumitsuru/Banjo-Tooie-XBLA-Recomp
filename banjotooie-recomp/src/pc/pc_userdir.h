// Onde ficam os saves e as opções de PC de cada pessoa.
//
// Pedido de 2026-11-13: "Documentos\My Games\Banjo-Tooie XBLA", dentro dos Documentos do perfil:
// cada conta de usuário do Windows tem o seu próprio save. Antes o lançador do Craft e o seletor
// gravavam numa pasta compartilhada dentro do app (out\craft-runtime\user) e o padrão do runtime
// era Documentos\banjotooie.
#pragma once

#include <filesystem>
#include <string>

namespace pc::userdir {

// <Documentos da conta atual>\My Games\Banjo-Tooie XBLA. "Documentos" é a pasta conhecida do
// Windows (FOLDERID_Documents): acompanha o OneDrive ou outro lugar que a conta use. Vazio se o
// Windows não informar a pasta Documentos.
std::filesystem::path DefaultRoot();

// A pasta já tem save ou opções de PC.
bool HasData(const std::filesystem::path& root);

struct Migration {
  bool copied = false;          // copiou dados antigos para a pasta nova
  std::filesystem::path from;   // de onde copiou
  int files = 0;                // quantos arquivos
  std::string error;            // vazio = sem erro
};

// Primeira vez em cada conta: se 'dest' ainda não tem save nem opções de PC, COPIA (nunca apaga
// nem move) o save, as opções e os backups de onde as versões anteriores gravavam, escolhendo o
// save mais recente entre a pasta compartilhada do app (<exe>\..\craft-runtime\user) e
// Documentos\banjotooie. A pasta de origem ganha um aviso "MIGRADO-PARA-MY-GAMES.txt" e não é
// copiada de novo: outra conta do Windows começa com o próprio jogo, não com uma cópia deste.
Migration MigrateLegacy(const std::filesystem::path& exe_dir, const std::filesystem::path& dest);

}  // namespace pc::userdir
