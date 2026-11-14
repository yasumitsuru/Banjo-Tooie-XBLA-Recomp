// Registro de sessão (M04, etapa C, item C-10; proposta do PLANO "Sistema de log completo").
//
// Um arquivo por sessão em <dados do usuário>\logs\sessao-AAAAMMDD-HHMMSS.txt (UTF-8), sempre
// ligado e independente das opções de linha de comando:
//  - cabeçalho: build (SHA-256 do exe, commit do app e do rexglue-sdk), Windows, CPU, memória,
//    GPU e driver, API gráfica, monitores, pasta do jogo, pré-checagem e opções de PC em vigor;
//  - tudo o que o runtime e a camada de PC registram (nível info; "detalhado" = debug);
//  - eventos do jogo: etapas do boot/desligamento, mudança de mapa e de cena, menus, saves,
//    viagens, opções gravadas, falhas;
//  - bloco de desempenho a cada 30 s (5 s no detalhado): quadros apresentados e atualizações
//    da lógica por segundo, tempo de quadro médio/p95/máximo, quadros acima de 33,4 ms, memória
//    do processo, CPU e memória de vídeo.
// Guarda as últimas kKeepSessions sessões e no máximo kMaxBytes por arquivo. O caminho da pasta
// pessoal (perfil do Windows) vira %USERPROFILE% no texto. Se o registro não puder ser criado,
// o jogo continua e o motivo vai para o relatório de boot.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace pc::log {

inline constexpr int kKeepSessions = 20;
inline constexpr uint64_t kMaxBytes = 16ull << 20;

struct StartInfo {
  std::filesystem::path user_data_root;
  std::filesystem::path game_data_root;
  std::string preflight;   // resumo da pré-checagem dos arquivos do jogo
  std::string user_data_source;
  bool automated = false;  // auto-teste/sonda (vai no cabeçalho)
};

// Cria o arquivo, escreve o cabeçalho e passa a receber o log do runtime. false = sem registro
// (Error() diz por quê).
bool Start(const StartInfo& info);
bool Active();
const std::filesystem::path& Path();
std::string Error();

// Evento do jogo/da camada de PC: "[sessao] <categoria>: <texto>" (também no log do runtime).
void Event(const char* category, const std::string& text);

// Thread do jogo, a cada quadro apresentado (gancho do intervalo de apresentação).
void NotePresent();

// Monitor do app (~10x/s): mudanças de mapa e de cena, bloco de desempenho periódico.
void Tick();

// Nível detalhado (debug) ligado/desligado na hora, pelo menu de PC.
void SetDetailed(bool on);

// Fim da sessão: último bloco de desempenho, motivo e fechamento do arquivo.
void Stop(const std::string& reason);

// Falha grave (tratador de exceções): escreve direto no arquivo, sem travas que possam estar
// presas na thread que caiu.
void Emergency(const char* text);

}  // namespace pc::log
