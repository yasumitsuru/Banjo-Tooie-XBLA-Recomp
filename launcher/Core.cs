// Banjo-Tooie XBLA PT-BR Recompiled — launcher portátil: núcleo (sem interface).
//
// Adaptado do instalador/configurador do MC_LA_Recompiled (Roberto Araujo,
// https://github.com/robertoaraujo77/MC_LA_Recompiled, GPL-3.0-or-later) e da primeira adaptação
// para o Banjo-Tooie feita no projeto irmão W:\Decomp\Banjo. Este launcher também é
// GPL-3.0-or-later (LICENSE-GPL-3.0.txt). C# 5, compilado com o csc.exe do .NET Framework 4 que
// acompanha o Windows: nada a instalar.
//
// Diferenças em relação ao MC_LA: não há ISO nem extração. A pessoa aponta a pasta do Banjo-Tooie
// XBLA já descompactada (default.xex + RAWFiles); a conferência usa a pré-checagem do próprio jogo
// (--preflight_only) e o SHA-256 dos arquivos-chave. Tudo fica na pasta portátil:
//   <raiz>\Banjo-Tooie PT-BR Recompiled.exe   este launcher
//   <raiz>\bin\                               o port (banjotooie.exe, rexruntime.dll, runtime VC++)
//   <raiz>\dados\                             saves, opções de PC, backups, registros, falhas
//   <raiz>\cache\                             cache de shaders
//   <raiz>\jogo\                              cópia dos arquivos do jogo (se a pessoa escolher)
//   <raiz>\config\launcher.ini                a pasta do jogo escolhida (relativa quando possível)
// Nada no Registro do Windows; nenhuma rede.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

namespace BanjoLauncher {
 static class Paths {
  public const string AppName="Banjo-Tooie PT-BR Recompiled";
  public const string Version="1.0.1";
  public static string Root {get{return Path.GetFullPath(AppDomain.CurrentDomain.BaseDirectory).TrimEnd('\\');}}
  public static string Bin {get{return Path.Combine(Root,"bin");}}
  public static string GameExe {get{return Path.Combine(Bin,"banjotooie.exe");}}
  public static string Data {get{return Path.Combine(Root,"dados");}}
  public static string Cache {get{return Path.Combine(Root,"cache");}}
  public static string LocalGame {get{return Path.Combine(Root,"jogo");}}
  public static string Config {get{return Path.Combine(Root,"config","launcher.ini");}}
  public static string LauncherLogs {get{return Path.Combine(Root,"logs");}}
  public static string PcSettings {get{return Path.Combine(Data,"pc_settings.toml");}}
  public static string RuntimeToml {get{return Path.Combine(Bin,"banjotooie.toml");}}
  public static string SessionLogs {get{return Path.Combine(Data,"logs");}}
  public static string Crashes {get{return Path.Combine(Data,"crashes");}}
  public static string Backups {get{return Path.Combine(Data,"backups");}}
  public const string SaveRel=@"B13EBABEBABEBABE\58410955\00000001\BanjoSave.bin\savegame.dat";
  public const string HeaderRel=@"B13EBABEBABEBABE\58410955\Headers\00000001\BanjoSave.bin.header";
  // Caminho guardado relativo à raiz quando a pasta está dentro dela (a pasta portátil pode mudar
  // de unidade ou de computador).
  public static string Store(string full) {
   full=Path.GetFullPath(full).TrimEnd('\\');
   string root=Root+"\\";
   if(full.StartsWith(root,StringComparison.OrdinalIgnoreCase)) return full.Substring(root.Length);
   if(String.Equals(full,Root,StringComparison.OrdinalIgnoreCase)) return ".";
   return full;
  }
  public static string Resolve(string stored) {
   if(String.IsNullOrWhiteSpace(stored)) return null;
   return Path.IsPathRooted(stored)?stored:Path.GetFullPath(Path.Combine(Root,stored));
  }
 }

 // Versões do jogo que o port aceita (SHA-256 dos arquivos-chave). O default.xex PT-BR desta
 // pessoa vem decifrado; o da versão global (inglês) é o de varejo cifrado: o runtime decifra e a
 // imagem é a mesma, então o port roda as duas. A tradução está em X360_strings.dat e db360*.
 static class Known {
  public const string XexPtBr="3efca627d60a6425af5c1891e3fe2666bf544b288c3e27e3259fa99ad50c1ee1";
  public const string XexRetail="4b512ae9f4412c246fe40a8b07b9d2932e1d18259dacb830991d7dbf827367a5";
  public static readonly Dictionary<string,string> PtBr=new Dictionary<string,string>{
   {@"RAWFiles\X360_strings.dat","5920b7624634fc4e91e5fa7172972401df386de58a499bcfb64083e3e3f7cbe0"},
   {@"RAWFiles\db360.cmp","b68557023ae8bddd729009c7ff02eec28f61873888373472a3539fa819cd20e3"},
   {@"RAWFiles\db360.textures.cmp","fb21dc86dd8bdee1f2bb6eaf7ec27a9fc90d2276e25bc8b8de3fcd94f7f68197"}};
  public static readonly Dictionary<string,string> Global=new Dictionary<string,string>{
   {@"RAWFiles\X360_strings.dat","d208df44a682fb7b101d5cc00eafbc2f5e0751b5e5a0de30c5ffb0565fbb6f98"},
   {@"RAWFiles\db360.cmp","7172cb4e91cda387682bda4a93674a55fa9d91b754caf8ee9638dbafa2eb7b71"},
   {@"RAWFiles\db360.textures.cmp","7e36051387b5bd0a47787d122166fb2af6a73bfbd7c5fef6924705d41fff585b"}};
 }

 static class Util {
  public static readonly JavaScriptSerializer Json=new JavaScriptSerializer{MaxJsonLength=int.MaxValue};
  public static string Sha256(string path) {
   using(var s=File.OpenRead(path)) using(var h=SHA256.Create()) return BitConverter.ToString(h.ComputeHash(s)).Replace("-","").ToLowerInvariant();
  }
  public static void WriteAtomic(string path,string text) {
   Directory.CreateDirectory(Path.GetDirectoryName(path));
   string tmp=path+".tmp";File.WriteAllText(tmp,text,new UTF8Encoding(false));
   if(File.Exists(path)) File.Replace(tmp,path,path+".bak"); else File.Move(tmp,path);
  }
  public static bool GameRunning() {
   foreach(var p in Process.GetProcessesByName("banjotooie")) {
    try {if(String.Equals(p.MainModule.FileName,Paths.GameExe,StringComparison.OrdinalIgnoreCase)) return true;}
    catch {} finally {p.Dispose();}
   }
   return false;
  }
  public static string Quote(string s) {return "\""+s.TrimEnd('\\').Replace("\"","")+"\"";}
 }

