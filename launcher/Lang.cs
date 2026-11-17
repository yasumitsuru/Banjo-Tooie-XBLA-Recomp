// Banjo-Tooie XBLA PT-BR Recompiled — launcher portátil: idiomas da interface (português e inglês).
// GPL-3.0-or-later, como o resto do launcher.
//
// Escolha do idioma: --idioma pt|en na linha de comando (só naquela execução, para testes) >
// "idioma" em config\launcher.ini (gravado pelo seletor da barra lateral) > idioma do Windows
// (português → PT-BR; qualquer outro → inglês). Os textos do XAML são escritos em português e
// trocados ao carregar a janela (tabela Xaml); os textos do código usam L.T("pt","en").
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;

namespace BanjoLauncher {
 static class L {
  public static bool En;
  public static string Code {get{return En?"en":"pt";}}
  public static string T(string pt,string en) {return En?en:pt;}
  public static CultureInfo Culture {get{return CultureInfo.GetCultureInfo(En?"en-US":"pt-BR");}}
  public static string Date(DateTime d) {return d.ToString(En?"yyyy-MM-dd HH:mm":"dd/MM/yyyy HH:mm",CultureInfo.InvariantCulture);}

  public static void Init(string[] args) {
   int i=Array.IndexOf(args,"--idioma");
   string code=i>=0&&i+1<args.Length?args[i+1]:null;
   if(code!="pt"&&code!="en") code=Saved();
   if(code!="pt"&&code!="en") code=CultureInfo.CurrentUICulture.TwoLetterISOLanguageName=="pt"?"pt":"en";
   En=code=="en";
  }
  public static string Saved() {return new KeyValueFile(Paths.Config).Get("idioma");}
  public static void Persist(string code) {var ini=new KeyValueFile(Paths.Config);ini.Set("idioma",code,true);ini.Save();}

  // Títulos dos grupos de opções (a chave interna continua em português).
  public static string Group(string title) {
   if(!En) return title;
   switch(title) {
    case "TELA":return "DISPLAY";case "QUADROS POR SEGUNDO":return "FRAMES PER SECOND";case "QUALIDADE DA IMAGEM":return "IMAGE QUALITY";
    case "CONTROLE (GAMEPAD)":return "CONTROLLER (GAMEPAD)";case "CÂMERA PELO MOUSE":return "MOUSE CAMERA";case "TECLADO":return "KEYBOARD";
    case "ÁUDIO":return "AUDIO";case "QUALIDADE DE VIDA":return "QUALITY OF LIFE";case "SAVES E REGISTRO":return "SAVES AND LOGGING";
    default:return title;
   }
  }

  // Troca os textos fixos da janela (TextBlock, conteúdo de botões/caixas, nomes acessíveis).
  public static void Apply(object node) {
   if(!En||node==null) return;
   var tb=node as TextBlock;if(tb!=null&&tb.Text!=null) tb.Text=X(tb.Text);
   var cc=node as ContentControl;if(cc!=null&&cc.Content is string) cc.Content=X((string)cc.Content);
   var fe=node as FrameworkElement;
   if(fe!=null){string an=AutomationProperties.GetName(fe);if(!String.IsNullOrEmpty(an))AutomationProperties.SetName(fe,X(an));}
   var d=node as DependencyObject;if(d==null) return;
   foreach(object c in LogicalTreeHelper.GetChildren(d)) Apply(c);
  }
  static string X(string pt) {string en;return Xaml.TryGetValue(pt,out en)?en:pt;}

