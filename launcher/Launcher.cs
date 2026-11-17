// Banjo-Tooie XBLA PT-BR Recompiled — launcher portátil: interface (WPF) e modos de teste.
// Adaptado do configurador do MC_LA_Recompiled (Roberto Araujo, GPL-3.0-or-later) via a primeira
// adaptação do projeto irmão W:\Decomp\Banjo. GPL-3.0-or-later.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace BanjoLauncher {
 static class Program {
  [STAThread]
  static int Main(string[] args) {
   try {
    if(args.Length>=2&&args[0]=="--teste") return TestMode.Run(args);
    var app=new Application{ShutdownMode=ShutdownMode.OnMainWindowClose};
    var c=new Controller();
    if(args.Length>=2&&args[0]=="--captura-ui") {
     string page=args.Length>=4&&args[2]=="--pagina"?args[3]:"jogar";
     return c.Capture(app,args[1],page);
    }
    app.Run(c.Window);
    return 0;
   } catch(Exception e) {
    try {Directory.CreateDirectory(Paths.LauncherLogs);File.AppendAllText(Path.Combine(Paths.LauncherLogs,"launcher.log"),DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss")+" ERRO FATAL "+e+Environment.NewLine);} catch {}
    if(args.Length==0) MessageBox.Show("O launcher encontrou um erro e vai fechar:\n\n"+e.Message,Paths.AppName,MessageBoxButton.OK,MessageBoxImage.Error);
    return 1;
   }
  }
 }

 static class Res {
  public static Stream Open(string name) {return Assembly.GetExecutingAssembly().GetManifestResourceStream("BanjoLauncher."+name);}
  public static BitmapImage Image(string name) {
   var s=Open(name);if(s==null)return null;
   var b=new BitmapImage();b.BeginInit();b.CacheOption=BitmapCacheOption.OnLoad;b.StreamSource=s;b.EndInit();b.Freeze();return b;
  }
 }

 sealed class Controller {
  public readonly Window Window;
  readonly Dictionary<string,Control> controls=new Dictionary<string,Control>();
  readonly Dictionary<string,string> pageOf=new Dictionary<string,string>();
  readonly HashSet<string> dirty=new HashSet<string>();
  Process running;
  bool loading;
  T F<T>(string name) where T:class {return Window.FindName(name) as T;}

  public Controller() {
   using(var s=Res.Open("Launcher.xaml")) Window=(Window)XamlReader.Load(s);
   Window.Title=Paths.AppName;
   try {using(var ico=Res.Open("banjotooie.ico")) Window.Icon=BitmapFrame.Create(ico,BitmapCreateOptions.None,BitmapCacheOption.OnLoad);} catch {}
   var art=Res.Image("banjotooie.png");F<Image>("HeroArt").Source=art;F<Image>("SideLogo").Source=art;
   F<TextBlock>("VersionText").Text="Launcher portátil "+Paths.Version;
   Game.Log=Log;
   Game.Progress=v=>Ui(()=>F<ProgressBar>("WorkProgress").Value=v);
   Nav("NavHome","PageHome");Nav("NavGraphics","PageGraphics");Nav("NavControls","PageControls");Nav("NavGame","PageGame");
   Nav("NavSaves","PageSaves");Nav("NavLogs","PageLogs");Nav("NavCredits","PageCredits");
   BuildOptions();
   LoadOptions();
   Click("BrowseSource",BrowseSource);Click("CheckSource",CheckSource);Click("PrepareButton",Prepare);Click("PlayButton",Play);
   Click("OpenRoot",()=>OpenFolder(Paths.Root));
   Click("ApplyGraphics",()=>SavePage("Gráficos"));Click("ApplyControls",()=>SavePage("Controles"));Click("ApplyGame",()=>SavePage("Jogo"));
   Click("DefaultsGraphics",()=>Defaults("Gráficos"));Click("DefaultsControls",()=>Defaults("Controles"));Click("DefaultsGame",()=>Defaults("Jogo"));
   Click("PresetOriginal",()=>Preset("Original",new Dictionary<string,string>{{"fps60","false"},{"internal_scale","1"},{"draw_distance","Original"},{"swap_post_effect","none"},{"native_2x_msaa","false"},{"anisotropic_override","-1"}}));
   Click("PresetBalanced",()=>Preset("Equilibrado",new Dictionary<string,string>{{"fps60","true"},{"lock60","true"},{"internal_scale","2"},{"draw_distance","Original"},{"swap_post_effect","fxaa"},{"native_2x_msaa","false"},{"anisotropic_override","4"}}));
   Click("PresetMax",()=>Preset("4K máximo",new Dictionary<string,string>{{"fps60","true"},{"lock60","true"},{"resolution","4k"},{"internal_scale","3"},{"draw_distance","Longe"},{"swap_post_effect","none"},{"native_2x_msaa","true"},{"anisotropic_override","5"},{"host_vsync","true"}}));
   Click("PrepareShaders",PrepareShaders);
   Click("OpenSaves",()=>OpenFolder(Paths.Data));Click("OpenBackups",()=>OpenFolder(Paths.Backups));
   Click("BackupSaves",ExportSaves);Click("ImportZip",ImportZip);Click("ImportDocs",ImportDocs);
   Click("Diagnose",Diagnose);Click("CopyLogs",()=>{try{Clipboard.SetText(F<TextBox>("LogText").Text);Status("Registro copiado.");}catch(Exception e){Status("Não foi possível copiar: "+e.Message);}});
   Click("OpenLogs",()=>OpenFolder(Paths.SessionLogs));Click("OpenLauncherLogs",()=>OpenFolder(Paths.LauncherLogs));Click("OpenCrashes",()=>OpenFolder(Paths.Crashes));
   Click("OpenLicenses",()=>{string l=Path.Combine(Paths.Root,"LICENCAS");OpenFolder(Directory.Exists(l)?l:Paths.Root);});
   F<TextBox>("SourcePath").TextChanged+=delegate{if(!loading)F<Button>("PrepareButton").IsEnabled=true;};
   Window.Closing+=(s,e)=>{
    if(dirty.Count>0&&MessageBox.Show(Window,"Há opções alteradas que ainda não foram salvas. Fechar mesmo assim?",Paths.AppName,MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes) e.Cancel=true;
   };
   RefreshHome();RefreshSaves();RefreshShaders();
   Log("Launcher "+Paths.Version+" aberto em "+Paths.Root);
  }

  // ---------- utilidades da interface ----------
  void Ui(Action a) {if(Window.Dispatcher.CheckAccess())a();else Window.Dispatcher.BeginInvoke(a);}
  void Click(string name,Action a) {var b=F<Button>(name);if(b!=null)b.Click+=delegate{try{a();}catch(Exception e){Fail(e);}};}
  void Status(string text) {Ui(()=>F<TextBlock>("StatusText").Text=text);}
  void Fail(Exception e) {Log("ERRO: "+e.Message);Status(e.Message);MessageBox.Show(Window,e.Message,Paths.AppName,MessageBoxButton.OK,MessageBoxImage.Warning);}
  void Log(string text) {
   string line=DateTime.Now.ToString("HH:mm:ss")+"  "+text;
   try {Directory.CreateDirectory(Paths.LauncherLogs);File.AppendAllText(Path.Combine(Paths.LauncherLogs,"launcher.log"),DateTime.Now.ToString("yyyy-MM-dd ")+line+Environment.NewLine,new UTF8Encoding(false));} catch {}
   Ui(()=>{var t=F<TextBox>("LogText");t.AppendText(line+Environment.NewLine);t.ScrollToEnd();});
  }
  void OpenFolder(string path) {Directory.CreateDirectory(path);Process.Start("explorer.exe",Util.Quote(path));}
  void Nav(string radio,string page) {
   F<RadioButton>(radio).Checked+=delegate{
    foreach(var p in new[]{"PageHome","PageGraphics","PageControls","PageGame","PageSaves","PageLogs","PageCredits"})((FrameworkElement)Window.FindName(p)).Visibility=p==page?Visibility.Visible:Visibility.Collapsed;
    if(page=="PageSaves")RefreshSaves();if(page=="PageGraphics")RefreshShaders();
   };
  }
  public void ShowPage(string page) {
   var map=new Dictionary<string,string>{{"jogar","NavHome"},{"graficos","NavGraphics"},{"controles","NavControls"},{"jogo","NavGame"},{"saves","NavSaves"},{"registro","NavLogs"},{"creditos","NavCredits"}};
   string r;if(map.TryGetValue(page,out r))F<RadioButton>(r).IsChecked=true;
  }
  Task Busy(string text,Action work,Action done) {
   foreach(var n in new[]{"PrepareButton","PlayButton","CheckSource","PrepareShaders","BackupSaves","ImportZip","ImportDocs"}){var b=F<Button>(n);if(b!=null)b.IsEnabled=false;}
   Status(text);
   return Task.Run(work).ContinueWith(t=>Ui(()=>{
    F<ProgressBar>("WorkProgress").Value=0;
    foreach(var n in new[]{"PrepareButton","CheckSource","PrepareShaders","BackupSaves","ImportZip","ImportDocs"}){var b=F<Button>(n);if(b!=null)b.IsEnabled=true;}
    RefreshHome();
    if(t.IsFaulted){var e=t.Exception.GetBaseException();Fail(e);}
    else if(done!=null)done();
   }));
  }

  // ---------- opções ----------
  void BuildOptions() {
   var mon=Settings.All.First(g=>g.Value.Key=="monitor").Value;
   var labels=new List<string>{"Automático (onde está o mouse ao abrir)"};var values=new List<string>{""};
   foreach(var m in Displays.List()){labels.Add(m[1]);values.Add(m[0]);}
   mon.Labels=labels.ToArray();mon.Values=values.ToArray();
   foreach(var group in Settings.All.GroupBy(g=>g.Key)) {
    var parts=group.Key.Split('|');string page=parts[0];
    var host=F<StackPanel>(page=="Gráficos"?"GraphicsGroups":page=="Controles"?"ControlGroups":"GameGroups");
    var card=new Border{Style=(Style)Window.FindResource("Card"),Margin=new Thickness(0,0,0,18)};
    var stack=new StackPanel();card.Child=stack;
    stack.Children.Add(new TextBlock{Text=parts[1],FontWeight=FontWeights.Bold,Foreground=(Brush)new BrushConverter().ConvertFrom("#F5B83D"),Margin=new Thickness(0,0,0,12)});
    bool twoCols=parts[1]=="TECLADO";
    var grid=new Grid();
    int cols=twoCols?2:1;
    for(int c=0;c<cols;c++){grid.ColumnDefinitions.Add(new ColumnDefinition());if(c<cols-1)grid.ColumnDefinitions.Add(new ColumnDefinition{Width=new GridLength(24)});}
    int i=0;
    foreach(var item in group) {
     var o=item.Value;pageOf[o.Key]=page;
     int row=i/cols,col=(i%cols)*2;i++;
     while(grid.RowDefinitions.Count<=row)grid.RowDefinitions.Add(new RowDefinition{Height=GridLength.Auto});
     FrameworkElement cell;
     if(o.Toggle) {
      var cb=new CheckBox{Content=new TextBlock{Text=o.Label,TextWrapping=TextWrapping.Wrap},Margin=new Thickness(0,7,0,7),Tag=o};
      cb.Checked+=delegate{Changed(o);};cb.Unchecked+=delegate{Changed(o);};
      controls[o.Key]=cb;cell=cb;
     } else {
      var g=new Grid{Margin=new Thickness(0,5,0,5)};
      g.ColumnDefinitions.Add(new ColumnDefinition());g.ColumnDefinitions.Add(new ColumnDefinition{Width=new GridLength(twoCols?190:330)});
      var label=new TextBlock{Text=o.Label,VerticalAlignment=VerticalAlignment.Center,TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,0,14,0)};
      var combo=new ComboBox{Tag=o};
      for(int k=0;k<o.Values.Length;k++)combo.Items.Add(new ComboBoxItem{Content=o.Labels[k],Tag=o.Values[k]});
      combo.SelectionChanged+=delegate{Changed(o);};
      Grid.SetColumn(combo,1);g.Children.Add(label);g.Children.Add(combo);
      controls[o.Key]=combo;cell=g;
     }
     Grid.SetRow(cell,row);Grid.SetColumn(cell,col);grid.Children.Add(cell);
    }
    stack.Children.Add(grid);
    if(parts[1]=="TELA") stack.Children.Add(new TextBlock{Text="O jogo desenha a imagem em 1280 × 720 vezes a escala interna e entrega na resolução de saída. Em 1080p a imagem interna 1440p é reduzida na apresentação (mais nitidez). 4K real = saída 4K com imagem interna 4K.",Style=(Style)Window.FindResource("Hint"),FontSize=11,Margin=new Thickness(0,10,0,0)});
    if(parts[1]=="QUADROS POR SEGUNDO") stack.Children.Add(new TextBlock{Text="60 FPS usa a lógica de 60 quadros do port (patch da comunidade do Xenia aplicado no código). 30 FPS é o ritmo original do Xbox 360.",Style=(Style)Window.FindResource("Hint"),FontSize=11,Margin=new Thickness(0,10,0,0)});
    if(parts[1]=="TECLADO") stack.Children.Add(new TextBlock{Text="F1 abre e fecha as Configurações de PC dentro do jogo. Duas ações na mesma tecla funcionam ao mesmo tempo.",Style=(Style)Window.FindResource("Hint"),FontSize=11,Margin=new Thickness(0,10,0,0)});
    host.Children.Add(card);
   }
  }
  void Changed(Option o) {
   if(loading)return;string p;if(pageOf.TryGetValue(o.Key,out p))dirty.Add(p);
   if(p=="Gráficos")F<TextBlock>("PresetLabel").Text="Perfil: personalizado (não salvo)";
   Status("Opções alteradas: clique em SALVAR CONFIGURAÇÕES.");
  }
  void SetControl(string key,string value) {
   Control c;if(!controls.TryGetValue(key,out c))return;
   var cb=c as CheckBox;if(cb!=null){cb.IsChecked=value=="true";return;}
   var combo=(ComboBox)c;
   foreach(ComboBoxItem it in combo.Items) if((string)it.Tag==value){combo.SelectedItem=it;return;}
   // Valor válido para o jogo mas fora da lista (ex.: escolhido no menu F1): mostrado como está.
   var extra=new ComboBoxItem{Content="Atual: "+value,Tag=value};combo.Items.Add(extra);combo.SelectedItem=extra;
  }
  string ControlValue(string key) {
   Control c=controls[key];var cb=c as CheckBox;if(cb!=null)return cb.IsChecked==true?"true":"false";
   var it=((ComboBox)c).SelectedItem as ComboBoxItem;return it==null?null:(string)it.Tag;
  }
  void LoadOptions() {
   loading=true;
   try {foreach(var kv in Settings.Load())SetControl(kv.Key,kv.Value);}
   finally {loading=false;}
   dirty.Clear();
  }
  void SavePage(string page) {
   var values=new Dictionary<string,string>();
   foreach(var g in Settings.All) if(g.Key.StartsWith(page+"|")){string v=ControlValue(g.Value.Key);if(v!=null)values[g.Value.Key]=v;}
   Settings.Save(values);dirty.Remove(page);
   Log("Configurações salvas ("+page+"): "+values.Count+" opções em "+Paths.Store(Paths.PcSettings)+(values.Keys.Any(k=>Settings.All.Any(g=>g.Value.Key==k&&g.Value.File=="rt"))?" e "+Paths.Store(Paths.RuntimeToml):"")+".");
   Status("Configurações salvas. Valem na próxima vez que o jogo abrir.");
   if(page=="Gráficos")F<TextBlock>("PresetLabel").Text="Perfil: configurações atuais (salvas)";
  }
  void Defaults(string page) {
   foreach(var g in Settings.All) if(g.Key.StartsWith(page+"|")) SetControl(g.Value.Key,g.Value.Default);
   dirty.Add(page);Status("Padrões do port carregados: clique em SALVAR CONFIGURAÇÕES para gravar.");
  }
  void Preset(string name,Dictionary<string,string> values) {
   foreach(var kv in values)SetControl(kv.Key,kv.Value);
   dirty.Add("Gráficos");F<TextBlock>("PresetLabel").Text="Perfil: "+name+" (não salvo)";
   Status("Perfil "+name+" carregado: clique em SALVAR CONFIGURAÇÕES.");
  }

  // ---------- jogo ----------
  void RefreshHome() {
   loading=true;
   try {
    bool ready=Game.Prepared();string stored=Game.StoredFolder();
    var src=F<TextBox>("SourcePath");
    if(String.IsNullOrEmpty(src.Text)) {
     if(stored!=null)src.Text=stored;
     else {string found=AutoDetect();if(found!=null){src.Text=found;F<TextBlock>("SourceCheck").Text="Encontrei uma pasta do jogo aqui perto. Clique em Conferir ou em PREPARAR MEU JOGO.";}}
    }
    F<Button>("PlayButton").IsEnabled=ready&&running==null;
    F<TextBlock>("InstallStatus").Text=running!=null?"O jogo está aberto":ready?"Pronto para jogar":"Prepare o seu jogo";
    string ver=new KeyValueFile(Paths.Config).Get("versao_jogo");
    string where=stored!=null&&String.Equals(stored.TrimEnd('\\'),Paths.LocalGame,StringComparison.OrdinalIgnoreCase)?"cópia portátil na pasta jogo\\":stored;
    F<TextBlock>("HomeActionHint").Text=ready?"Jogo: "+where+(ver!=null?" · versão "+ver:"")+". F1 dentro do jogo abre as Configurações de PC.":"Escolha abaixo a pasta do seu Banjo-Tooie XBLA descompactado e clique em PREPARAR MEU JOGO.";
    if(ready&&running==null)Status("Pronto. Clique em JOGAR.");
   } finally {loading=false;}
  }
  // Procura a pasta do jogo dentro da pasta portátil e ao lado dela (sem varrer o disco).
  static string AutoDetect() {
   var cands=new List<string>{Paths.LocalGame};
   try {var parent=Directory.GetParent(Paths.Root);if(parent!=null)cands.AddRange(Directory.GetDirectories(parent.FullName));cands.AddRange(Directory.GetDirectories(Paths.Root));} catch {}
   foreach(var d in cands) if(File.Exists(Path.Combine(d,"default.xex"))&&Directory.Exists(Path.Combine(d,"RAWFiles"))) return d;
   return null;
  }
  void BrowseSource() {
   using(var dlg=new System.Windows.Forms.FolderBrowserDialog{Description="Escolha a pasta do Banjo-Tooie XBLA descompactado (a que tem default.xex e RAWFiles).",ShowNewFolderButton=false}) {
    var cur=F<TextBox>("SourcePath").Text;if(Directory.Exists(cur))dlg.SelectedPath=cur;
    if(dlg.ShowDialog()==System.Windows.Forms.DialogResult.OK){F<TextBox>("SourcePath").Text=dlg.SelectedPath;CheckSource();}
   }
  }
  void CheckSource() {
   string folder=F<TextBox>("SourcePath").Text.Trim();GameCheck r=null;
   Log("Conferindo "+folder+"…");
   Busy("Conferindo os arquivos do jogo…",()=>{r=Game.Check(folder);},()=>{
    F<TextBlock>("SourceCheck").Text=(r.Ok?"✔ ":"✖ ")+r.Message;
    F<TextBlock>("SourceCheck").Foreground=(Brush)new BrushConverter().ConvertFrom(r.Ok?"#9FE2A0":"#FF9C7A");
    Log(r.Message);foreach(var d in r.Details)Log("  "+d);
    Status(r.Ok?"Pasta reconhecida. Clique em PREPARAR MEU JOGO.":"A pasta não serve: veja a mensagem.");
   });
  }
  void Prepare() {
   string folder=F<TextBox>("SourcePath").Text.Trim();bool copy=F<CheckBox>("CopyGame").IsChecked==true;
   Busy("Preparando…",()=>Game.Prepare(folder,copy),()=>{
    F<TextBlock>("SourceCheck").Text="✔ Jogo preparado. Os arquivos originais não foram alterados.";
    F<TextBlock>("SourceCheck").Foreground=(Brush)new BrushConverter().ConvertFrom("#9FE2A0");
    if(copy)F<TextBox>("SourcePath").Text=Paths.LocalGame;
    LoadOptions();RefreshShaders();Status("Jogo preparado. Clique em JOGAR.");
   });
  }
  void Play() {
   if(Util.GameRunning()){Status("O jogo já está aberto.");return;}
   foreach(var p in dirty.ToList())SavePage(p);
   running=Game.Launch("");
   Log("Jogo aberto (processo "+running.Id+"): "+Paths.Store(Paths.GameExe)+" "+Game.Args(Game.StoredFolder()));
   RefreshHome();Status("Jogo aberto. F1 abre as Configurações de PC dentro do jogo.");
   Window.WindowState=WindowState.Minimized;
   var proc=running;
   Task.Run(()=>{proc.WaitForExit();int code=proc.ExitCode;Ui(()=>{
    running=null;Log("Jogo fechado (código "+code+").");
    Window.WindowState=WindowState.Normal;Window.Activate();RefreshHome();RefreshSaves();LoadOptions();
    if(code!=0){Status("O jogo fechou com erro (código "+code+"). Veja a página Diagnóstico.");
     MessageBox.Show(Window,"O jogo fechou com o código "+code+".\n\nOs detalhes estão em dados\\logs e, se houve falha, em dados\\crashes. Na página Diagnóstico, \"Gerar diagnóstico\" junta tudo num arquivo de texto.",Paths.AppName,MessageBoxButton.OK,MessageBoxImage.Warning);}
   });});
  }
  void RefreshShaders() {
   int n=Directory.Exists(Paths.Cache)?Directory.GetFiles(Paths.Cache,"*",SearchOption.AllDirectories).Length:0;
   long size=n==0?0:Directory.GetFiles(Paths.Cache,"*",SearchOption.AllDirectories).Sum(f=>new FileInfo(f).Length);
   F<TextBlock>("ShaderStatus").Text=n==0?"O cache de shaders desta pasta ainda está vazio: o jogo cria os shaders na primeira passagem por cada lugar.":"Cache de shaders: "+n+" arquivos ("+(size/1048576.0).ToString("0.0",CultureInfo.GetCultureInfo("pt-BR"))+" MB) em cache\\.";
   F<Button>("PrepareShaders").IsEnabled=n>0&&Game.Prepared();
  }
  void PrepareShaders() {
   if(Util.GameRunning()){Status("Feche o jogo antes.");return;}
   string msg=null;Busy("Preparando os shaders guardados (pode levar alguns minutos)…",()=>{msg=Game.PrepareShaders();},()=>{Log(msg);Status(msg);RefreshShaders();});
  }

  // ---------- saves ----------
  void RefreshSaves() {
   string save=Saves.Save;
   F<TextBlock>("SavePath").Text="Pasta: "+Paths.Data;
   var b=Saves.Backups();
   F<TextBlock>("SaveSummary").Text=(save==null?"Ainda não há save nesta pasta (ele aparece depois da primeira gravação no jogo).":"Save: "+new FileInfo(save).Length+" bytes, gravado em "+new FileInfo(save).LastWriteTime.ToString("dd/MM/yyyy HH:mm"))+
    "\nBackups automáticos: "+b.Length+(b.Length>0?" (o mais recente de "+b.Last().LastWriteTime.ToString("dd/MM/yyyy HH:mm")+")":"");
   string docs=Path.Combine(Saves.DocumentsSaves,Paths.SaveRel);
   bool has=File.Exists(docs);
   F<TextBlock>("DocsSummary").Text=has?"Encontrado um save em Documentos\\My Games\\Banjo-Tooie XBLA (gravado em "+new FileInfo(docs).LastWriteTime.ToString("dd/MM/yyyy HH:mm")+"). É o save do port aberto sem este launcher.":"Nenhum save em Documentos\\My Games\\Banjo-Tooie XBLA.";
   F<Button>("ImportDocs").IsEnabled=has;
  }
  void ExportSaves() {
   var dlg=new Microsoft.Win32.SaveFileDialog{Title="Exportar backup dos dados",FileName="banjotooie-dados-"+DateTime.Now.ToString("yyyyMMdd-HHmm")+".zip",Filter="Arquivo ZIP|*.zip",InitialDirectory=Paths.Root};
   if(dlg.ShowDialog(Window)!=true)return;string dest=dlg.FileName;int n=0;
   Busy("Exportando…",()=>{n=Saves.Export(dest);},()=>{Log("Backup exportado: "+dest+" ("+n+" arquivos).");Status("Backup exportado ("+n+" arquivos).");});
  }
  void ImportZip() {
   var dlg=new Microsoft.Win32.OpenFileDialog{Title="Importar backup dos dados",Filter="Arquivo ZIP|*.zip",InitialDirectory=Paths.Root};
   if(dlg.ShowDialog(Window)!=true)return;
   if(MessageBox.Show(Window,"Os dados atuais (saves e opções) serão substituídos pelos do arquivo escolhido. Antes disso eles são exportados para backups-launcher\\. Continuar?",Paths.AppName,MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes)return;
   string src=dlg.FileName,msg=null;
   Busy("Importando…",()=>{msg=Saves.ImportZip(src);},()=>{Log(msg);Status(msg);LoadOptions();RefreshSaves();});
  }
  void ImportDocs() {
   if(MessageBox.Show(Window,"Copiar o save de Documentos\\My Games\\Banjo-Tooie XBLA para esta pasta portátil? O original continua onde está; se já houver um save aqui, ele vai antes para dados\\backups.",Paths.AppName,MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes)return;
   string msg=null;Busy("Copiando o save…",()=>{msg=Saves.ImportFromDocuments();},()=>{Log(msg);Status("Save copiado para a pasta portátil.");RefreshSaves();});
  }
  void Diagnose() {
   string file=null;Busy("Gerando diagnóstico…",()=>{file=Diagnostics.Write();},()=>{Log("Diagnóstico gravado em "+Paths.Store(file)+".");Status("Diagnóstico gravado.");Process.Start("explorer.exe","/select,"+Util.Quote(file));});
  }

  // Captura da interface (testes): desenha a janela fora da tela, grava o PNG e sai.
  public int Capture(Application app,string png,string page) {
   Window.WindowStartupLocation=WindowStartupLocation.Manual;Window.Left=-30000;Window.Top=-30000;Window.ShowActivated=false;Window.ShowInTaskbar=false;
   int result=1;
   Window.ContentRendered+=delegate{
    ShowPage(page);
    Window.Dispatcher.BeginInvoke(DispatcherPriority.ApplicationIdle,new Action(()=>{
     try {
      var root=(FrameworkElement)Window.Content;
      var bmp=new RenderTargetBitmap((int)root.ActualWidth,(int)root.ActualHeight,96,96,PixelFormats.Pbgra32);
      var vis=new DrawingVisual();
      using(var dc=vis.RenderOpen()){dc.DrawRectangle(Window.Background,null,new Rect(0,0,root.ActualWidth,root.ActualHeight));dc.DrawRectangle(new VisualBrush(root),null,new Rect(0,0,root.ActualWidth,root.ActualHeight));}
      bmp.Render(vis);
      var enc=new PngBitmapEncoder();enc.Frames.Add(BitmapFrame.Create(bmp));
      Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(png)));
      using(var f=File.Create(png))enc.Save(f);
      result=0;
     } finally {dirty.Clear();Window.Close();}
    }));
   };
   app.Run(Window);
   return result;
  }
 }

 // Modo de teste sem interface: --teste <saida.json> [--jogo <pasta>] [--preparar] [--copiar]
 //   [--opcoes "chave=valor;chave=valor"] [--abrir-jogo "<argumentos extras>"] [--espera <s>]
 //   [--exportar <zip>] [--importar-zip <zip>] [--importar-documentos] [--preparar-shaders]
 //   [--diagnostico]
 // Cada etapa grava o que fez e o resultado num JSON (usado por tools/launcher/testar_launcher.py).
 static class TestMode {
  static string Arg(string[] a,string name){int i=Array.IndexOf(a,name);return i>=0&&i+1<a.Length?a[i+1]:null;}
  public static int Run(string[] args) {
   var r=new Dictionary<string,object>();var log=new List<string>();
   Game.Log=s=>log.Add(s);
   string outFile=args[1];int fail=0;
   r["launcher"]=Paths.Version;r["raiz"]=Paths.Root;
   r["pastas"]=new Dictionary<string,object>{{"bin",Paths.Bin},{"dados",Paths.Data},{"cache",Paths.Cache},{"config",Paths.Config},{"jogo_local",Paths.LocalGame}};
   r["bin_arquivos"]=Directory.Exists(Paths.Bin)?Directory.GetFiles(Paths.Bin).Select(Path.GetFileName).ToArray():new string[0];
   try {
    string jogo=Arg(args,"--jogo");
    if(jogo!=null) {
     var c=Game.Check(jogo);
     r["conferencia"]=new Dictionary<string,object>{{"pasta",jogo},{"ok",c.Ok},{"compativel",c.Compatible},{"versao",c.Version},{"mensagem",c.Message},{"faltando_obrigatorios",c.MissingRequired},{"faltando_recomendados",c.MissingRecommended},{"detalhes",c.Details}};
     if(Array.IndexOf(args,"--preparar")>=0) {
      try {
       string used=Game.Prepare(jogo,Array.IndexOf(args,"--copiar")>=0);
       r["preparo"]=new Dictionary<string,object>{{"ok",true},{"jogo_usado",used},{"launcher_ini",File.ReadAllText(Paths.Config)},{"pronto",Game.Prepared()}};
      } catch(Exception e){r["preparo"]=new Dictionary<string,object>{{"ok",false},{"erro",e.Message}};fail=1;}
     }
    }
    r["preparado"]=Game.Prepared();r["jogo_guardado"]=Game.StoredFolder();
    r["launcher_ini"]=File.Exists(Paths.Config)?File.ReadAllText(Paths.Config):null;
    string opts=Arg(args,"--opcoes");
    if(opts!=null) {
     var want=new Dictionary<string,string>();
     foreach(var kv in opts.Split(';').Where(s=>s.Contains("="))){var p=kv.Split(new[]{'='},2);want[p[0].Trim()]=p[1].Trim();}
     Settings.Save(want);
     var got=Settings.Load();
     var diff=want.Where(kv=>got[kv.Key]!=kv.Value).Select(kv=>kv.Key+": pedido "+kv.Value+", lido "+got[kv.Key]).ToList();
     r["opcoes"]=new Dictionary<string,object>{{"gravadas",want},{"relidas",want.Keys.ToDictionary(k=>k,k=>got[k])},{"diferencas",diff},{"pc_settings",File.ReadAllText(Paths.PcSettings)},{"runtime_toml",File.Exists(Paths.RuntimeToml)?File.ReadAllText(Paths.RuntimeToml):null}};
     if(diff.Count>0)fail=1;
    }
    r["opcoes_atuais"]=Settings.Load();
    string exp=Arg(args,"--exportar");
    if(exp!=null) r["exportar"]=new Dictionary<string,object>{{"arquivo",exp},{"arquivos",Saves.Export(exp)}};
    string imp=Arg(args,"--importar-zip");
    if(imp!=null) {
     try {r["importar_zip"]=Saves.ImportZip(imp);} catch(Exception e){r["importar_zip"]="erro: "+e.Message;fail=1;}
    }
    if(Array.IndexOf(args,"--importar-documentos")>=0) {
     try {r["importar_documentos"]=Saves.ImportFromDocuments();} catch(Exception e){r["importar_documentos"]="erro: "+e.Message;}
    }
    string extra=Arg(args,"--abrir-jogo");
    if(extra!=null) {
     int wait=Int32.Parse(Arg(args,"--espera")??"300",CultureInfo.InvariantCulture);
     var sw=Stopwatch.StartNew();
     var p=Game.Launch(extra);
     bool exited=p.WaitForExit(wait*1000);
     if(!exited){try{p.Kill();}catch{}}
     r["jogo"]=new Dictionary<string,object>{{"argumentos",Game.Args(Game.StoredFolder())+" "+extra},{"saiu_sozinho",exited},{"codigo",exited?p.ExitCode:-1},{"segundos",Math.Round(sw.Elapsed.TotalSeconds,1)}};
     if(!exited||p.ExitCode!=0)fail=1;
    }
    if(Array.IndexOf(args,"--preparar-shaders")>=0) {
     try {r["shaders"]=Game.PrepareShaders();} catch(Exception e){r["shaders"]="erro: "+e.Message;fail=1;}
    }
    if(Array.IndexOf(args,"--diagnostico")>=0) r["diagnostico"]=Diagnostics.Write();
    r["dados_arquivos"]=Directory.Exists(Paths.Data)?Directory.GetFiles(Paths.Data,"*",SearchOption.AllDirectories).Select(f=>f.Substring(Paths.Root.Length+1)).ToArray():new string[0];
   } catch(Exception e){r["erro"]=e.ToString();fail=2;}
   r["registro"]=log;r["resultado"]=fail==0?"PASS":"FAIL";
   Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(outFile)));
   File.WriteAllText(outFile,Util.Json.Serialize(r),new UTF8Encoding(false));
   return fail;
  }
 }
}