 // Arquivo "chave = valor" (pc_settings.toml do jogo, banjotooie.toml do runtime, launcher.ini):
 // troca só as linhas das chaves pedidas e mantém o resto (comentários, seções, chaves novas).
 sealed class KeyValueFile {
  readonly string path;
  readonly List<string> lines=new List<string>();
  public KeyValueFile(string path) {this.path=path;if(File.Exists(path)) lines.AddRange(File.ReadAllLines(path,Encoding.UTF8));}
  public bool Exists {get{return File.Exists(path);}}
  public string FilePath {get{return path;}}
  static readonly Regex Line=new Regex(@"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$");
  static string Clean(string raw) {
   var sb=new StringBuilder();bool q=false;
   foreach(char c in raw){if(c=='"'){q=!q;continue;}if(c=='#'&&!q)break;sb.Append(c);}
   return sb.ToString().Trim();
  }
  public string Get(string key) {
   foreach(var l in lines){var m=Line.Match(l);if(m.Success&&m.Groups[1].Value==key) return Clean(m.Groups[2].Value);}
   return null;
  }
  public void Set(string key,string value,bool quoted) {
   string text=key+" = "+(quoted?"\""+value+"\"":value);
   for(int i=0;i<lines.Count;i++){var m=Line.Match(lines[i]);if(m.Success&&m.Groups[1].Value==key){lines[i]=text;return;}}
   lines.Add(text);
  }
  public void Save() {Util.WriteAtomic(path,String.Join("\n",lines)+"\n");}
 }

 // Uma opção que o jogo de fato lê. File: "pc" = dados\pc_settings.toml (Configurações de PC do
 // jogo, as mesmas do menu dentro do jogo); "rt" = bin\banjotooie.toml (cvars do runtime).
 sealed class Option {
  public string Key,Label,File,Default;public string[] Labels,Values;public bool Quoted,Toggle;
  public static Option Choice(string file,string key,string label,string[] labels,string[] values,string def,bool quoted) {return new Option{File=file,Key=key,Label=label,Labels=labels,Values=values,Default=def,Quoted=quoted};}
  public static Option Flag(string file,string key,string label,bool def) {return new Option{File=file,Key=key,Label=label,Toggle=true,Default=def?"true":"false"};}
 }

 sealed class GameCheck {
  public bool Ok;public bool Compatible;public string Version="",VersionCode="";public string Message="";
  public List<string> MissingRequired=new List<string>();public List<string> MissingRecommended=new List<string>();
  public List<string> Details=new List<string>();
 }

 static class Game {
  public static Action<string> Log=delegate{};
  public static Action<int> Progress=delegate{};

  // Pasta do jogo guardada no launcher.ini (relativa à raiz quando dentro dela).
  public static string StoredFolder() {var ini=new KeyValueFile(Paths.Config);return Paths.Resolve(ini.Get("jogo"));}
  public static bool Prepared() {
   var ini=new KeyValueFile(Paths.Config);string f=Paths.Resolve(ini.Get("jogo"));
   return ini.Get("estado")=="pronto"&&f!=null&&File.Exists(Path.Combine(f,"default.xex"))&&File.Exists(Paths.GameExe);
  }

  // Conferência: a pré-checagem do próprio jogo (arquivos obrigatórios/recomendados) + SHA-256 dos
  // arquivos-chave para saber a versão (PT-BR, global ou desconhecida).
  public static GameCheck Check(string folder) {
   var r=new GameCheck();
   if(String.IsNullOrWhiteSpace(folder)||!Directory.Exists(folder)){r.Message=L.T("Escolha a pasta do Banjo-Tooie XBLA descompactado (a que tem default.xex e a pasta RAWFiles).","Choose the extracted Banjo-Tooie XBLA folder (the one with default.xex and the RAWFiles folder).");return r;}
   if(!File.Exists(Paths.GameExe)){r.Message=L.T("Falta o programa do port em bin\\banjotooie.exe. Extraia o pacote completo de novo.","The port program bin\\banjotooie.exe is missing. Extract the full package again.");return r;}
   folder=Path.GetFullPath(folder).TrimEnd('\\');
   Directory.CreateDirectory(Paths.LauncherLogs);
   string report=Path.Combine(Paths.LauncherLogs,"preflight.json");
   if(File.Exists(report)) File.Delete(report);
   var psi=new ProcessStartInfo(Paths.GameExe,"--game_data_root "+Util.Quote(folder)+" --user_data_root "+Util.Quote(Paths.Data)+" --cache_path "+Util.Quote(Paths.Cache)+" --preflight_only --port_silent --boot_report "+Util.Quote(report)){UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=Paths.Bin,RedirectStandardError=true};
   string err;int code;
   using(var p=Process.Start(psi)){err=p.StandardError.ReadToEnd();if(!p.WaitForExit(60000)){try{p.Kill();}catch{}r.Message=L.T("A pré-checagem do jogo não respondeu.","The game's preflight check did not respond.");return r;}code=p.ExitCode;}
   if(!File.Exists(report)){r.Message=L.T("A pré-checagem do jogo não gerou relatório (código ","The game's preflight check produced no report (code ")+code+"). "+err.Trim();return r;}
   var boot=Util.Json.Deserialize<Dictionary<string,object>>(File.ReadAllText(report,Encoding.UTF8));
   var pre=(Dictionary<string,object>)boot["preflight"];
   foreach(var o in (System.Collections.ArrayList)pre["missing_required"]) r.MissingRequired.Add(Convert.ToString(o));
   foreach(var o in (System.Collections.ArrayList)pre["missing_recommended"]) r.MissingRecommended.Add(Convert.ToString(o));
   bool passed=Convert.ToBoolean(pre["passed"]);
   if(!passed){r.Message=r.MissingRequired.Count>0?L.T("Faltam arquivos obrigatórios: ","Missing required files: ")+String.Join(", ",r.MissingRequired)+".":L.T("O default.xex não é um XEX válido.","default.xex is not a valid XEX file.");return r;}
   string xex=Util.Sha256(Path.Combine(folder,"default.xex"));
   r.Details.Add("default.xex SHA-256 "+xex);
   if(xex!=Known.XexPtBr&&xex!=Known.XexRetail){r.Message=L.T("Esse default.xex é de outra versão (SHA-256 ","This default.xex is from another version (SHA-256 ")+xex.Substring(0,12)+L.T("…). O port foi recompilado para o Banjo-Tooie XBLA (título 58410955) e não roda outra versão.","…). The port was recompiled for Banjo-Tooie XBLA (title 58410955) and cannot run another version.");return r;}
   r.Compatible=true;
   int pt=0,gl=0;
   foreach(var kv in Known.PtBr){string f=Path.Combine(folder,kv.Key);if(!File.Exists(f))continue;string h=Util.Sha256(f);r.Details.Add(kv.Key+" SHA-256 "+h);if(h==kv.Value)pt++;if(h==Known.Global[kv.Key])gl++;}
   r.VersionCode=pt==3?"ptbr":gl==3?"global":"modificada";r.Version=VersionName(r.VersionCode);
   r.Ok=true;
   r.Message=L.T("Pasta reconhecida: Banjo-Tooie XBLA, versão ","Folder recognized: Banjo-Tooie XBLA, version ")+r.Version+"."+(r.MissingRecommended.Count>0?L.T(" Arquivos recomendados ausentes: "," Missing recommended files: ")+String.Join(", ",r.MissingRecommended)+".":"")+(pt==3?"":L.T(" Este projeto é feito para a versão PT-BR; outras versões rodam, mas não são o alvo."," This project targets the PT-BR version; other versions run, but are not the target."));
   return r;
  }
  // Nome da versão no idioma da interface (o código fica no launcher.ini: ptbr, global, modificada).
  public static string VersionName(string code) {
   switch(code) {
    case "ptbr":return L.T("PT-BR (tradução conferida)","PT-BR (translation verified)");
    case "global":return L.T("Global (inglês)","Global (English)");
    case "modificada":return L.T("Modificada (textos ou texturas diferentes das versões conhecidas)","Modified (texts or textures differ from the known versions)");
    default:return code;
   }
  }

