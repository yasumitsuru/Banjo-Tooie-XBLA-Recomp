# Decisões e respostas da pessoa (registro)

Respostas dadas pela pessoa dona do jogo, em 2026-11-10, às pendências abertas depois da Missão 01.
Elas são **relatos e decisões da pessoa**: valem como orientação do projeto. Onde ainda não houve
teste automatizado, isso está dito ao lado. Nenhuma missão nova foi iniciada por causa delas.

| # | Pergunta (pendência) | Resposta da pessoa (literal) | Como fica no projeto |
|---|---|---|---|
| 1 | O port passa da abertura para o menu, a seleção de save, a introdução e o jogo, com controle/teclado, e grava/carrega saves? (`unk_68518783`) | "está carregando sim" | **Relatado pela pessoa** que o port carrega. No teste automatizado (Missão 01) só a abertura foi observada, sem entrada. Menu, jogo, controle e saves continuam **sem teste automatizado**. A resposta não deixa claro se saves foram gravados e carregados. Status: PARCIAL (relato) |
| 2 | A tradução PT-BR cobre todo o texto? "Menu START" é o menu do jogo ou um menu de opções do port? (`unk_bd24ad9e`) | "O game está com a tradução aplicada" | Tradução **confirmada pela pessoa** como aplicada nos dados; o port não precisa traduzir texto do jogo. O significado de **"menu START" ainda não foi respondido** e continua em aberto (só importa na missão de QoL) |
| 3 | Resolução e taxa originais; como chegar a 4K/60 (`unk_f626b246`) | "está em 720p/30 utilize o Sistema gráfico do ReXGlue, derivado do Xenia" | Original = **1280×720 a 30 FPS**: confirmado pela pessoa e pelas medições da Missão 01 (captura 1280×720; 29,45/29,61 FPS). **Decisão:** a resolução 4K deve usar o sistema gráfico do ReXGlue (`resolution_scale` / `resolution=4k` / FSR, auditoria §3.5), não mudanças no código de render do jogo. **Ressalva técnica (evidência):** o runtime não tem opção de taxa de quadros (só vsync), então os 60 FPS ainda exigem trocar o intervalo de apresentação do jogo (`0x82276E34`) e as constantes de tempo (MECHANICS §6); isso fica para a missão de 4K/60 |
| 4 | Uso pessoal ou distribuição? (a base não declara licença) (`unk_b00ea24b`) | "irei usar só pra uso pessoal." | **Uso pessoal**. Manter os arquivos copiados da base é aceitável nesse uso; **não publicar** o projeto nem binários. Se isso mudar, pedir permissão ao autor da base ou reescrever os arquivos copiados |
| 5 | Versão completa ou demo? (`unk_ddf84578`) | "versão completa" | **Versão completa**, confirmada pela pessoa. Coerente com o runtime, que informa a licença como comprada (fork `035aa25`, `xam_content.cpp`) |

## Ainda em aberto

- Significado de **"menu START"** (menu do próprio jogo ou um menu de opções do port).
- **Teste automatizado** de menu, jogo, controle e saves, com entrada simulada. Hoje há o relato da
  pessoa, mas nenhuma evidência gravada pelo projeto.
