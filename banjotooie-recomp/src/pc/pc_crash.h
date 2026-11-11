// Registro de falhas do port PT-BR.
//
// Quando o processo cai (exceção não tratada), grava um relatório em
// <dados do usuário>\crashes\crash-AAAAMMDD-HHMMSS.txt com o tipo da falha, o endereço e a
// pilha do host, cada quadro traduzido para a função original do jogo (sub_8xxxxxxx) quando
// ele está no código recompilado. Não precisa de símbolos de depuração: o mapa vem da tabela
// de funções do próprio runtime. Ideia inspirada no crash_trace do MC_LA_Recompiled.
#pragma once

#include <cstdint>
#include <filesystem>

namespace rex::runtime { class FunctionDispatcher; }

namespace pc::crash {

// Monta o mapa host→jogo e instala o filtro de exceções não tratadas.
// 'dispatcher' é o FunctionDispatcher do runtime; 'folder' recebe os relatórios.
void Install(rex::runtime::FunctionDispatcher* dispatcher, const std::filesystem::path& folder);

// Função do jogo (endereço PowerPC) que contém um endereço do código recompilado, ou 0.
uint32_t GuestFunctionFor(uintptr_t host_pc);

}  // namespace pc::crash