  // Preparação: confere, copia (opcional) os arquivos para <raiz>\jogo conferindo o SHA-256, cria as
  // pastas portáteis e grava o launcher.ini. Os arquivos originais nunca são alterados.
  public static string Prepare(string source,bool copy) {
   Log(L.T("[ETAPA 1/4] Conferindo os seus arquivos do jogo…","[STEP 1/4] Checking your game files…"));Progress(5);
   if(Util.GameRunning()) throw new IOException(L.T("Feche o jogo antes de preparar.","Close the game before preparing."));
   var check=Check(source);foreach(var d in check.Details)Log("  "+d);
   if(!check.Ok) throw new IOException(check.Message);
   Log(check.Message);
   string src=Path.GetFullPath(source).TrimEnd('\\');
   string game=src;
   if(copy&&!String.Equals(src,Paths.LocalGame,StringComparison.OrdinalIgnoreCase)) {
    Log(L.T("[ETAPA 2/4] Copiando os arquivos do jogo para a pasta portátil (os originais não mudam)…","[STEP 2/4] Copying the game files into the portable folder (the originals do not change)…"));Progress(15);
    var files=Directory.GetFiles(src,"*",SearchOption.AllDirectories);long total=files.Sum(f=>new FileInfo(f).Length),done=0;int copied=0,kept=0;
    foreach(string f in files) {
     string rel=f.Substring(src.Length).TrimStart('\\');string dst=Path.Combine(Paths.LocalGame,rel);
     Directory.CreateDirectory(Path.GetDirectoryName(dst));
     string hs=Util.Sha256(f);
     if(File.Exists(dst)&&new FileInfo(dst).Length==new FileInfo(f).Length&&Util.Sha256(dst)==hs){kept++;}
     else {File.Copy(f,dst+".tmp",true);if(Util.Sha256(dst+".tmp")!=hs)throw new IOException(L.T("Cópia diferente do original: ","Copy differs from the original: ")+rel);if(File.Exists(dst))File.Delete(dst);File.Move(dst+".tmp",dst);copied++;}
     done+=new FileInfo(f).Length;Progress(15+(int)(60*done/Math.Max(1,total)));
    }
    Log("  "+copied+L.T(" arquivos copiados, "," files copied, ")+kept+L.T(" já estavam iguais; SHA-256 conferido em todos."," were already identical; SHA-256 verified for all."));
    game=Paths.LocalGame;
    var again=Check(game);if(!again.Ok) throw new IOException(L.T("A cópia não passou na conferência: ","The copy failed the check: ")+again.Message);
   } else Log(L.T("[ETAPA 2/4] O jogo será lido da pasta escolhida (sem cópia).","[STEP 2/4] The game will be read from the chosen folder (no copy)."));
   Log(L.T("[ETAPA 3/4] Criando as pastas portáteis (dados, cache, registros)…","[STEP 3/4] Creating the portable folders (data, cache, logs)…"));Progress(80);
   foreach(string d in new[]{Paths.Data,Paths.Cache,Paths.LauncherLogs})Directory.CreateDirectory(d);
   var ini=new KeyValueFile(Paths.Config);
   ini.Set("jogo",Paths.Store(game),true);ini.Set("versao_jogo",check.Version,true);ini.Set("versao",check.VersionCode,true);ini.Set("launcher",Paths.Version,true);
   ini.Set("estado","pronto",true);ini.Set("preparado_em",DateTime.Now.ToString("yyyy-MM-dd HH:mm",CultureInfo.InvariantCulture),true);ini.Save();
   Log("  launcher.ini: jogo = "+Paths.Store(game));
   Log(L.T("[ETAPA 4/4] Pronto. Os shaders da placa de vídeo são preparados pelo próprio jogo ao abrir.","[STEP 4/4] Done. The game itself prepares the graphics card shaders when it starts."));Progress(100);
   return game;
  }

