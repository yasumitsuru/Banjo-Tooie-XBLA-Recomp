# banjotooie - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(banjotooie LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
# MISSAO 01: banjotooie_app.cpp (ganchos do host) e port_diagnostics.cpp (preflight, relatorio
# de boot, falhas, PNG) sao deste projeto; so instrumentacao, sem mudar o jogo.
# MISSAO 02: port_test_input.cpp = controle virtual so para testes (--port_test_input).
set(BANJOTOOIE_SOURCES
    src/main.cpp
    src/banjotooie_app.cpp
    src/port_diagnostics.cpp
    src/port_test_input.cpp
    # Camada de PC (do projeto W:\Decomp\Banjo da mesma pessoa): Configuracoes de PC, ganchos
    # de pc_hooks.toml (60 FPS, distancia de desenho, entrada no menu de pausa), automacao.
    src/pc/pc_settings.cpp
    src/pc/pc_hooks.cpp
    src/pc/pc_automation.cpp
    src/pc/pc_menu.cpp
    src/pc/pc_ingame_menu.cpp
    src/pc/pc_lang.cpp
    src/pc/pc_saves.cpp
    src/pc/pc_save_format.cpp
    src/pc/pc_progress.cpp
    src/pc/scripted_input.cpp
    src/pc/pc_crash.cpp
    src/pc/pc_shaders.cpp
    src/pc/pc_userdir.cpp
    # M04 (C-10): registro de sessão em <dados do usuário>\logs\sessao-*.txt
    src/pc/pc_log.cpp
    # M04 (C-29, C-25): monitor escolhido, resolução automática dele e sem bordas no monitor inteiro
    src/pc/pc_display.cpp
    # M04 (C-6): CONQUISTAS no PC (lista do jogo + obtidas guardadas por conta)
    src/pc/pc_achievements.cpp
)

if(WIN32)
    # banjotooie.rc embeds MAINICON (the exe + window-class icon) into the exe.
    enable_language(RC)
    add_executable(banjotooie WIN32 ${BANJOTOOIE_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/banjotooie.rc)
else()
    add_executable(banjotooie ${BANJOTOOIE_SOURCES})
endif()

rexglue_setup_target(banjotooie)

# M04 (C-10): identificação do build no registro de sessão (commit do app e do rexglue-sdk e se
# havia mudanças não gravadas). O configure roda a cada build (tools/build_native.cmd).
find_package(Git QUIET)
set(PC_GIT_COMMIT "desconhecido")
set(PC_SDK_COMMIT "desconhecido")
set(PC_GIT_STATE "desconhecido")
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.." OUTPUT_VARIABLE _pc_c
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _pc_r)
    if(_pc_r EQUAL 0)
        set(PC_GIT_COMMIT "${_pc_c}")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${REXSDK_DIR}" OUTPUT_VARIABLE _pc_s
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _pc_r)
    if(_pc_r EQUAL 0)
        set(PC_SDK_COMMIT "${_pc_s}")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
        --ignore-submodules=dirty
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.." OUTPUT_VARIABLE _pc_d
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _pc_r)
    if(_pc_r EQUAL 0)
        if(_pc_d STREQUAL "")
            set(PC_GIT_STATE "limpo")
        else()
            set(PC_GIT_STATE "com mudanças não gravadas")
        endif()
    endif()
endif()
set_source_files_properties(src/pc/pc_log.cpp PROPERTIES COMPILE_DEFINITIONS
    "PC_GIT_COMMIT=\"${PC_GIT_COMMIT}\";PC_SDK_COMMIT=\"${PC_SDK_COMMIT}\";PC_GIT_STATE=\"${PC_GIT_STATE}\"")

# --- Local build fix -------------------------------------------------------
# The SDK harness rex_app.cpp (injected into this target by
# rexglue_configure_target) includes imgui.h, but imgui's include dir isn't
# propagated through rex::runtime in the REXSDK_DIR (source-tree) build path.
# Scope the include to just these harness sources so the 48 generated TUs
# aren't invalidated / rebuilt.
set_source_files_properties(
    "${REXSDK_DIR}/src/ui/rex_app.cpp"
    "${REXSDK_DIR}/src/ui/windowed_app_main_win.cpp"
    src/banjotooie_app.cpp
    src/pc/pc_menu.cpp
    PROPERTIES INCLUDE_DIRECTORIES
        "${REXSDK_DIR}/thirdparty/imgui;${REXSDK_DIR}/src;${CMAKE_BINARY_DIR}/rexglue-sdk/include"
)

if(WIN32)
    # MISSAO 01: MiniDumpWriteDump (relatorio de falhas em port_diagnostics.cpp).
    target_link_libraries(banjotooie PRIVATE dbghelp)
    # Camada de PC: CommandLineToArgvW; timeBeginPeriod; placa/driver no relatorio de shaders.
    target_link_libraries(banjotooie PRIVATE shell32 winmm dxgi bcrypt)

    # Craft discovers packaged programs in app/Build. This entry point runs the
    # real native game, forwards checker arguments, and returns its exit code.
    add_executable(banjotooie_craft_launcher WIN32 ../tools/native_launcher.cpp)
    target_link_libraries(banjotooie_craft_launcher PRIVATE shell32)
    set_target_properties(banjotooie_craft_launcher PROPERTIES
        OUTPUT_NAME banjotooie
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../Build")

    # Seletor de versao (PT-BR ou global/ingles) para validacao visual (pedido de 2026-11-13).
    # Fica em app/Seletor, fora de app/Build, para nao mudar o programa que o Craft verifica.
    add_executable(banjotooie_seletor WIN32 ../tools/version_selector.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/banjotooie.rc)
    target_link_libraries(banjotooie_seletor PRIVATE shell32 gdi32 user32)
    set_target_properties(banjotooie_seletor PROPERTIES
        OUTPUT_NAME BanjoTooie_Seletor
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../Seletor")
endif()
