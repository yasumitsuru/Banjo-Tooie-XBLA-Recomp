// Preparação de shaders da placa de vídeo (inspirado no shader_prepare do MC_LA_Recompiled).
//
// O runtime guarda cada shader e pipeline que o jogo cria em <cache>\shaders\shareable e, a
// cada início, cria de novo todos eles antes do código do jogo rodar (o driver compila agora,
// não no meio do jogo). Este módulo acrescenta:
//
// - Pacote de shaders: se ao lado do executável houver a pasta "shaderpack" com os arquivos
//   58410955.* gerados nas sessões de teste deste port, os que faltam no cache do jogador são
//   copiados antes do carregamento. Nunca substitui um arquivo que o jogador já tem.
// - --prepare-shaders <relatorio.json>: inicia o runtime, cria todos os pipelines guardados
//   (o driver compila e grava o próprio cache), grava o relatório e sai antes do código do jogo
//   rodar. O launcher usa isso depois de instalar e quando o driver ou a placa mudam.
//
// O pacote não tem dados do jogo além do que o próprio cache guarda (descrições de pipeline e
// shaders traduzidos da cópia do jogador) e só é usado no computador do próprio jogador.
#pragma once

#include <filesystem>
#include <string>

namespace pc::shaders {

// Caminho pedido por --prepare-shaders, ou vazio.
std::filesystem::path RequestedReport();

// Copia para 'cache_root'\shaders\shareable os arquivos do pacote que faltam.
// Retorna quantos foram copiados.
int InstallPack(const std::filesystem::path& exe_dir, const std::filesystem::path& cache_root);

// Grava o relatório da preparação (placa, driver, tamanhos, tempo) e encerra o processo.
[[noreturn]] void FinishPreparation(const std::filesystem::path& report,
                                    const std::filesystem::path& cache_root, int pack_copied);

}  // namespace pc::shaders