  // Shaders já guardados no cache (de sessões anteriores nesta pasta): o jogo os cria de novo
  // antes de rodar (--prepare-shaders grava o relatório e sai).
  public static string PrepareShaders() {
   string game=StoredFolder();if(game==null) throw new IOException(L.T("Prepare o jogo primeiro.","Prepare the game first."));
   string report=Path.Combine(Paths.LauncherLogs,"shaders.json");
   var psi=new ProcessStartInfo(Paths.GameExe,Args(game)+" --port_silent --prepare-shaders "+Util.Quote(report)){UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=Paths.Bin};
   using(var p=Process.Start(psi)){if(!p.WaitForExit(600000)){try{p.Kill();}catch{}throw new IOException(L.T("A preparação de shaders passou de 10 minutos.","Shader preparation took longer than 10 minutes."));}}
   if(!File.Exists(report)) return L.T("Shaders: o jogo não gerou relatório (nada guardado no cache ainda?).","Shaders: the game produced no report (nothing in the cache yet?).");
   var j=Util.Json.Deserialize<Dictionary<string,object>>(File.ReadAllText(report));
   object ok,adapter,driver,files,ms;
   j.TryGetValue("ok",out ok);j.TryGetValue("adapter",out adapter);j.TryGetValue("driver",out driver);j.TryGetValue("files",out files);j.TryGetValue("process_ms",out ms);
   if(ok is bool&&!(bool)ok) throw new IOException(L.T("O jogo não conseguiu preparar os shaders (veja logs\\shaders.json).","The game could not prepare the shaders (see logs\\shaders.json)."));
   int n=files is System.Collections.ArrayList?((System.Collections.ArrayList)files).Count:0;
   return L.T("Shaders preparados na ","Shaders prepared on ")+adapter+" (driver "+driver+"): "+n+L.T(" arquivo(s) do cache em "," cache file(s) in ")+Convert.ToString(ms,CultureInfo.InvariantCulture)+" ms.";
  }

  // Argumentos de caminho: tudo dentro da pasta portátil (saves, opções, registros, cache).
  public static string Args(string game) {
   return "--game_data_root "+Util.Quote(game)+" --user_data_root "+Util.Quote(Paths.Data)+" --cache_path "+Util.Quote(Paths.Cache);
  }
  public static Process Launch(string extra) {
   string game=StoredFolder();
   if(game==null||!Prepared()) throw new IOException(L.T("Prepare o jogo antes de jogar.","Prepare the game before playing."));
   Directory.CreateDirectory(Paths.Data);Directory.CreateDirectory(Paths.Cache);
   return Process.Start(new ProcessStartInfo(Paths.GameExe,Args(game)+(String.IsNullOrEmpty(extra)?"":" "+extra)){WorkingDirectory=Paths.Bin,UseShellExecute=false});
  }
 }