  static readonly Dictionary<string,string> Xaml=new Dictionary<string,string>{
   {"Launcher portátil","Portable launcher"},
   {"Port nativo · ReXGlue · sem emulador","Native port · ReXGlue · no emulator"},
   {"Jogar","Play"},{"Gráficos","Graphics"},{"Controles","Controls"},{"Áudio e jogo","Audio & game"},
   {"Saves e backups","Saves & backups"},{"Diagnóstico","Diagnostics"},{"Créditos","Credits"},
   {"A AVENTURA CONTINUA","ADVENTURE AWAITS"},{"EM 4K A 60 FPS.","IN 4K AT 60 FPS."},
   {"Tradução PT-BR · Port nativo recompilado · Pasta 100% portátil","PT-BR translation · Recompiled native port · 100% portable folder"},
   {"PRONTO PARA A AVENTURA?","READY FOR THE ADVENTURE?"},{"Prepare o seu jogo","Prepare your game"},{"JOGAR","PLAY"},
   {"Escolha abaixo a pasta do seu Banjo-Tooie XBLA descompactado.","Choose your extracted Banjo-Tooie XBLA folder below."},
   {"SEUS ARQUIVOS DO JOGO","YOUR GAME FILES"},
   {"Pasta do Banjo-Tooie XBLA já descompactado (com default.xex e a pasta RAWFiles)","Extracted Banjo-Tooie XBLA folder (with default.xex and the RAWFiles folder)"},
   {"Procurar pasta…","Browse…"},{"Conferir","Check"},
   {"Use a sua própria cópia do jogo. Ela só é lida: nada nela é alterado, apagado ou enviado.","Use your own copy of the game. It is only read: nothing in it is changed, deleted or sent anywhere."},
   {"Copiar os arquivos do jogo para dentro desta pasta portátil (pasta jogo\\)","Copy the game files into this portable folder (jogo\\ folder)"},
   {"Ligado: a pasta portátil fica completa e pode ser levada para outro disco ou computador (cerca de 95 MB a mais). Desligado: o jogo é lido da pasta escolhida, que precisa continuar no mesmo lugar.",
    "On: the portable folder is self-contained and can be moved to another drive or computer (about 95 MB more). Off: the game is read from the chosen folder, which must stay where it is."},
   {"PREPARAR MEU JOGO","PREPARE MY GAME"},{"Abrir a pasta portátil","Open the portable folder"},
   {"O jogo é um programa do Windows recompilado a partir do código do Xbox 360 — não há emulador nem imagem de disco. A preparação confere os seus arquivos (a própria pré-checagem do jogo e o SHA-256 dos arquivos-chave), copia se você quiser e guarda tudo nesta pasta: saves, opções, registros e cache. Nada vai para o Registro do Windows e nada é baixado da internet.",
    "The game is a Windows program recompiled from the Xbox 360 code — there is no emulator and no disc image. Preparing checks your files (the game's own preflight check and the SHA-256 of the key files), copies them if you want, and keeps everything in this folder: saves, settings, logs and cache. Nothing goes into the Windows Registry and nothing is downloaded from the internet."},
   {"GRÁFICOS","GRAPHICS"},
   {"As mesmas opções do menu Configurações de PC do jogo (F1). Valem na próxima vez que o jogo abrir.","The same options as the game's PC Settings menu (F1). They apply the next time the game starts."},
   {"ORIGINAL","ORIGINAL"},{"30 FPS, imagem interna 720p,\nalcance e filtros do console.","30 FPS, 720p internal image,\nconsole draw distance and filtering."},
   {"EQUILIBRADO","BALANCED"},{"60 FPS, imagem interna 1440p,\nFXAA e anisotrópica 8×.","60 FPS, 1440p internal image,\nFXAA and 8× anisotropic."},
   {"4K MÁXIMO","4K MAXIMUM"},{"Saída e imagem interna 4K a 60 FPS,\nMSAA 2×, anisotrópica 16×, alcance Longe.","4K output and internal image at 60 FPS,\n2× MSAA, 16× anisotropic, Far draw distance."},
   {"Perfil Original","Original profile"},{"Perfil Equilibrado","Balanced profile"},{"Perfil 4K máximo","4K maximum profile"},
   {"Perfil: configurações atuais","Profile: current settings"},
   {"SHADERS DA PLACA DE VÍDEO","GRAPHICS CARD SHADERS"},
   {"O jogo guarda na pasta cache\\ cada shader que a placa já usou. Depois de trocar de placa ou de driver, preparar de novo evita travadinhas na primeira passagem por cada lugar.",
    "The game keeps every shader the card has already used in the cache\\ folder. After changing the graphics card or driver, preparing them again avoids small stutters the first time you pass through each place."},
   {"Preparar shaders guardados agora","Prepare saved shaders now"},
   {"Restaurar padrões","Restore defaults"},{"SALVAR CONFIGURAÇÕES","SAVE SETTINGS"},
   {"CONTROLES","CONTROLS"},
   {"Teclado e mouse funcionam sempre; um controle de Xbox (ou compatível com XInput) conectado também. Os botões do controle seguem o layout do Xbox 360 e não são remapeados; as teclas do teclado sim. Também dá para trocar as teclas no menu F1 do jogo, apertando a tecla nova.",
    "Keyboard and mouse always work; a connected Xbox (or XInput-compatible) controller works too. Controller buttons follow the Xbox 360 layout and are not remapped; keyboard keys are. You can also change keys in the game's F1 menu by pressing the new key."},
   {"ÁUDIO E JOGO","AUDIO & GAME"},
   {"Música e efeitos sonoros têm volumes separados no menu de opções do próprio jogo; aqui ficam o volume geral do PC e as melhorias de qualidade de vida do port.",
    "Music and sound effects have separate volumes in the game's own options menu; here you'll find the PC master volume and the port's quality-of-life improvements."},
   {"SAVES E BACKUPS","SAVES & BACKUPS"},
   {"Saves, backups automáticos, opções de PC e registros ficam na pasta dados\\ desta pasta portátil. Levar a pasta inteira leva também o seu progresso.",
    "Saves, automatic backups, PC settings and logs live in the dados\\ folder of this portable folder. Moving the whole folder also moves your progress."},
   {"SEU SAVE","YOUR SAVE"},{"Abrir pasta dos dados","Open data folder"},{"Abrir backups automáticos","Open automatic backups"},
   {"Exportar backup .zip","Export .zip backup"},{"Importar backup .zip","Import .zip backup"},
   {"Importar um .zip substitui os dados atuais; antes disso eles são exportados para backups-launcher\\ (nada é apagado). Para voltar a um backup automático, use F1 → SAVES dentro do jogo: o jogo confere o arquivo antes de restaurar.",
    "Importing a .zip replaces the current data; before that, the current data is exported to backups-launcher\\ (nothing is deleted). To go back to an automatic backup, use F1 → SAVES inside the game: the game checks the file before restoring it."},
   {"SAVE DO JOGO INSTALADO FORA DESTA PASTA","SAVE FROM THE GAME INSTALLED OUTSIDE THIS FOLDER"},
   {"Copiar o save de Documentos para esta pasta","Copy the save from Documents to this folder"},
   {"Só copia: o save de Documentos\\My Games continua intacto. Se já houver um save aqui, ele vai antes para dados\\backups.",
    "Copy only: the save in Documents\\My Games stays untouched. If there is already a save here, it is moved to dados\\backups first."},
   {"REGISTRO E DIAGNÓSTICO","LOG & DIAGNOSTICS"},
   {"Mensagens da conferência, da preparação e de cada abertura do jogo. O diagnóstico junta num arquivo de texto o que ajuda a investigar um problema (sem o save e sem arquivos do jogo).",
    "Messages from checking, preparing and each game launch. The diagnostics report gathers into a text file what helps to investigate a problem (without the save and without game files)."},
   {"Gerar diagnóstico","Create diagnostics report"},{"Copiar registro","Copy log"},{"Registros do jogo","Game logs"},
   {"Registros do launcher","Launcher logs"},{"Relatórios de falha","Crash reports"},
   {"CRÉDITOS","CREDITS"},
   {"Recompilação base do Banjo-Tooie XBLA: iron-jay (banjo_tooie_xbla_recomp)","Base recompilation of Banjo-Tooie XBLA: iron-jay (banjo_tooie_xbla_recomp)"},
   {"Runtime, recompilador e renderização: ReXGlue SDK (BSD-3-Clause, derivado do Xenia)","Runtime, recompiler and rendering: ReXGlue SDK (BSD-3-Clause, derived from Xenia)"},
   {"Referência de endereços para 60 FPS e alcance de desenho: patches da comunidade do Xenia (illusion, retroben, wedarobi)","Address reference for 60 FPS and draw distance: Xenia community patches (illusion, retroben, wedarobi)"},
   {"Modelo deste launcher (layout, estilos, configurador e backups): MC_LA_Recompiled, de Roberto Araujo (GPL-3.0-or-later). Este launcher também é GPL-3.0-or-later.",
    "Model for this launcher (layout, styles, settings editor and backups): MC_LA_Recompiled, by Roberto Araujo (GPL-3.0-or-later). This launcher is also GPL-3.0-or-later."},
   {"Tradução PT-BR: a da cópia do jogo fornecida por você","PT-BR translation: the one in the game copy you provide"},
   {"Banjo-Tooie e suas marcas pertencem à Microsoft e à Rare. Esta pasta não traz nenhum arquivo do jogo: o port usa somente a sua própria cópia, neste computador. O programa do port contém código recompilado do jogo: é para uso pessoal; não compartilhe a pasta preparada.",
    "Banjo-Tooie and its trademarks belong to Microsoft and Rare. This folder contains no game files: the port only uses your own copy, on this computer. The port program contains code recompiled from the game: it is for personal use; do not share the prepared folder."},
   {"Abrir créditos e licenças","Open credits and licenses"},
   {"Escolha a pasta do seu jogo.","Choose your game folder."}};
 }
}
