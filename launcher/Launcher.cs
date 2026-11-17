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
    L.Init(args);
    if(args.Length>=2&&args[0]=="--teste") return TestMode.Run(args);
    var app=new Application{ShutdownMode=ShutdownMode.OnMainWindowClose};
    var c=new Controller();
    if(args.Length>=2&&args[0]=="--captura-ui") return c.Capture(app,args[1],TestMode.Arg(args,"--pagina")??"jogar",TestMode.Arg(args,"--tamanho"));
    if(args.Length>=2&&args[0]=="--textos-ui") {
     // --trocar-para pt|en: usa o seletor de idioma da janela (o mesmo caminho do clique) e lista
     // os textos da janela nova.
     string to=TestMode.Arg(args,"--trocar-para");
     if(to!=null){c.SelectLanguage(to);return Controller.Current.DumpTexts(args[1]);}
     return c.DumpTexts(args[1]);
    }
    app.Run(c.Window);
    return 0;
   } catch(Exception e) {
    try {Directory.CreateDirectory(Paths.LauncherLogs);File.AppendAllText(Path.Combine(Paths.LauncherLogs,"launcher.log"),DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss")+" ERRO FATAL "+e+Environment.NewLine);} catch {}
    if(args.Length==0) MessageBox.Show(L.T("O launcher encontrou um erro e vai fechar:\n\n","The launcher hit an error and will close:\n\n")+e.Message,Paths.AppName,MessageBoxButton.OK,MessageBoxImage.Error);
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
  readonly Dictionary<StackPanel,List<FrameworkElement>> hostCards=new Dictionary<StackPanel,List<FrameworkElement>>();
  Process running;
  bool loading;
  internal T F<T>(string name) where T:class {return Window.FindName(name) as T;}

  public Controller() {
   using(var s=Res.Open("Launcher.xaml")) Window=(Window)XamlReader.Load(s);
   Window.Title=Paths.AppName;
   L.Apply(Window);
   try {using(var ico=Res.Open("banjotooie.ico")) Window.Icon=BitmapFrame.Create(ico,BitmapCreateOptions.None,BitmapCacheOption.OnLoad);} catch {}
   var art=Res.Image("banjotooie.png");F<Image>("HeroArt").Source=art;F<Image>("SideLogo").Source=art;
   F<TextBlock>("VersionText").Text=L.T("Launcher portátil ","Portable launcher ")+Paths.Version;
   SetupLanguage();
   SetupFit();
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
   Click("PresetOriginal",()=>Preset(L.T("Original","Original"),new Dictionary<string,string>{{"fps60","false"},{"internal_scale","1"},{"draw_distance","Original"},{"swap_post_effect","none"},{"native_2x_msaa","false"},{"anisotropic_override","-1"}}));
   Click("PresetBalanced",()=>Preset(L.T("Equilibrado","Balanced"),new Dictionary<string,string>{{"fps60","true"},{"lock60","true"},{"internal_scale","2"},{"draw_distance","Original"},{"swap_post_effect","fxaa"},{"native_2x_msaa","false"},{"anisotropic_override","4"}}));
   Click("PresetMax",()=>Preset(L.T("4K máximo","4K maximum"),new Dictionary<string,string>{{"fps60","true"},{"lock60","true"},{"resolution","4k"},{"internal_scale","3"},{"draw_distance","Longe"},{"swap_post_effect","none"},{"native_2x_msaa","true"},{"anisotropic_override","5"},{"host_vsync","true"}}));
   Click("PrepareShaders",PrepareShaders);
   Click("OpenSaves",()=>OpenFolder(Paths.Data));Click("OpenBackups",()=>OpenFolder(Paths.Backups));
   Click("BackupSaves",ExportSaves);Click("ImportZip",ImportZip);Click("ImportDocs",ImportDocs);
   Click("Diagnose",Diagnose);Click("CopyLogs",()=>{try{Clipboard.SetText(F<TextBox>("LogText").Text);Status(L.T("Registro copiado.","Log copied."));}catch(Exception e){Status(L.T("Não foi possível copiar: ","Could not copy: ")+e.Message);}});
   Click("OpenLogs",()=>OpenFolder(Paths.SessionLogs));Click("OpenLauncherLogs",()=>OpenFolder(Paths.LauncherLogs));Click("OpenCrashes",()=>OpenFolder(Paths.Crashes));
   Click("OpenLicenses",()=>{string l=Path.Combine(Paths.Root,"LICENCAS");OpenFolder(Directory.Exists(l)?l:Paths.Root);});
   F<TextBox>("SourcePath").TextChanged+=delegate{if(!loading)F<Button>("PrepareButton").IsEnabled=true;};
   Window.Closing+=(s,e)=>{
    if(dirty.Count>0&&MessageBox.Show(Window,L.T("Há opções alteradas que ainda não foram salvas. Fechar mesmo assim?","Some changed options have not been saved yet. Close anyway?"),Paths.AppName,MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes) e.Cancel=true;
   };
   RefreshHome();RefreshSaves();RefreshShaders();
   Log("Launcher "+Paths.Version+L.T(" aberto em "," opened in ")+Paths.Root+" ("+L.T("idioma: português","language: English")+")");
   Current=this;
  }

  // ---------- tamanho da janela ----------
  // Abaixo do tamanho do desenho (DesignW x DesignH, em unidades da tela) a janela inteira é
  // reduzida em escala, sem barra de rolagem na página Jogar; acima dele, a arte da página Jogar
  // cresce e as páginas de opções passam a duas colunas.
  const double DesignW=1120,DesignH=860,TwoColumns=1080;
  double scale=1;
  FrameworkElement client;
  void SetupFit() {
   var root=F<Grid>("RootGrid");
   root.Loaded+=delegate{
    client=VisualTreeHelper.GetParent(root) as FrameworkElement;
    if(client!=null){client.SizeChanged+=delegate{Fit();};Fit();}
   };
  }
  void Fit() {
   if(client==null||client.ActualWidth<=0||client.ActualHeight<=0)return;
   double s=Math.Min(1.0,Math.Min(client.ActualWidth/DesignW,client.ActualHeight/DesignH));
   if(Math.Abs(s-scale)<0.002)return;
   scale=s;F<Grid>("RootGrid").LayoutTransform=s<0.999?new ScaleTransform(s,s):Transform.Identity;
  }
  // Para os testes: escala, tamanho útil e se a página Jogar coube sem cortar.
  public Dictionary<string,object> LayoutReport() {
   var home=F<Grid>("PageHome");var info=F<Border>("HomeInfoBox");
   var r=new Dictionary<string,object>{{"escala",Math.Round(scale,3)},{"cliente",client==null?"":Math.Round(client.ActualWidth)+"x"+Math.Round(client.ActualHeight)},
    {"pagina_jogar",Math.Round(home.ActualWidth)+"x"+Math.Round(home.ActualHeight)}};
   if(home.IsVisible) {
    // a última caixa da página precisa terminar dentro da página
    var pt=info.TranslatePoint(new Point(0,info.ActualHeight),home);
    r["jogar_cabe"]=pt.Y<=home.ActualHeight+0.5;r["jogar_fim_conteudo"]=Math.Round(pt.Y);
    var card=F<Button>("PlayButton");var pb=card.TranslatePoint(new Point(0,card.ActualHeight),home);r["botao_jogar_visivel"]=pb.Y>0&&pb.Y<=home.ActualHeight;
   }
   foreach(var kv in hostCards) r["colunas_"+kv.Key.Name]=kv.Key.Children.OfType<Grid>().Any(g=>g.ColumnDefinitions.Count==3)?2:1;
   return r;
  }

  // ---------- idioma ----------
  // O seletor da barra lateral grava "idioma" no config\launcher.ini e reabre a janela no outro
  // idioma (no mesmo lugar e na mesma página); as opções salvas e o jogo não mudam.
  public static Controller Current;
  public void SelectLanguage(string code) {
   var box=F<ComboBox>("LanguageBox");
   foreach(ComboBoxItem it in box.Items) if((string)it.Tag==code) box.SelectedItem=it;
  }
  void SetupLanguage() {
   var box=F<ComboBox>("LanguageBox");
   foreach(ComboBoxItem it in box.Items) if((string)it.Tag==L.Code) box.SelectedItem=it;
   box.SelectionChanged+=delegate{
    var it=box.SelectedItem as ComboBoxItem;if(it==null)return;string code=(string)it.Tag;
    if(code==L.Code)return;
    Action revert=()=>{foreach(ComboBoxItem x in box.Items) if((string)x.Tag==L.Code) box.SelectedItem=x;};
    if(running!=null||Util.GameRunning()){Status(L.T("Feche o jogo antes de trocar o idioma do launcher.","Close the game before changing the launcher language."));Window.Dispatcher.BeginInvoke(revert);return;}
    if(dirty.Count>0&&MessageBox.Show(Window,L.T("Há opções alteradas que ainda não foram salvas e serão descartadas ao trocar o idioma. Continuar?","Some changed options have not been saved yet and will be discarded when switching the language. Continue?"),Paths.AppName,MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes){Window.Dispatcher.BeginInvoke(revert);return;}
    try {L.Persist(code);} catch(Exception e){Fail(e);Window.Dispatcher.BeginInvoke(revert);return;}
    L.En=code=="en";Settings.Rebuild();
    string page=CurrentPage();var old=Window;
    var next=new Controller();
    next.Window.WindowStartupLocation=WindowStartupLocation.Manual;
    next.Window.Left=old.Left;next.Window.Top=old.Top;next.Window.Width=old.Width;next.Window.Height=old.Height;next.Window.WindowState=old.WindowState;
    next.ShowPage(page);
    if(!String.IsNullOrEmpty(F<TextBox>("SourcePath").Text))next.F<TextBox>("SourcePath").Text=F<TextBox>("SourcePath").Text;
    if(Application.Current!=null)Application.Current.MainWindow=next.Window;
    next.Window.Show();
    dirty.Clear();old.Close();
   };
  }
  string CurrentPage() {
   foreach(var kv in new Dictionary<string,string>{{"NavGraphics","graficos"},{"NavControls","controles"},{"NavGame","jogo"},{"NavSaves","saves"},{"NavLogs","registro"},{"NavCredits","creditos"}})
    if(F<RadioButton>(kv.Key).IsChecked==true)return kv.Value;
   return "jogar";
  }

  // ---------- utilidades da interface ----------
  void Ui(Action a) {if(Window.Dispatcher.CheckAccess())a();else Window.Dispatcher.BeginInvoke(a);}
  void Click(string name,Action a) {var b=F<Button>(name);if(b!=null)b.Click+=delegate{try{a();}catch(Exception e){Fail(e);}};}
  void Status(string text) {Ui(()=>F<TextBlock>("StatusText").Text=text);}
  void Fail(Exception e) {Log(L.T("ERRO: ","ERROR: ")+e.Message);Status(e.Message);MessageBox.Show(Window,e.Message,Paths.AppName,MessageBoxButton.OK,MessageBoxImage.Warning);}
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
   var labels=new List<string>{L.T("Automático (onde está o mouse ao abrir)","Automatic (where the mouse is at launch)")};var values=new List<string>{""};
   foreach(var m in Displays.List()){labels.Add(m[1]);values.Add(m[0]);}
   mon.Labels=labels.ToArray();mon.Values=values.ToArray();
   foreach(var group in Settings.All.GroupBy(g=>g.Key)) {
    var parts=group.Key.Split('|');string page=parts[0];
    var host=F<StackPanel>(page=="Gráficos"?"GraphicsGroups":page=="Controles"?"ControlGroups":"GameGroups");
    var card=new Border{Style=(Style)Window.FindResource("Card"),Margin=new Thickness(0,0,0,18)};
    var stack=new StackPanel();card.Child=stack;
    stack.Children.Add(new TextBlock{Text=L.Group(parts[1]),FontWeight=FontWeights.Bold,Foreground=(Brush)new BrushConverter().ConvertFrom("#F5B83D"),Margin=new Thickness(0,0,0,12)});
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
      g.ColumnDefinitions.Add(new ColumnDefinition());
      g.ColumnDefinitions.Add(twoCols?new ColumnDefinition{Width=new GridLength(190)}:new ColumnDefinition{Width=new GridLength(1,GridUnitType.Star),MinWidth=230,MaxWidth=380});
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
    if(parts[1]=="TELA") stack.Children.Add(new TextBlock{Text=L.T("O jogo desenha a imagem em 1280 × 720 vezes a escala interna e entrega na resolução de saída. Em 1080p a imagem interna 1440p é reduzida na apresentação (mais nitidez). 4K real = saída 4K com imagem interna 4K.","The game draws the image at 1280 × 720 times the internal scale and presents it at the output resolution. At 1080p the 1440p internal image is scaled down when presented (sharper). Real 4K = 4K output with a 4K internal image."),Style=(Style)Window.FindResource("Hint"),FontSize=11,Margin=new Thickness(0,10,0,0)});
    if(parts[1]=="QUADROS POR SEGUNDO") stack.Children.Add(new TextBlock{Text=L.T("60 FPS usa a lógica de 60 quadros do port (patch da comunidade do Xenia aplicado no código). 30 FPS é o ritmo original do Xbox 360.","60 FPS uses the port's 60-frame logic (the Xenia community patch applied in code). 30 FPS is the original Xbox 360 rate."),Style=(Style)Window.FindResource("Hint"),FontSize=11,Margin=new Thickness(0,10,0,0)});
    if(parts[1]=="TECLADO") stack.Children.Add(new TextBlock{Text=L.T("F1 abre e fecha as Configurações de PC dentro do jogo. Duas ações na mesma tecla funcionam ao mesmo tempo.","F1 opens and closes the PC Settings inside the game. Two actions on the same key work at the same time."),Style=(Style)Window.FindResource("Hint"),FontSize=11,Margin=new Thickness(0,10,0,0)});
    card.Tag=twoCols?"largo":null;  // o teclado (duas colunas por dentro) ocupa a largura toda
    List<FrameworkElement> list;if(!hostCards.TryGetValue(host,out list)){list=new List<FrameworkElement>();hostCards[host]=list;host.SizeChanged+=delegate{ArrangeCards(host);};}
    list.Add(card);host.Children.Add(card);
   }
  }
  // Uma coluna em janelas estreitas; duas (cada cartão na coluna mais curta) nas largas.
  void ArrangeCards(StackPanel host) {
   bool two=host.ActualWidth>=TwoColumns;
   if(host.Tag is bool&&(bool)host.Tag==two)return;
   host.Tag=two;
   var cards=hostCards[host];
   foreach(var c in cards){var p=c.Parent as Panel;if(p!=null)p.Children.Remove(c);}
   host.Children.Clear();
   if(!two){foreach(var c in cards){c.Margin=new Thickness(0,0,0,18);host.Children.Add(c);}return;}
   var grid=new Grid();
   grid.ColumnDefinitions.Add(new ColumnDefinition());grid.ColumnDefinitions.Add(new ColumnDefinition{Width=new GridLength(18)});grid.ColumnDefinitions.Add(new ColumnDefinition());
   var left=new StackPanel();var right=new StackPanel();Grid.SetColumn(right,2);
   grid.Children.Add(left);grid.Children.Add(right);host.Children.Add(grid);
   double colW=(host.ActualWidth-18)/2,hl=0,hr=0;
   foreach(var c in cards) {
    c.Margin=new Thickness(0,0,0,18);
    if((c.Tag as string)=="largo"){host.Children.Add(c);continue;}
    c.Measure(new Size(colW,double.PositiveInfinity));double h=c.DesiredSize.Height;
    if(hl<=hr){left.Children.Add(c);hl+=h;}else{right.Children.Add(c);hr+=h;}
   }
  }
  void Changed(Option o) {
   if(loading)return;string p;if(pageOf.TryGetValue(o.Key,out p))dirty.Add(p);
   if(p=="Gráficos")F<TextBlock>("PresetLabel").Text=L.T("Perfil: personalizado (não salvo)","Profile: custom (not saved)");
   Status(L.T("Opções alteradas: clique em SALVAR CONFIGURAÇÕES.","Options changed: click SAVE SETTINGS."));
  }
  void SetControl(string key,string value) {
   Control c;if(!controls.TryGetValue(key,out c))return;
   var cb=c as CheckBox;if(cb!=null){cb.IsChecked=value=="true";return;}
   var combo=(ComboBox)c;
   foreach(ComboBoxItem it in combo.Items) if((string)it.Tag==value){combo.SelectedItem=it;return;}
   // Valor válido para o jogo mas fora da lista (ex.: escolhido no menu F1): mostrado como está.
   var extra=new ComboBoxItem{Content=L.T("Atual: ","Current: ")+value,Tag=value};combo.Items.Add(extra);combo.SelectedItem=extra;
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
   Log(L.T("Configurações salvas (","Settings saved (")+PageName(page)+"): "+values.Count+L.T(" opções em "," options in ")+Paths.Store(Paths.PcSettings)+(values.Keys.Any(k=>Settings.All.Any(g=>g.Value.Key==k&&g.Value.File=="rt"))?L.T(" e "," and ")+Paths.Store(Paths.RuntimeToml):"")+".");
   Status(L.T("Configurações salvas. Valem na próxima vez que o jogo abrir.","Settings saved. They apply the next time the game starts."));
   if(page=="Gráficos")F<TextBlock>("PresetLabel").Text=L.T("Perfil: configurações atuais (salvas)","Profile: current settings (saved)");
  }
  static string PageName(string page) {return page=="Gráficos"?L.T("Gráficos","Graphics"):page=="Controles"?L.T("Controles","Controls"):L.T("Áudio e jogo","Audio & game");}
  void Defaults(string page) {
   foreach(var g in Settings.All) if(g.Key.StartsWith(page+"|")) SetControl(g.Value.Key,g.Value.Default);
   dirty.Add(page);Status(L.T("Padrões do port carregados: clique em SALVAR CONFIGURAÇÕES para gravar.","Port defaults loaded: click SAVE SETTINGS to store them."));
  }
  void Preset(string name,Dictionary<string,string> values) {
   foreach(var kv in values)SetControl(kv.Key,kv.Value);
   dirty.Add("Gráficos");F<TextBlock>("PresetLabel").Text=L.T("Perfil: ","Profile: ")+name+L.T(" (não salvo)"," (not saved)");
   Status(L.T("Perfil ","Profile ")+name+L.T(" carregado: clique em SALVAR CONFIGURAÇÕES."," loaded: click SAVE SETTINGS."));
  }

  // ---------- jogo ----------
  void RefreshHome() {
   loading=true;
   try {
    bool ready=Game.Prepared();string stored=Game.StoredFolder();
    var src=F<TextBox>("SourcePath");
    if(String.IsNullOrEmpty(src.Text)) {
     if(stored!=null)src.Text=stored;
     else {string found=AutoDetect();if(found!=null){src.Text=found;F<TextBlock>("SourceCheck").Text=L.T("Encontrei uma pasta do jogo aqui perto. Clique em Conferir ou em PREPARAR MEU JOGO.","Found a game folder nearby. Click Check or PREPARE MY GAME.");}}
    }
    F<Button>("PlayButton").IsEnabled=ready&&running==null;
    F<TextBlock>("InstallStatus").Text=running!=null?L.T("O jogo está aberto","The game is running"):ready?L.T("Pronto para jogar","Ready to play"):L.T("Prepare o seu jogo","Prepare your game");
    var ini=new KeyValueFile(Paths.Config);string code=ini.Get("versao"),old=ini.Get("versao_jogo");
    // launcher.ini de antes do inglês: só tinha o nome em português.
    if(code==null&&old!=null)code=old.StartsWith("PT-BR")?"ptbr":old.StartsWith("Global")?"global":old.StartsWith("Modificada")?"modificada":null;
    string ver=code!=null?Game.VersionName(code):old;
    string where=stored!=null&&String.Equals(stored.TrimEnd('\\'),Paths.LocalGame,StringComparison.OrdinalIgnoreCase)?L.T("cópia portátil na pasta jogo\\","portable copy in the jogo\\ folder"):stored;
    F<TextBlock>("HomeActionHint").Text=ready?L.T("Jogo: ","Game: ")+where+(ver!=null?L.T(" · versão "," · version ")+ver:"")+L.T(". F1 dentro do jogo abre as Configurações de PC.",". F1 inside the game opens the PC Settings."):L.T("Escolha abaixo a pasta do seu Banjo-Tooie XBLA descompactado e clique em PREPARAR MEU JOGO.","Choose your extracted Banjo-Tooie XBLA folder below and click PREPARE MY GAME.");
    if(ready&&running==null)Status(L.T("Pronto. Clique em JOGAR.","Ready. Click PLAY."));
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
   using(var dlg=new System.Windows.Forms.FolderBrowserDialog{Description=L.T("Escolha a pasta do Banjo-Tooie XBLA descompactado (a que tem default.xex e RAWFiles).","Choose the extracted Banjo-Tooie XBLA folder (the one with default.xex and RAWFiles)."),ShowNewFolderButton=false}) {
    var cur=F<TextBox>("SourcePath").Text;if(Directory.Exists(cur))dlg.SelectedPath=cur;
    if(dlg.ShowDialog()==System.Windows.Forms.DialogResult.OK){F<TextBox>("SourcePath").Text=dlg.SelectedPath;CheckSource();}
   }
  }
  void CheckSource() {
   string folder=F<TextBox>("SourcePath").Text.Trim();GameCheck r=null;
   Log(L.T("Conferindo ","Checking ")+folder+"…");
   Busy(L.T("Conferindo os arquivos do jogo…","Checking the game files…"),()=>{r=Game.Check(folder);},()=>{
    F<TextBlock>("SourceCheck").Text=(r.Ok?"✔ ":"✖ ")+r.Message;
    F<TextBlock>("SourceCheck").Foreground=(Brush)new BrushConverter().ConvertFrom(r.Ok?"#9FE2A0":"#FF9C7A");
    Log(r.Message);foreach(var d in r.Details)Log("  "+d);
    Status(r.Ok?L.T("Pasta reconhecida. Clique em PREPARAR MEU JOGO.","Folder recognized. Click PREPARE MY GAME."):L.T("A pasta não serve: veja a mensagem.","This folder cannot be used: see the message."));
   });
  }
  void Prepare() {
   string folder=F<TextBox>("SourcePath").Text.Trim();bool copy=F<CheckBox>("CopyGame").IsChecked==true;
   Busy(L.T("Preparando…","Preparing…"),()=>Game.Prepare(folder,copy),()=>{
    F<TextBlock>("SourceCheck").Text=L.T("✔ Jogo preparado. Os arquivos originais não foram alterados.","✔ Game prepared. The original files were not changed.");
    F<TextBlock>("SourceCheck").Foreground=(Brush)new BrushConverter().ConvertFrom("#9FE2A0");
    if(copy)F<TextBox>("SourcePath").Text=Paths.LocalGame;
    LoadOptions();RefreshShaders();Status(L.T("Jogo preparado. Clique em JOGAR.","Game prepared. Click PLAY."));
   });
  }
  void Play() {
   if(Util.GameRunning()){Status(L.T("O jogo já está aberto.","The game is already running."));return;}
   foreach(var p in dirty.ToList())SavePage(p);
   running=Game.Launch("");
   Log(L.T("Jogo aberto (processo ","Game started (process ")+running.Id+"): "+Paths.Store(Paths.GameExe)+" "+Game.Args(Game.StoredFolder()));
   RefreshHome();Status(L.T("Jogo aberto. F1 abre as Configurações de PC dentro do jogo.","Game started. F1 opens the PC Settings inside the game."));
   Window.WindowState=WindowState.Minimized;
   var proc=running;
   Task.Run(()=>{proc.WaitForExit();int code=proc.ExitCode;Ui(()=>{
    running=null;Log(L.T("Jogo fechado (código ","Game closed (code ")+code+").");
    Window.WindowState=WindowState.Normal;Window.Activate();RefreshHome();RefreshSaves();LoadOptions();
    if(code!=0){Status(L.T("O jogo fechou com erro (código ","The game closed with an error (code ")+code+L.T("). Veja a página Diagnóstico.","). See the Diagnostics page."));
     MessageBox.Show(Window,L.T("O jogo fechou com o código ","The game closed with code ")+code+L.T(".\n\nOs detalhes estão em dados\\logs e, se houve falha, em dados\\crashes. Na página Diagnóstico, \"Gerar diagnóstico\" junta tudo num arquivo de texto.",".\n\nThe details are in dados\\logs and, if it crashed, in dados\\crashes. On the Diagnostics page, \"Create diagnostics report\" gathers everything into a text file."),Paths.AppName,MessageBoxButton.OK,MessageBoxImage.Warning);}
   });});
  }
  void RefreshShaders() {
   int n=Directory.Exists(Paths.Cache)?Directory.GetFiles(Paths.Cache,"*",SearchOption.AllDirectories).Length:0;
   long size=n==0?0:Directory.GetFiles(Paths.Cache,"*",SearchOption.AllDirectories).Sum(f=>new FileInfo(f).Length);
   F<TextBlock>("ShaderStatus").Text=n==0?L.T("O cache de shaders desta pasta ainda está vazio: o jogo cria os shaders na primeira passagem por cada lugar.","This folder's shader cache is still empty: the game creates the shaders the first time it passes through each place."):L.T("Cache de shaders: ","Shader cache: ")+n+L.T(" arquivos ("," files (")+(size/1048576.0).ToString("0.0",L.Culture)+L.T(" MB) em cache\\."," MB) in cache\\.");
   F<Button>("PrepareShaders").IsEnabled=n>0&&Game.Prepared();
  }
  void PrepareShaders() {
   if(Util.GameRunning()){Status(L.T("Feche o jogo antes.","Close the game first."));return;}
   string msg=null;Busy(L.T("Preparando os shaders guardados (pode levar alguns minutos)…","Preparing the saved shaders (this may take a few minutes)…"),()=>{msg=Game.PrepareShaders();},()=>{Log(msg);Status(msg);RefreshShaders();});
  }

  // ---------- saves ----------
  void RefreshSaves() {
   string save=Saves.Save;
   F<TextBlock>("SavePath").Text=L.T("Pasta: ","Folder: ")+Paths.Data;
   var b=Saves.Backups();
   F<TextBlock>("SaveSummary").Text=(save==null?L.T("Ainda não há save nesta pasta (ele aparece depois da primeira gravação no jogo).","There is no save in this folder yet (it appears after the first save in the game)."):"Save: "+new FileInfo(save).Length+L.T(" bytes, gravado em "," bytes, written on ")+L.Date(new FileInfo(save).LastWriteTime))+
    L.T("\nBackups automáticos: ","\nAutomatic backups: ")+b.Length+(b.Length>0?L.T(" (o mais recente de "," (latest from ")+L.Date(b.Last().LastWriteTime)+")":"");
   string docs=Path.Combine(Saves.DocumentsSaves,Paths.SaveRel);
   bool has=File.Exists(docs);
   F<TextBlock>("DocsSummary").Text=has?L.T("Encontrado um save em Documentos\\My Games\\Banjo-Tooie XBLA (gravado em ","Found a save in Documents\\My Games\\Banjo-Tooie XBLA (written on ")+L.Date(new FileInfo(docs).LastWriteTime)+L.T("). É o save do port aberto sem este launcher.","). It is the save of the port started without this launcher."):L.T("Nenhum save em Documentos\\My Games\\Banjo-Tooie XBLA.","No save in Documents\\My Games\\Banjo-Tooie XBLA.");
   F<Button>("ImportDocs").IsEnabled=has;
  }
  void ExportSaves() {
   var dlg=new Microsoft.Win32.SaveFileDialog{Title=L.T("Exportar backup dos dados","Export data backup"),FileName="banjotooie-dados-"+DateTime.Now.ToString("yyyyMMdd-HHmm")+".zip",Filter=L.T("Arquivo ZIP|*.zip","ZIP file|*.zip"),InitialDirectory=Paths.Root};
   if(dlg.ShowDialog(Window)!=true)return;string dest=dlg.FileName;int n=0;
   Busy(L.T("Exportando…","Exporting…"),()=>{n=Saves.Export(dest);},()=>{Log(L.T("Backup exportado: ","Backup exported: ")+dest+" ("+n+L.T(" arquivos)."," files)."));Status(L.T("Backup exportado (","Backup exported (")+n+L.T(" arquivos)."," files)."));});
  }
  void ImportZip() {
   var dlg=new Microsoft.Win32.OpenFileDialog{Title=L.T("Importar backup dos dados","Import data backup"),Filter=L.T("Arquivo ZIP|*.zip","ZIP file|*.zip"),InitialDirectory=Paths.Root};
   if(dlg.ShowDialog(Window)!=true)return;
   if(MessageBox.Show(Window,L.T("Os dados atuais (saves e opções) serão substituídos pelos do arquivo escolhido. Antes disso eles são exportados para backups-launcher\\. Continuar?","The current data (saves and settings) will be replaced by the chosen file. Before that, it is exported to backups-launcher\\. Continue?"),Paths.AppName,MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes)return;
   string src=dlg.FileName,msg=null;
   Busy(L.T("Importando…","Importing…"),()=>{msg=Saves.ImportZip(src);},()=>{Log(msg);Status(msg);LoadOptions();RefreshSaves();});
  }
  void ImportDocs() {
   if(MessageBox.Show(Window,L.T("Copiar o save de Documentos\\My Games\\Banjo-Tooie XBLA para esta pasta portátil? O original continua onde está; se já houver um save aqui, ele vai antes para dados\\backups.","Copy the save from Documents\\My Games\\Banjo-Tooie XBLA to this portable folder? The original stays where it is; if there is already a save here, it is moved to dados\\backups first."),Paths.AppName,MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes)return;
   string msg=null;Busy(L.T("Copiando o save…","Copying the save…"),()=>{msg=Saves.ImportFromDocuments();},()=>{Log(msg);Status(L.T("Save copiado para a pasta portátil.","Save copied to the portable folder."));RefreshSaves();});
  }
  void Diagnose() {
   string file=null;Busy(L.T("Gerando diagnóstico…","Creating diagnostics report…"),()=>{file=Diagnostics.Write();},()=>{Log(L.T("Diagnóstico gravado em ","Diagnostics report written to ")+Paths.Store(file)+".");Status(L.T("Diagnóstico gravado.","Diagnostics report written."));Process.Start("explorer.exe","/select,"+Util.Quote(file));});
  }

  // Textos da interface (testes de tradução): todos os textos fixos e gerados das 7 páginas, sem
  // os campos de texto (caminhos e registro).
  public int DumpTexts(string json) {
   var texts=new List<string>();
   Action<object> walk=null;
   walk=o=>{
    if(o is TextBox) return;
    var tb=o as TextBlock;if(tb!=null&&!String.IsNullOrWhiteSpace(tb.Text)) texts.Add(tb.Text);
    var cc=o as ContentControl;if(cc!=null&&cc.Content is string&&!String.IsNullOrWhiteSpace((string)cc.Content)) texts.Add((string)cc.Content);
    var fe=o as FrameworkElement;if(fe!=null){string an=System.Windows.Automation.AutomationProperties.GetName(fe);if(!String.IsNullOrEmpty(an))texts.Add(an);}
    var d=o as DependencyObject;if(d!=null) foreach(object c in LogicalTreeHelper.GetChildren(d)) walk(c);
   };
   walk(Window);
   Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(json)));
   File.WriteAllText(json,Util.Json.Serialize(new Dictionary<string,object>{{"idioma",L.Code},{"textos",texts.Distinct().ToList()}}),new UTF8Encoding(false));
   return 0;
  }

  // Captura da interface (testes): desenha a janela fora da tela, grava o PNG e sai.
  public int Capture(Application app,string png,string page,string size) {
   Window.WindowStartupLocation=WindowStartupLocation.Manual;Window.Left=-30000;Window.Top=-30000;Window.ShowActivated=false;Window.ShowInTaskbar=false;
   if(size!=null){var p=size.Split('x');Window.MinWidth=0;Window.MinHeight=0;Window.Width=Double.Parse(p[0],CultureInfo.InvariantCulture);Window.Height=Double.Parse(p[1],CultureInfo.InvariantCulture);}
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
      File.WriteAllText(Path.ChangeExtension(png,".json"),Util.Json.Serialize(LayoutReport()),new UTF8Encoding(false));
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
 //   [--diagnostico] [--definir-idioma pt|en] [--idioma pt|en (só nesta execução)]
 // Cada etapa grava o que fez e o resultado num JSON (usado por tools/launcher/testar_launcher.py).
 static class TestMode {
  public static string Arg(string[] a,string name){int i=Array.IndexOf(a,name);return i>=0&&i+1<a.Length?a[i+1]:null;}
  public static int Run(string[] args) {
   var r=new Dictionary<string,object>();var log=new List<string>();
   Game.Log=s=>log.Add(s);
   string outFile=args[1];int fail=0;
   r["launcher"]=Paths.Version;r["raiz"]=Paths.Root;
   string setLang=Arg(args,"--definir-idioma");
   if(setLang=="pt"||setLang=="en"){L.Persist(setLang);r["idioma_gravado"]=L.Saved();}
   r["idioma"]=L.Code;
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