 // Opções: as mesmas chaves do menu CONFIGURAÇÕES DE PC do jogo (pc_settings.toml) e do runtime.
 static class Settings {
  static readonly string[] Sens={"0.5","0.75","1","1.25","1.5","2"};
  static readonly string[] SensL={"50%","75%","100%","125%","150%","200%"};
  // Nomes de tecla que o runtime entende (rexglue-sdk/src/ui/keybinds.cpp, kKeyNames).
  public static readonly string[] Keys=("Space Return Backspace Tab Escape Shift Control Alt Up Down Left Right "+
   "A B C D E F G H I J K L M N O P Q R S T U V W X Y Z 0 1 2 3 4 5 6 7 8 9 "+
   "F1 F2 F3 F4 F5 F6 F7 F8 F9 F10 F11 F12 Insert Delete Home End PageUp PageDown "+
   "Numpad0 Numpad1 Numpad2 Numpad3 Numpad4 Numpad5 Numpad6 Numpad7 Numpad8 Numpad9 NumpadPlus NumpadMinus NumpadStar NumpadSlash "+
   "Backtick Minus Plus Comma Period Semicolon Slash Backslash LBracket RBracket Quote CapsLock LMB RMB MMB").Split(' ');
  static string US {get{return L.T("EUA","US");}}
  static string KeyLabel(string k) {
   switch(k){
    case "Space":return L.T("Espaço","Space");case "Return":return "Enter";case "Backspace":return "Backspace";case "Escape":return "Esc";
    case "Control":return "Ctrl";case "Up":return L.T("Seta para cima","Up arrow");case "Down":return L.T("Seta para baixo","Down arrow");case "Left":return L.T("Seta esquerda","Left arrow");case "Right":return L.T("Seta direita","Right arrow");
    case "LMB":return L.T("Botão esquerdo do mouse","Left mouse button");case "RMB":return L.T("Botão direito do mouse","Right mouse button");case "MMB":return L.T("Botão do meio do mouse","Middle mouse button");
    case "PageUp":return "Page Up";case "PageDown":return "Page Down";case "Delete":return "Delete";
    case "Minus":return L.T("- (menos)","- (minus)");case "Plus":return L.T("= (igual)","= (equals)");case "Comma":return L.T(", (vírgula)",", (comma)");case "Period":return L.T(". (ponto)",". (period)");
    // Teclas OEM: o símbolo muda com o layout; os dois mais comuns (ABNT2 brasileiro e americano).
    case "Backtick":return "' (ABNT2) / ` ("+US+")";case "Semicolon":return "Ç (ABNT2) / ; ("+US+")";case "Slash":return "; (ABNT2) / / ("+US+")";
    case "Backslash":return "] (ABNT2) / \\ ("+US+")";case "LBracket":return "´ (ABNT2) / [ ("+US+")";case "RBracket":return "[ (ABNT2) / ] ("+US+")";case "Quote":return "~ (ABNT2) / ' ("+US+")";
    case "NumpadPlus":return "Numpad +";case "NumpadMinus":return "Numpad −";case "NumpadStar":return "Numpad *";case "NumpadSlash":return "Numpad /";
    default:return k.StartsWith("Numpad")?"Numpad "+k.Substring(6):k;
   }
  }
  // {chave, rótulo, padrão do port}
  static string[][] KeyActions(){return new string[][]{
   new[]{"keybind_start",L.T("Iniciar / pausa (Start)","Start / pause (Start)"),"Return"},new[]{"keybind_a",L.T("Botão A (pular)","A button (jump)"),"Space"},
   new[]{"keybind_b",L.T("Botão B (atacar / voltar)","B button (attack / back)"),"Backspace"},new[]{"keybind_x",L.T("Botão X","X button"),"R"},new[]{"keybind_y",L.T("Botão Y","Y button"),"E"},
   new[]{"keybind_left_trigger",L.T("Gatilho esquerdo (LT, agachar)","Left trigger (LT, crouch)"),"Z"},new[]{"keybind_right_trigger",L.T("Gatilho direito (RT)","Right trigger (RT)"),"X"},
   new[]{"keybind_left_shoulder",L.T("Botão LB","LB button"),"Q"},new[]{"keybind_right_shoulder",L.T("Botão RB","RB button"),"F"},
   new[]{"keybind_back",L.T("Botão Back","Back button"),"Tab"},
   new[]{"keybind_lstick_up",L.T("Mover para frente","Move forward"),"W"},new[]{"keybind_lstick_down",L.T("Mover para trás","Move back"),"S"},
   new[]{"keybind_lstick_left",L.T("Mover para a esquerda","Move left"),"A"},new[]{"keybind_lstick_right",L.T("Mover para a direita","Move right"),"D"},
   new[]{"keybind_dpad_up",L.T("Direcional para cima","D-pad up"),"Up"},new[]{"keybind_dpad_down",L.T("Direcional para baixo","D-pad down"),"Down"},
   new[]{"keybind_dpad_left",L.T("Direcional para a esquerda","D-pad left"),"Left"},new[]{"keybind_dpad_right",L.T("Direcional para a direita","D-pad right"),"Right"},
   new[]{"cam_key_up",L.T("Câmera para cima","Camera up"),"I"},new[]{"cam_key_down",L.T("Câmera para baixo","Camera down"),"K"},
   new[]{"cam_key_left",L.T("Câmera para a esquerda","Camera left"),"J"},new[]{"cam_key_right",L.T("Câmera para a direita","Camera right"),"L"}};}
  public static readonly List<KeyValuePair<string,Option>> All=new List<KeyValuePair<string,Option>>();
  static void G(string group,Option o){All.Add(new KeyValuePair<string,Option>(group,o));}
  static string[] A(params string[] s){return s;}
  // Os grupos ("Página|TÍTULO") são chaves internas; o título aparece traduzido (L.Group).
  static Settings() {Build();}
  // Refaz a lista com os rótulos no idioma atual (troca de idioma no launcher).
  public static void Rebuild() {All.Clear();Build();}
  static void Build() {
   // GRÁFICOS — resolução: a saída; a interna (escala da imagem 1280×720 do jogo) é separada.
   G("Gráficos|TELA",Option.Choice("pc","resolution",L.T("Resolução de saída","Output resolution"),A(L.T("Automática (pelo monitor)","Automatic (from the monitor)"),"1080p (1920 × 1080)","1440p (2560 × 1440)","4K (3840 × 2160)"),A("auto","1080p","1440p","4k"),"auto",true));
   G("Gráficos|TELA",Option.Choice("pc","internal_scale",L.T("Resolução interna (renderização)","Internal resolution (rendering)"),A(L.T("Automática (1080p/1440p → 1440p, 4K → 4K)","Automatic (1080p/1440p → 1440p, 4K → 4K)"),L.T("720p (1×, original)","720p (1×, original)"),"1440p (2×)","4K (3×)"),A("0","1","2","3"),"0",false));
   G("Gráficos|TELA",Option.Choice("pc","display_mode",L.T("Modo de exibição","Display mode"),A(L.T("Janela","Windowed"),L.T("Janela sem bordas","Borderless window"),L.T("Tela cheia","Fullscreen")),A("Janela","SemBordas","TelaCheia"),"Janela",true));
   G("Gráficos|TELA",Option.Choice("pc","monitor","Monitor",A(L.T("Automático (onde está o mouse)","Automatic (where the mouse is)")),A(""),"",true));
   G("Gráficos|TELA",Option.Flag("rt","host_vsync",L.T("Sincronização vertical (VSync, sem tearing)","Vertical sync (VSync, no tearing)"),true));
   G("Gráficos|QUADROS POR SEGUNDO",Option.Choice("pc","fps60",L.T("Limite de FPS da lógica do jogo","Game logic FPS limit"),A("60 FPS",L.T("30 FPS (original do console)","30 FPS (original console rate)")),A("true","false"),"true",false));
   G("Gráficos|QUADROS POR SEGUNDO",Option.Flag("pc","lock60",L.T("Ritmo fixo em 60 na tela (tempo de quadro constante)","Locked 60 pacing on screen (constant frame time)"),true));
   G("Gráficos|QUADROS POR SEGUNDO",Option.Flag("pc","scenes60",L.T("Cenas também a 60 — experimental (cenas contadas em quadros ficam mais rápidas)","Cutscenes at 60 too — experimental (frame-timed cutscenes play faster)"),false));
   G("Gráficos|QUALIDADE DA IMAGEM",Option.Choice("rt","swap_post_effect",L.T("Antisserrilhado (pós-processamento)","Anti-aliasing (post-processing)"),A(L.T("Desligado","Off"),"FXAA",L.T("FXAA forte","Strong FXAA")),A("none","fxaa","fxaa_extreme"),"none",true));
   G("Gráficos|QUALIDADE DA IMAGEM",Option.Flag("rt","native_2x_msaa",L.T("MSAA 2× nativo (vale ao reiniciar o jogo)","Native 2× MSAA (applies when the game restarts)"),true));
   G("Gráficos|QUALIDADE DA IMAGEM",Option.Choice("rt","anisotropic_override",L.T("Filtragem anisotrópica","Anisotropic filtering"),A(L.T("Como no jogo","As in the game"),L.T("Desligada","Off"),"2×","4×","8×","16×"),A("-1","0","2","3","4","5"),"3",false));
   G("Gráficos|QUALIDADE DA IMAGEM",Option.Choice("pc","draw_distance",L.T("Distância de desenho","Draw distance"),A("Original",L.T("Longe","Far"),L.T("Total","Full")),A("Original","Longe","Total"),"Original",true));
   // CONTROLES
   G("Controles|CONTROLE (GAMEPAD)",Option.Flag("pc","pad_vibration",L.T("Vibração","Vibration"),true));
   G("Controles|CONTROLE (GAMEPAD)",Option.Choice("pc","pad_deadzone",L.T("Zona morta do analógico","Stick dead zone"),A("5%","8%","12%","18%","25%"),A("0.05","0.08","0.12","0.18","0.25"),"0.12",false));
   G("Controles|CONTROLE (GAMEPAD)",Option.Choice("pc","pad_sens_x",L.T("Câmera no analógico direito — horizontal","Right-stick camera — horizontal"),SensL,Sens,"1",false));
   G("Controles|CONTROLE (GAMEPAD)",Option.Choice("pc","pad_sens_y",L.T("Câmera no analógico direito — vertical","Right-stick camera — vertical"),SensL,Sens,"1",false));
   G("Controles|CONTROLE (GAMEPAD)",Option.Flag("pc","pad_invert_x",L.T("Inverter horizontal do analógico direito","Invert right-stick horizontal"),false));
   G("Controles|CONTROLE (GAMEPAD)",Option.Flag("pc","pad_invert_y",L.T("Inverter vertical do analógico direito","Invert right-stick vertical"),false));
   G("Controles|CÂMERA PELO MOUSE",Option.Flag("pc","mouse_look",L.T("Girar a câmera com o mouse","Turn the camera with the mouse"),false));
   G("Controles|CÂMERA PELO MOUSE",Option.Choice("pc","cam_sens_x",L.T("Sensibilidade horizontal","Horizontal sensitivity"),SensL,Sens,"1",false));
   G("Controles|CÂMERA PELO MOUSE",Option.Choice("pc","cam_sens_y",L.T("Sensibilidade vertical","Vertical sensitivity"),SensL,Sens,"1",false));
   G("Controles|CÂMERA PELO MOUSE",Option.Flag("pc","cam_invert_x",L.T("Inverter eixo horizontal","Invert horizontal axis"),false));
   G("Controles|CÂMERA PELO MOUSE",Option.Flag("pc","cam_invert_y",L.T("Inverter eixo vertical","Invert vertical axis"),false));
   G("Controles|CÂMERA PELO MOUSE",Option.Choice("pc","cam_smoothing",L.T("Suavização","Smoothing"),A(L.T("Nenhuma","None"),L.T("Leve","Light"),L.T("Média","Medium"),L.T("Forte","Strong")),A("0","0.25","0.5","0.75"),"0.25",false));
   // Teclado: os mesmos cvars do runtime (keybind_*) e as teclas de câmera das Configurações de
   // PC que o menu F1 do jogo edita. Padrões = os do port (banjotooie_app.cpp). Start e B não
   // oferecem Escape/Shift: são os padrões do runtime, que o port troca por Return/Backspace ao
   // abrir quando o valor gravado é o padrão (HasNonDefaultValue).
   foreach(var k in KeyActions()) {
    string[] keys=k[0]=="keybind_start"?Keys.Where(x=>x!="Escape").ToArray():k[0]=="keybind_b"?Keys.Where(x=>x!="Shift").ToArray():Keys;
    G("Controles|TECLADO",Option.Choice(k[0].StartsWith("cam_")?"pc":"rt",k[0],k[1],keys.Select(KeyLabel).ToArray(),keys,k[2],true));
   }
   // JOGO, ÁUDIO E QUALIDADE DE VIDA
   G("Jogo|ÁUDIO",Option.Choice("pc","volume_master",L.T("Volume geral","Master volume"),Enumerable.Range(0,11).Select(i=>(i*10)+"%").ToArray(),Enumerable.Range(0,11).Select(i=>(i*10).ToString(CultureInfo.InvariantCulture)).ToArray(),"100",false));
   G("Jogo|ÁUDIO",Option.Flag("rt","audio_mute",L.T("Silenciar tudo","Mute everything"),false));
   G("Jogo|QUALIDADE DE VIDA",Option.Choice("pc","dialog_speed",L.T("Velocidade dos diálogos","Dialogue speed"),A("Original",L.T("Rápida (2×)","Fast (2×)"),L.T("Muito rápida (4×)","Very fast (4×)"),L.T("Instantânea","Instant")),A("0","1","2","3"),"0",false));
   G("Jogo|QUALIDADE DE VIDA",Option.Flag("pc","skip_intro_logos",L.T("Início rápido (acelera os logos de abertura)","Quick start (speeds up the opening logos)"),false));
   G("Jogo|QUALIDADE DE VIDA",Option.Flag("pc","pause_on_focus_loss",L.T("Pausar ao minimizar ou trocar de janela","Pause when minimized or switching windows"),true));
   G("Jogo|QUALIDADE DE VIDA",Option.Flag("pc","show_collectibles",L.T("Rastreador de colecionáveis por mundo","Per-world collectibles tracker"),false));
   G("Jogo|QUALIDADE DE VIDA",Option.Flag("pc","show_fps",L.T("Contador de FPS na tela","On-screen FPS counter"),false));
   G("Jogo|QUALIDADE DE VIDA",Option.Flag("pc","show_perf_stats",L.T("Estatísticas de desempenho na tela","On-screen performance statistics"),false));
   G("Jogo|SAVES E REGISTRO",Option.Flag("pc","save_backup",L.T("Backups automáticos do save","Automatic save backups"),true));
   G("Jogo|SAVES E REGISTRO",Option.Choice("pc","backup_count",L.T("Quantos backups manter","How many backups to keep"),A("5","10","20","30"),A("5","10","20","30"),"10",false));
   G("Jogo|SAVES E REGISTRO",Option.Flag("pc","log_detailed",L.T("Registro de sessão detalhado","Detailed session log"),false));
  }
  public static KeyValueFile Pc() {return new KeyValueFile(Paths.PcSettings);}
  public static KeyValueFile Rt() {return new KeyValueFile(Paths.RuntimeToml);}
  // Valor atual (o que o jogo vai ler) de cada opção.
  public static Dictionary<string,string> Load() {
   var pc=Pc();var rt=Rt();var v=new Dictionary<string,string>();
   foreach(var g in All) {
    var o=g.Value;string x;
    if(o.Key=="resolution") x=pc.Get("resolution_auto")=="false"?pc.Get("resolution"):"auto";
    else x=o.File=="pc"?pc.Get(o.Key):rt.Get(o.Key);
    v[o.Key]=x??o.Default;
   }
   return v;
  }
  // Grava nos arquivos que o jogo lê. Resolução, escala, janela e tela cheia ficam SÓ no
  // pc_settings.toml (o jogo os reaplica sobre o banjotooie.toml; M04 C-82).
  public static void Save(Dictionary<string,string> values) {
   if(Util.GameRunning()) throw new IOException(L.T("Feche o jogo antes de salvar no launcher (ou use F1 dentro do jogo).","Close the game before saving in the launcher (or use F1 inside the game)."));
   var pc=Pc();var rt=Rt();
   foreach(var g in All) {
    var o=g.Value;string x;if(!values.TryGetValue(o.Key,out x))continue;
    if(o.Key=="resolution"){if(x=="auto")pc.Set("resolution_auto","true",false);else{pc.Set("resolution_auto","false",false);pc.Set("resolution",x,true);}continue;}
    if(o.File=="pc")pc.Set(o.Key,x,o.Quoted);else rt.Set(o.Key,x,o.Quoted);
   }
   if(pc.Get("settings_version")==null) pc.Set("settings_version","4",false);
   pc.Save();
   if(values.Keys.Any(k=>All.Any(g=>g.Value.Key==k&&g.Value.File=="rt"))) rt.Save();
  }
 }

