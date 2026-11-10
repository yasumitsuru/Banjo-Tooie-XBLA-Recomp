// Banjo-Tooie XBLA PT-BR Recompiled - diagnostico de inicializacao (MISSAO 01).
//
// Tudo aqui e instrumentacao do host: nao altera o codigo recompilado do jogo nem os dados.
//  - Preflight: confere a pasta do jogo (default.xex + RAWFiles) antes de abrir a janela e
//    explica em PT-BR o que falta, com codigo de saida proprio.
//  - Relatorio de boot (JSON): etapas com tempo, caminhos, preflight, quadros do jogo, FPS,
//    captura de tela e motivo da saida. Gravado a cada etapa, entao uma falha deixa a ultima.
//  - Falhas nao tratadas: registra codigo/endereco/modulo e grava um minidump.
//  - Contador de quadros: incrementado pelo gancho de apresentacao (ver main.cpp).
//  - PNG: gravador minimo (deflate sem compressao), sem dependencias.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace port {

// Codigos de saida do port (alem dos do proprio runtime).
enum ExitCode : int {
  kExitOk = 0,
  kExitMissingRequiredFiles = 2,
  kExitNoGameDataRoot = 3,
  kExitSmokeNoFrames = 4,
  kExitScreenshotFailed = 5,
};

struct PreflightFile {
  std::string rel;          // caminho relativo a pasta do jogo, como o jogo pede
  std::string role;         // obrigatorio | recomendado | opcional
  std::string found_at;     // onde foi achado (relativo), vazio se ausente
  uint64_t size = 0;
  std::string note;
};

struct PreflightResult {
  std::filesystem::path game_root;
  bool root_ok = false;
  bool xex_magic_ok = false;
  std::vector<PreflightFile> files;
  std::vector<std::string> missing_required;
  std::vector<std::string> missing_recommended;
  bool passed() const { return root_ok && xex_magic_ok && missing_required.empty(); }
  std::string SummaryPtBr() const;  // texto para a caixa de mensagem e para o log
};

PreflightResult RunPreflight(const std::filesystem::path& game_root);

// Relatorio de boot em JSON (seguro entre threads).
class BootReport {
 public:
  static BootReport& Get();
  void SetPath(const std::filesystem::path& path);
  const std::filesystem::path& path() const { return path_; }
  void SetInfo(const std::string& key, const std::string& value);
  void SetPreflight(const PreflightResult& result);
  void Stage(const std::string& name, const std::string& detail = {});
  void SetFrames(uint64_t frames, double fps, double elapsed_s);
  void SetScreenshot(const std::string& path, uint32_t width, uint32_t height, bool ok,
                     const std::string& detail);
  void SetCrash(const std::string& text);
  void SetExit(int code, const std::string& reason);
  void Flush();
  double ElapsedMs() const;

 private:
  BootReport();
  std::string ToJson() const;
  struct Impl;
  Impl* impl_;
  std::filesystem::path path_;
};

void InstallCrashHandler(const std::filesystem::path& dump_dir);

// Grava RGB(X) 8 bits como PNG. stride em bytes; bpp 4 (RGBX, X ignorado).
bool WritePngRgbx(const std::filesystem::path& path, const uint8_t* data, uint32_t width,
                  uint32_t height, size_t stride);

// Quadros apresentados pelo jogo (chamadas da funcao de apresentacao 0x822FA358).
std::atomic<uint64_t>& GuestPresentCount();

// Caixa de erro com titulo "Banjo-Tooie" (texto UTF-8).
void ShowErrorPtBr(const std::string& text);

std::string JsonEscape(const std::string& s);
std::string NowIso8601();

}  // namespace port
