# Decisões e respostas da pessoa (registro)

Respostas dadas pela pessoa dona do jogo, em 2026-11-10, às pendências abertas depois da Missão 01,
em duas rodadas. Elas são **relatos e decisões da pessoa** e valem como orientação do projeto. Onde
o projeto ainda não tem teste automatizado próprio, isso está dito ao lado. Nenhuma missão nova foi
iniciada por causa delas.

| # | Pergunta (pendência) | Resposta da pessoa (literal) | Como fica no projeto |
|---|---|---|---|
| 1 | O port passa da abertura para o menu, a seleção de save, a introdução e o jogo, com controle/teclado, e grava/carrega saves? (`unk_68518783`) | 1ª rodada: "está carregando sim". **2ª rodada: "sim está funcionando normalmente"** | **Funciona, segundo o uso real da pessoa**: menu, seleção de save, introdução, jogo, controle/teclado e gravar/carregar saves. O teste automatizado do projeto (Missão 01) cobre só até a abertura, sem entrada; um teste com entrada simulada continua sendo melhoria futura, não um bloqueio |
| 2 | A tradução PT-BR cobre todo o texto? "Menu START" é o menu do jogo ou um menu de opções do port? (`unk_bd24ad9e`) | 1ª rodada: "O game está com a tradução aplicada". **2ª rodada: "e o menu do próprio jogo."** | Tradução **aplicada nos dados**, segundo a pessoa; o port não traduz texto do jogo. **"Menu START" = o menu do próprio jogo** (o que abre com START). **Não é preciso criar um menu de opções do port**; o requisito é preservar o menu original funcionando, e a pessoa relata que funciona (nº 1). Os ajustes técnicos do runtime continuam no F4 |
| 3 | Resolução e taxa originais; como chegar a 4K/60 (`unk_f626b246`) | "está em 720p/30 utilize o Sistema gráfico do ReXGlue, derivado do Xenia" | Original = **1280×720 a 30 FPS**: confirmado pela pessoa e pelas medições da Missão 01 (captura 1280×720; 29,45–29,63 FPS). **Decisão:** a resolução 4K deve usar o sistema gráfico do ReXGlue (`resolution_scale` / `resolution=4k` / FSR, auditoria §3.5), não mudanças no código de render do jogo. **Ressalva técnica (evidência):** o runtime não tem opção de taxa de quadros (só vsync), então os 60 FPS ainda exigem trocar o intervalo de apresentação do jogo (`0x82276E34`) e as constantes de tempo (MECHANICS §6); isso fica para a missão de 4K/60 |
| 4 | Uso pessoal ou distribuição? (a base não declara licença) (`unk_b00ea24b`) | "irei usar só pra uso pessoal." | **Uso pessoal**. Manter os arquivos copiados da base é aceitável nesse uso; **não publicar** o projeto nem binários. Se isso mudar, pedir permissão ao autor da base ou reescrever os arquivos copiados |
| 5 | Versão completa ou demo? (`unk_ddf84578`) | "versão completa" | **Versão completa**, confirmada pela pessoa. Coerente com o runtime, que informa a licença como comprada (fork `035aa25`, `xam_content.cpp`) |

## Ainda em aberto

Nenhuma pendência de decisão. Melhorias futuras, fora desta missão e não bloqueantes:

- teste automatizado com entrada simulada (menu, jogo e saves) para ter evidência gravada pelo
  próprio projeto, além do relato da pessoa;
- 4K/60, draw distance e QoL nas próximas missões.
