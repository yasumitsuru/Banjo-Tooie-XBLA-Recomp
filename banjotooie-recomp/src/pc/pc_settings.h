// Configurações de PC do port PT-BR (arquivo próprio, separado dos saves do jogo).
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

namespace pc {

enum class DrawDistance : int { Original = 0, Longe = 1, Total = 2 };
enum class DisplayMode : int { Janela = 0, SemBordas = 1, TelaCheia = 2 };
enum class Resolution : int { R1080p = 0, R1440p = 1, R2160p = 2 };

struct Settings {
  // Jogo / tempo
  bool fps60 = true;                     // 60 FPS (passo de quadros 1); false = 30 FPS original
  bool lock60 = true;                    // ritmo da tela travado em 60 (cenas a 30 repetem cada quadro)
  bool scenes60 = false;                 // lógica das cenas também a 60 (experimental: cenas
                                         // cronometradas por quadros ficam mais rápidas)
                                         // (false = cenas a 30, como o patch Xenia)
  DrawDistance draw_distance = DrawDistance::Original;
  bool pause_on_focus_loss = true;       // pausa (Start) ao minimizar/perder o foco
  bool skip_intro_logos = false;         // "Início rápido": logos opcionais até o título
  bool show_fps = false;
  bool show_perf_stats = false;
  bool show_collectibles = false;
  bool pc_menu_entry = true;  // "CLASSIFICAÇÕES" (só Xbox Live) vira "CONFIGURAÇÕES DE PC" nos menus do jogo
  // Diálogos: 0 = Original (1x), 1 = Rápida (2x), 2 = Muito rápida (4x), 3 = Instantânea.
  // Multiplica o tempo que a caixa de texto acumula por letra (gancho 0x8225D8A8).
  int dialog_speed = 0;
  // Vídeo (aplicados ao runtime; resolução exige reiniciar)
  Resolution resolution = Resolution::R1440p;
  bool resolution_auto = true;  // escolhe pela resolução do monitor na primeira execução
  DisplayMode display_mode = DisplayMode::Janela;
  // Câmera (mouse/teclado)
  bool mouse_look = false;
  float cam_sens_x = 1.0f;
  float cam_sens_y = 1.0f;
  bool cam_invert_x = false;
  bool cam_invert_y = false;
  float cam_smoothing = 0.25f;  // 0 = direto, 0.9 = bem suave
  std::string cam_key_up = "I";
  std::string cam_key_down = "K";
  std::string cam_key_left = "J";
  std::string cam_key_right = "L";
  // Controle (gamepad)
  float pad_deadzone = 0.12f;
  float pad_sens_x = 1.0f;
  float pad_sens_y = 1.0f;
  bool pad_invert_x = false;
  bool pad_invert_y = false;
  bool pad_vibration = true;
  // Áudio (0..100)
  int volume_master = 100;
  // Saves
  bool save_backup = true;
  int backup_count = 10;
};

// Estado global lido pelos ganchos (thread do jogo) e escrito pelo menu (thread da UI).
Settings& Get();

// Caminho do arquivo e persistência.
void SetPath(const std::filesystem::path& file);
const std::filesystem::path& Path();
bool Load();   // false se não existia (mantém os padrões)
bool Save();   // escrita atômica (arquivo .tmp + rename) com cópia .bak do anterior

const char* DrawDistanceName(DrawDistance d);
const char* DisplayModeName(DisplayMode d);
const char* ResolutionName(Resolution r);
const char* DialogSpeedName(int speed);
float DialogSpeedFactor(int speed);  // 1, 2, 4 ou 1000 (página inteira num quadro)

// Aplica as opções que vivem em cvars do runtime (gamepad e volume geral).
void ApplyRuntimeOptions();

}  // namespace pc