 static class Saves {
  public static string Save {get{string p=Path.Combine(Paths.Data,Paths.SaveRel);return File.Exists(p)?p:null;}}
  public static FileInfo[] Backups() {
   if(!Directory.Exists(Paths.Backups)) return new FileInfo[0];
   return new DirectoryInfo(Paths.Backups).GetFiles("*.dat").OrderBy(f=>f.LastWriteTimeUtc).ToArray();
  }
  // Exporta dados\ (sem cache) num ZIP fora da pasta.
  public static int Export(string dest) {
   if(Util.GameRunning()) throw new IOException(L.T("Feche o jogo antes de exportar.","Close the game before exporting."));
   if(!Directory.Exists(Paths.Data)) throw new IOException(L.T("Ainda não há dados para exportar.","There is no data to export yet."));
   dest=Path.GetFullPath(dest);if(dest.StartsWith(Paths.Data+"\\",StringComparison.OrdinalIgnoreCase)) throw new IOException(L.T("Salve o backup fora da pasta dos dados.","Save the backup outside the data folder."));
   if(File.Exists(dest))File.Delete(dest);int n=0;
   using(var zip=ZipFile.Open(dest,ZipArchiveMode.Create)) foreach(var f in Directory.GetFiles(Paths.Data,"*",SearchOption.AllDirectories)){zip.CreateEntryFromFile(f,f.Substring(Paths.Data.Length).TrimStart('\\').Replace('\\','/'),CompressionLevel.Optimal);n++;}
   return n;
  }
  // Pasta usada pelo jogo instalado fora do pacote (Documentos\My Games\Banjo-Tooie XBLA).
  public static string DocumentsSaves {get{return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),"My Games","Banjo-Tooie XBLA");}}
  // Copia (nunca move) o save e o cabeçalho da pasta de Documentos para dados\. Se já houver um
  // save na pasta portátil, ele vai antes para dados\backups\antes-da-importacao-*.dat.
  public static string ImportFromDocuments() {
   if(Util.GameRunning()) throw new IOException(L.T("Feche o jogo antes de importar.","Close the game before importing."));
   string src=Path.Combine(DocumentsSaves,Paths.SaveRel);
   if(!File.Exists(src)) throw new IOException(L.T("Não há save em ","There is no save in ")+DocumentsSaves+".");
   string dst=Path.Combine(Paths.Data,Paths.SaveRel);
   if(File.Exists(dst)){Directory.CreateDirectory(Paths.Backups);File.Copy(dst,Path.Combine(Paths.Backups,"antes-da-importacao-"+DateTime.Now.ToString("yyyyMMdd-HHmmss")+".dat"));}
   Directory.CreateDirectory(Path.GetDirectoryName(dst));File.Copy(src,dst,true);
   string hs=Path.Combine(DocumentsSaves,Paths.HeaderRel),hd=Path.Combine(Paths.Data,Paths.HeaderRel);
   if(File.Exists(hs)){Directory.CreateDirectory(Path.GetDirectoryName(hd));File.Copy(hs,hd,true);}
   if(Util.Sha256(src)!=Util.Sha256(dst)) throw new IOException(L.T("A cópia do save ficou diferente do original.","The save copy differs from the original."));
   return L.T("Save copiado de ","Save copied from ")+src+L.T(" (o original não foi alterado)."," (the original was not changed).");
  }
  // Importa um .zip exportado por este launcher para dados\. Os dados atuais são exportados antes
  // para <raiz>\backups-launcher\ (nada é apagado). Entradas fora de dados\ são ignoradas.
  public static string ImportZip(string zipPath) {
   if(Util.GameRunning()) throw new IOException(L.T("Feche o jogo antes de importar.","Close the game before importing."));
   using(var zip=ZipFile.OpenRead(zipPath)) {
    if(!zip.Entries.Any(e=>e.Name=="pc_settings.toml"||e.Name=="savegame.dat")) throw new IOException(L.T("Esse .zip não parece um backup deste launcher (não tem save nem pc_settings.toml).","This .zip does not look like a backup from this launcher (it has no save and no pc_settings.toml)."));
    string before="";
    if(Directory.Exists(Paths.Data)&&Directory.GetFiles(Paths.Data,"*",SearchOption.AllDirectories).Length>0) {
     string dir=Path.Combine(Paths.Root,"backups-launcher");Directory.CreateDirectory(dir);
     before=Path.Combine(dir,"dados-antes-da-importacao-"+DateTime.Now.ToString("yyyyMMdd-HHmmss")+".zip");Export(before);
    }
    int n=0,skipped=0;
    foreach(var e in zip.Entries) {
     if(String.IsNullOrEmpty(e.Name))continue;
     string dst=Path.GetFullPath(Path.Combine(Paths.Data,e.FullName.Replace('/','\\')));
     if(!dst.StartsWith(Paths.Data+"\\",StringComparison.OrdinalIgnoreCase)){skipped++;continue;}
     Directory.CreateDirectory(Path.GetDirectoryName(dst));e.ExtractToFile(dst,true);n++;
    }
    return n+L.T(" arquivos importados"," files imported")+(skipped>0?" ("+skipped+L.T(" ignorados por apontarem para fora de dados\\)"," skipped because they point outside dados\\)"):"")+(before==""?".":L.T("; os dados anteriores foram guardados em ","; the previous data was saved to ")+Paths.Store(before)+".");
   }
  }
 }

 // Monitores ligados (nome do dispositivo usado pelo jogo na opção monitor, resolução física).
 static class Displays {
  [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)]
  struct DISPLAY_DEVICE {public int cb;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)]public string DeviceName;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=128)]public string DeviceString;public int StateFlags;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=128)]public string DeviceID;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=128)]public string DeviceKey;}
  [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)]
  struct DEVMODE {
   [MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)]public string dmDeviceName;
   public short dmSpecVersion,dmDriverVersion,dmSize,dmDriverExtra;public int dmFields;
   public int dmPositionX,dmPositionY,dmDisplayOrientation,dmDisplayFixedOutput;
   public short dmColor,dmDuplex,dmYResolution,dmTTOption,dmCollate;
   [MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)]public string dmFormName;
   public short dmLogPixels;public int dmBitsPerPel,dmPelsWidth,dmPelsHeight,dmDisplayFlags,dmDisplayFrequency;
   public int dmICMMethod,dmICMIntent,dmMediaType,dmDitherType,dmReserved1,dmReserved2,dmPanningWidth,dmPanningHeight;
  }
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern bool EnumDisplayDevices(string device,uint num,ref DISPLAY_DEVICE dd,uint flags);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern bool EnumDisplaySettings(string device,int mode,ref DEVMODE dm);
  // {dispositivo, texto}
  public static List<string[]> List() {
   var list=new List<string[]>();
   for(uint i=0;i<32;i++) {
    var dd=new DISPLAY_DEVICE();dd.cb=Marshal.SizeOf(dd);
    if(!EnumDisplayDevices(null,i,ref dd,0))break;
    if((dd.StateFlags&1)==0)continue;  // não ligado à área de trabalho
    var dm=new DEVMODE();dm.dmSize=(short)Marshal.SizeOf(dm);
    if(!EnumDisplaySettings(dd.DeviceName,-1,ref dm))continue;
    string n=dd.DeviceName.Replace(@"\\.\DISPLAY","");
    list.Add(new[]{dd.DeviceName,"Monitor "+n+" — "+dm.dmPelsWidth+" × "+dm.dmPelsHeight+L.T(" a "," at ")+dm.dmDisplayFrequency+" Hz"+((dd.StateFlags&4)!=0?L.T(" (principal)"," (primary)"):"")});
   }
   return list;
  }
 }

 // Diagnóstico: um .txt com o que ajuda a investigar um problema. Sem saves e sem conteúdo do jogo.
 static class Diagnostics {
  static string Tail(string path,int lines) {
   try {var all=File.ReadAllLines(path,Encoding.UTF8);return String.Join(Environment.NewLine,all.Skip(Math.Max(0,all.Length-lines)));}
   catch(Exception e){return L.T("(não foi possível ler: ","(could not read: ")+e.Message+")";}
  }
  public static string Write() {
   var sb=new StringBuilder();
   sb.AppendLine(L.T("Diagnóstico — ","Diagnostics — ")+Paths.AppName+" (launcher "+Paths.Version+")");
   sb.AppendLine(L.T("Data: ","Date: ")+DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss",CultureInfo.InvariantCulture));
   sb.AppendLine("Windows: "+Environment.OSVersion.VersionString+(Environment.Is64BitOperatingSystem?" (64-bit)":" (32-bit)")+", .NET "+Environment.Version);
   sb.AppendLine(L.T("Pasta portátil: ","Portable folder: ")+Paths.Root);
   sb.AppendLine();sb.AppendLine(L.T("[Programa do port em bin\\]","[Port program in bin\\]"));
   foreach(var f in new[]{"banjotooie.exe","rexruntime.dll","msvcp140.dll","msvcp140_atomic_wait.dll","vcruntime140.dll","vcruntime140_1.dll"}) {
    string p=Path.Combine(Paths.Bin,f);
    sb.AppendLine("  "+f+": "+(File.Exists(p)?new FileInfo(p).Length+" bytes"+(f.EndsWith(".exe")||f=="rexruntime.dll"?", SHA-256 "+Util.Sha256(p):""):L.T("AUSENTE","MISSING")));
   }
   sb.AppendLine();sb.AppendLine("[launcher.ini]");
   sb.AppendLine(File.Exists(Paths.Config)?File.ReadAllText(Paths.Config,Encoding.UTF8).Trim():L.T("  (ainda não preparado)","  (not prepared yet)"));
   string game=Game.StoredFolder();
   sb.AppendLine();sb.AppendLine(L.T("[Arquivos do jogo]","[Game files]"));
   if(game!=null) {
    try {var c=Game.Check(game);sb.AppendLine("  "+c.Message);foreach(var d in c.Details)sb.AppendLine("  "+d);}
    catch(Exception e){sb.AppendLine(L.T("  conferência falhou: ","  check failed: ")+e.Message);}
   } else sb.AppendLine(L.T("  nenhuma pasta escolhida","  no folder chosen"));
   sb.AppendLine();sb.AppendLine(L.T("[Monitores]","[Monitors]"));
   foreach(var m in Displays.List())sb.AppendLine("  "+m[0]+": "+m[1]);
   sb.AppendLine();sb.AppendLine(L.T("[Configurações de PC: dados\\pc_settings.toml]","[PC settings: dados\\pc_settings.toml]"));
   sb.AppendLine(File.Exists(Paths.PcSettings)?File.ReadAllText(Paths.PcSettings,Encoding.UTF8).Trim():L.T("  (ainda não existe: o jogo cria na primeira abertura)","  (does not exist yet: the game creates it on first launch)"));
   sb.AppendLine();sb.AppendLine(L.T("[Opções do runtime: bin\\banjotooie.toml]","[Runtime options: bin\\banjotooie.toml]"));
   sb.AppendLine(File.Exists(Paths.RuntimeToml)?File.ReadAllText(Paths.RuntimeToml,Encoding.UTF8).Trim():L.T("  (ainda não existe)","  (does not exist yet)"));
   sb.AppendLine();sb.AppendLine("[Saves]");
   sb.AppendLine("  save: "+(Saves.Save!=null?L.T("existe (","exists (")+new FileInfo(Saves.Save).Length+" bytes, "+new FileInfo(Saves.Save).LastWriteTime.ToString("yyyy-MM-dd HH:mm",CultureInfo.InvariantCulture)+")":L.T("ainda não existe","does not exist yet")));
   sb.AppendLine(L.T("  backups automáticos: ","  automatic backups: ")+Saves.Backups().Length);
   int crashes=Directory.Exists(Paths.Crashes)?Directory.GetFiles(Paths.Crashes).Length:0;
   sb.AppendLine(L.T("  relatórios de falha: ","  crash reports: ")+crashes);
   sb.AppendLine(L.T("  cache de shaders: ","  shader cache: ")+(Directory.Exists(Paths.Cache)?Directory.GetFiles(Paths.Cache,"*",SearchOption.AllDirectories).Length:0)+L.T(" arquivos"," files"));
   if(Directory.Exists(Paths.SessionLogs)) {
    var last=new DirectoryInfo(Paths.SessionLogs).GetFiles("sessao-*.txt").OrderBy(f=>f.LastWriteTimeUtc).LastOrDefault();
    if(last!=null){sb.AppendLine();sb.AppendLine(L.T("[Fim do último registro de sessão: ","[End of the latest session log: ")+last.Name+"]");sb.AppendLine(Tail(last.FullName,80));}
   }
   string launcherLog=Path.Combine(Paths.LauncherLogs,"launcher.log");
   if(File.Exists(launcherLog)){sb.AppendLine();sb.AppendLine(L.T("[Fim do registro do launcher]","[End of the launcher log]"));sb.AppendLine(Tail(launcherLog,60));}
   Directory.CreateDirectory(Paths.LauncherLogs);
   string outFile=Path.Combine(Paths.LauncherLogs,"diagnostico-"+DateTime.Now.ToString("yyyyMMdd-HHmmss")+".txt");
   File.WriteAllText(outFile,sb.ToString(),new UTF8Encoding(true));
   return outFile;
  }
 }
}
