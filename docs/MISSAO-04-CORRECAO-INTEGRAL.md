# MISSÃO 04 — Correção Integral, Estabilização e Validação Automatizada

Texto da pessoa, recebido em 2026-11-13 com o pedido "atualize o plano com o seguinte prompt".
O número 04 é a numeração interna do projeto (o texto original não traz número). O plano de
execução, com as etapas ligadas às pendências da auditoria de 2026-11-13, está em
[`PLANO.md`](PLANO.md).

---

# MISSÃO — Correção Integral, Estabilização e Validação Automatizada do Banjo-Tooie XBLA PT-BR Recompiled

## 1. Objetivo principal

Dar continuidade ao desenvolvimento do **Banjo-Tooie XBLA PT-BR Recompiled para Windows 11 x86-64**, corrigindo todas as falhas, limitações e pendências técnicas identificadas na auditoria mais recente.

A missão deve contemplar:

- Correção de defeitos existentes.
- Finalização de funcionalidades incompletas.
- Validação de funcionalidades já implementadas.
- Desenvolvimento de testes automatizados.
- Verificação da integridade da tradução PT-BR.
- Estabilização do jogo em resolução 4K e 60 FPS.
- Preservação do comportamento, da jogabilidade e dos recursos originais.
- Garantia de compatibilidade com controle físico.
- Testes de todos os mundos e situações relevantes.
- Homologação final com evidências verificáveis.

**O objetivo é entregar uma versão PT-BR funcional, estável, completa e tecnicamente validada, sem prejudicar o comportamento original de Banjo-Tooie XBLA.**

## 2. Escopo exclusivo: versão PT-BR

Esta missão deve trabalhar exclusivamente na versão brasileira do jogo.

Regras obrigatórias:

1. Localizar e identificar a versão PT-BR utilizada no projeto.
2. Preservar integralmente os recursos da tradução já aplicada.
3. Não substituir textos brasileiros pelos textos da versão global em inglês.
4. Não alterar os arquivos originais do Xbox 360 sem necessidade técnica comprovada.
5. Não modificar a versão global/inglesa como alvo desta missão.
6. Priorizar arquivos, configurações, executáveis e recursos específicos da versão PT-BR.
7. Quando uma correção exigir alteração em código compartilhado, avaliar e impedir regressões nas demais variantes.
8. Não reconstruir a tradução do zero.
9. Preservar caracteres especiais, acentuação, diálogos, interface e recursos traduzidos.
10. Utilizar a versão PT-BR como referência principal em todos os testes.

O escopo envolve o port nativo para PC, sua integração com o runtime e todas as funcionalidades previstas para a versão brasileira.

## 3. Autonomia e permissões

O agente está autorizado a:

- Inspecionar e modificar o código-fonte.
- Corrigir implementações existentes.
- Criar e modificar scripts de automação.
- Compilar e executar o projeto.
- Criar testes funcionais, gráficos e de regressão.
- Utilizar ferramentas de engenharia reversa.
- Inspecionar e modificar configurações necessárias.
- Produzir builds de desenvolvimento.
- Criar checkpoints e commits locais.
- Executar testes automatizados prolongados.
- Criar ambientes isolados de validação.
- Criar, copiar, editar e restaurar saves exclusivamente para testes.
- Consultar repositórios públicos e documentação técnica.
- Reutilizar soluções existentes quando compatíveis e permitidas.

Executar autonomamente as atividades dentro do escopo autorizado, sem solicitar aprovações intermediárias desnecessárias.

Não contornar restrições de segurança, não expor credenciais e não distribuir arquivos proprietários do jogo.

### Proteção obrigatória dos dados

Antes de qualquer operação potencialmente destrutiva:

1. Identificar os arquivos originais do jogo.
2. Identificar os saves reais do usuário.
3. Preservar cópias verificáveis dos arquivos envolvidos.
4. Criar um ambiente de testes separado.
5. Validar que operações de teste não escrevem nas pastas reais do usuário.
6. Registrar checkpoints que permitam rollback.
7. Não sobrescrever arquivos originais sem necessidade comprovada.

## 4. Autorização específica para edição de saves

**Está expressamente autorizada a edição de arquivos de salvamento do jogo para testes automatizados e validação.**

Essa autorização inclui:

- Criar saves artificiais de teste.
- Editar cópias de saves existentes.
- Desbloquear mundos.
- Desbloquear habilidades.
- Liberar personagens, áreas, eventos e colecionáveis necessários aos testes.
- Preparar saves em diferentes estágios de progressão.
- Criar saves com o jogo próximo de 100% de conclusão.
- Construir cenários de teste com estados específicos.
- Simular corrupção de saves exclusivamente em arquivos descartáveis.
- Restaurar estados anteriores após cada teste.

Se necessário, localizar saves avançados compatíveis disponibilizados publicamente na internet, verificando procedência, compatibilidade e integridade.

Não utilizar arquivos de procedência suspeita.

### Isolamento obrigatório

Nenhum save utilizado nos testes pode substituir o save pessoal do usuário.

Os testes devem utilizar diretórios isolados e reproduzíveis.

Se for necessário converter ou editar saves, preservar o arquivo de origem e registrar a transformação realizada.

## 5. Correção e segurança do sistema de salvamento

Investigar e corrigir integralmente os problemas relacionados a saves.

Requisitos:

1. Impedir que um save corrompido seja sobrescrito sem preservar uma cópia para diagnóstico.
2. Melhorar a validação de integridade dos dados.
3. Identificar estruturas e possíveis mecanismos de verificação do formato original.
4. Implementar proteção contra gravações interrompidas, quando tecnicamente viável.
5. Garantir backups automáticos confiáveis.
6. Impedir que backups inválidos eliminem todos os backups íntegros.
7. Permitir restauração confiável.
8. Permitir selecionar backups anteriores, quando aplicável.
9. Testar saves de início, meio e fim do jogo.
10. Testar saves modificados e avançados.
11. Validar a separação entre saves reais e saves de teste.
12. Evitar conflitos com sincronização de arquivos.
13. Garantir compatibilidade com os mecanismos originais de progressão.

Utilizar testes automatizados positivos e negativos para comprovar cada correção.

## 6. Resolução, renderização e apresentação

Validar e corrigir o sistema gráfico utilizado pelo port.

### Resoluções

Garantir suporte efetivo a:

- 1920 × 1080.
- 2560 × 1440.
- 3840 × 2160.

Verificar separadamente:

- Resolução interna.
- Resolução do framebuffer.
- Resolução de apresentação.
- Dimensões da janela.
- Resolução do monitor.

**Não confundir upscaling de uma imagem em baixa resolução com renderização interna na resolução selecionada.**

Investigar especialmente a situação em que 1080p está sendo renderizado internamente a 1280 × 720.

Encontrar uma solução tecnicamente adequada, evitando redução de qualidade desnecessária.

### Modos de exibição

Corrigir e validar:

- Tela cheia.
- Janela.
- Janela sem bordas.
- Alternância entre os modos.
- Detecção do monitor correto.
- Tratamento de múltiplos monitores.
- Escala DPI.
- VSync e Screen Tearing.

A janela sem bordas deve ocupar corretamente a área de exibição do monitor selecionado.

Executar uma matriz automatizada envolvendo as três resoluções e os três modos de exibição, incluindo testes de reabertura e persistência.

## 7. Estabilização de 60 FPS

Esta é uma das prioridades técnicas mais importantes.

O jogo original utiliza comportamentos que podem depender da frequência de atualização.

Portanto, alcançar 60 FPS no contador não é suficiente.

Investigar e validar:

1. Física do personagem.
2. Velocidade de movimentação.
3. Altura e duração dos pulos.
4. Colisões.
5. Natação.
6. Voo.
7. Animações.
8. Movimento da câmera.
9. Cronômetros.
10. Minijogos.
11. Inteligência artificial e inimigos.
12. Chefes.
13. Cutscenes.
14. Eventos dependentes de frames.
15. Sincronização de áudio e vídeo.
16. Transições entre mundos.

### Metodologia

Criar testes automatizados comparando o comportamento original a 30 FPS com a implementação a 60 FPS.

Utilizar entradas determinísticas, estados de jogo equivalentes e métricas reproduzíveis.

Se necessário, corrigir a temporização, constantes, hooks ou a arquitetura de atualização.

Não reescrever a lógica original desnecessariamente.

Priorizar soluções que preservem a física e a jogabilidade original.

Separar lógica, renderização e temporização somente quando as evidências demonstrarem necessidade e viabilidade técnica.

### Critério de aceitação

O jogo deve alcançar 60 FPS reais em condições compatíveis com o hardware de referência, sem acelerar indevidamente a jogabilidade.

As tolerâncias dos testes devem ser justificadas tecnicamente e não podem esconder diferenças perceptíveis ou problemas de progressão.

## 8. Draw Distance em todos os mundos

Validar e, quando necessário, corrigir os três perfis existentes:

- Original.
- Longe.
- Total.

Não implementar um ajuste manual.

Investigar os limites documentados para os diferentes mapas, incluindo os valores 10400, 12800, 21504 e 25600.

### Testes obrigatórios

Criar rotinas automatizadas para:

1. Entrar nos mundos.
2. Posicionar a câmera em pontos reproduzíveis.
3. Selecionar os três perfis.
4. Capturar imagens.
5. Verificar geometria.
6. Identificar desaparecimento indevido de objetos.
7. Detectar pop-in inesperado.
8. Avaliar transparências e efeitos.
9. Medir desempenho.
10. Verificar persistência.
11. Comparar os resultados entre diferentes mapas.

Utilizar saves avançados, edição de saves ou mecanismos internos de transporte entre mapas.

Não depender de o usuário concluir o jogo manualmente.

O perfil Total deve utilizar o maior alcance seguro tecnicamente validado.

## 9. Menu nativo CONFIGURAÇÕES DE PC

**A localização atual do menu foi solicitada e aprovada pelo usuário. Deve ser preservada.**

Fluxo correto:

START → AJUDA E OPÇÕES → CONFIGURAÇÕES DE PC.

Não mover a opção para a primeira tela do menu de pausa.

Requisitos:

- Preservar a interface original do jogo.
- Manter a entrada dentro de AJUDA E OPÇÕES.
- Preservar a navegação nativa.
- Não prejudicar as demais opções do menu.
- Manter categorias de gráficos, controles, áudio e jogo.
- Garantir funcionamento com controle.
- Validar abertura, navegação, seleção e retorno.
- Evitar dependência de overlays externos.
- Manter a interface compatível com a tradução PT-BR.

Realizar apenas os ajustes necessários para corrigir falhas e completar funcionalidades.

Não redesenhar o menu sem necessidade.

## 10. Controles físicos e remapeamento

Verificar e corrigir:

- Suporte a XInput e/ou SDL.
- Detecção de controle físico.
- Botões.
- Analógicos.
- Direcional digital.
- Gatilhos.
- Vibração.
- Zona morta.
- Sensibilidade.
- Inversão de eixos.
- Reconexão.
- Desconexão.
- Navegação nos menus.

Completar o remapeamento de:

- Teclado.
- Mouse.
- Gamepad.

Implementar tratamento de conflitos e restauração dos controles padrão.

### Validação

Automatizar o máximo possível por testes instrumentados e entradas simuladas.

Entretanto, não classificar o uso de um controle físico como validado apenas por meio de controle virtual.

Quando não houver hardware físico acessível, registrar essa limitação e manter o requisito como pendente de validação real.

## 11. Áudio e câmera

### Áudio

Corrigir e validar:

- Volume geral.
- Silenciamento.
- Volume da música.
- Volume dos efeitos sonoros.
- Aplicação das configurações.
- Persistência.
- Sincronização audiovisual.
- Ausência de cortes e distorções.

Investigar os controles de música e efeitos já existentes no jogo original antes de duplicar implementações.

### Câmera

Validar:

- Sensibilidade horizontal e vertical.
- Inversão de eixos.
- Suavização.
- Funcionamento com mouse.
- Funcionamento com controle.
- Compatibilidade com diferentes modos de câmera.
- Persistência das configurações.

Preservar o comportamento original quando os ajustes estiverem nos valores padrão.

## 12. Tradução PT-BR e interface

Verificar e corrigir:

1. Diálogos de todos os mundos.
2. Menus.
3. HUD.
4. Mensagens do sistema.
5. Acentuação.
6. Caracteres especiais.
7. Textos presentes em texturas.
8. Legibilidade em 1080p, 1440p e 4K.
9. Escala da interface.
10. Velocidade dos diálogos.
11. Posicionamento dos textos.
12. Possíveis cortes e sobreposições.
13. Consistência do idioma em toda a versão PT-BR.

Preservar integralmente a tradução fornecida pelo usuário.

Não substituir termos já traduzidos sem necessidade técnica.

## 13. Configurações e persistência

Corrigir a fragmentação das configurações entre arquivos associados ao executável e arquivos específicos da conta.

Objetivos:

- Persistência confiável.
- Separação dos saves.
- Ausência de conflitos entre valores duplicados.
- Compatibilidade com múltiplos usuários do Windows.
- Tratamento de configurações inválidas.
- Recuperação por backup.
- Migração entre versões.
- Restauração dos padrões.
- Aplicação correta após reiniciar o jogo.

Preservar as configurações existentes sempre que possível.

Não apagar preferências sem mecanismo de migração ou recuperação.

## 14. Cutscenes, início rápido e qualidade de vida

Finalizar e validar:

- Início rápido.
- Ignorar introduções opcionais.
- Pular cutscenes já assistidas.
- Preservação de eventos obrigatórios.
- Ausência de softlocks.
- Pausa automática ao minimizar.
- Pausa ao perder o foco.
- Retomada correta do jogo.
- Contador de FPS.
- Estatísticas opcionais de desempenho.
- Rastreador de colecionáveis por mundo.
- Persistência das preferências.

O rastreador deve ser confrontado automaticamente com os totais reais do jogo.

Verificar o comportamento da opção CONQUISTAS, atualmente associada a uma função não implementada do Xbox 360.

Implementar um tratamento compatível com o PC, preservando o restante do menu e evitando chamadas sem efeito ou comportamento enganoso.

## 15. Automação completa dos testes

Criar ou aprimorar uma infraestrutura automatizada capaz de validar o jogo com mínima intervenção humana.

O sistema deve conseguir:

1. Inicializar o jogo.
2. Selecionar a versão PT-BR.
3. Carregar saves de teste.
4. Navegar pelos menus.
5. Acessar diferentes mundos.
6. Posicionar o personagem.
7. Executar sequências de comandos.
8. Alterar configurações.
9. Capturar imagens.
10. Coletar métricas de FPS e tempo de quadro.
11. Registrar eventos e erros.
12. Comparar resultados esperados e observados.
13. Detectar travamentos.
14. Validar saves.
15. Restaurar o ambiente isolado.
16. Reexecutar testes de regressão.

Priorizar scripts reproduzíveis, determinísticos e independentes de interação humana.

Caso ferramentas ou sondas já existam no projeto, reutilizá-las e corrigi-las em vez de criar implementações redundantes.

### Testes por mundo

Criar cenários automatizados abrangendo o maior número possível de áreas relevantes, incluindo:

- Exploração.
- Movimentação.
- Combate.
- Minijogos.
- Cutscenes.
- Colecionáveis.
- Mudanças de mapa.
- Salvamento e carregamento.
- Draw Distance.
- Desempenho.

Utilizar a edição autorizada de saves para preparar os cenários necessários.

### Testes prolongados

Executar testes de estabilidade, incluindo sessões prolongadas de pelo menos uma hora, quando o ambiente permitir.

Monitorar:

- FPS.
- Tempo de quadro.
- Memória.
- Uso de CPU e GPU.
- Travamentos.
- Exceções.
- Crescimento anormal de recursos.
- Integridade dos arquivos produzidos.

## 16. Logs e diagnóstico

Implementar ou completar um sistema de diagnóstico que facilite identificar falhas reais.

Incluir, quando tecnicamente viável:

- Identificação da versão do build.
- Informações do runtime.
- API gráfica efetiva.
- Informações de GPU.
- Inicialização e encerramento.
- Mudanças de mapa.
- Eventos relevantes.
- Erros e exceções.
- Métricas de desempenho.
- Informações de falhas de salvamento.
- Registros de testes automatizados.

Preservar informações úteis para depuração sem registrar desnecessariamente dados pessoais.

## 17. Fontes técnicas

Consultar as fontes relevantes para a arquitetura existente:

- Banjo-Tooie XBLA Recompiled: https://github.com/iron-jay/banjo_tooie_xbla_recomp
- ReXGlue SDK: https://github.com/rexglue/rexglue-sdk
- Patches Xenia Canary: https://github.com/xenia-canary/game-patches
- XenonRecomp: https://github.com/hedge-dev/XenonRecomp
- XenonRecomp complementar: https://github.com/sonicnext-dev/XenonRecomp
- XenonRecompUnlimited: https://github.com/testdriveupgrade/XenonRecompUnlimited
- Midnight Club LA Recompiled: https://github.com/robertoaraujo77/MC_LA_Recompiled

Consultar também documentação oficial de SDL, DirectX 12, Ghidra, LLVM/Clang, CMake e ferramentas de depuração necessárias.

Utilizar referências externas somente quando forem compatíveis com a arquitetura existente.

Não introduzir dependências ou mudanças de arquitetura sem justificativa técnica.

## 18. Estratégia de execução

Executar a missão por etapas, respeitando dependências.

### Etapa A — Preparação

- Inspecionar o projeto existente.
- Recuperar o contexto técnico e o relatório de auditoria.
- Identificar as pendências ainda presentes.
- Verificar o estado do Git.
- Criar checkpoints.
- Preparar ambientes de testes isolados.
- Confirmar o alvo PT-BR.

### Etapa B — Segurança

- Corrigir os problemas de salvamento.
- Melhorar backups e restauração.
- Validar isolamento.
- Implementar testes negativos.

### Etapa C — Infraestrutura

- Completar logs.
- Validar saves avançados.
- Automatizar acesso aos mundos.
- Preparar a bateria de testes.

### Etapa D — Estabilização

- Validar 60 FPS e física.
- Corrigir resoluções e modos de vídeo.
- Validar Draw Distance.
- Corrigir problemas de renderização.

### Etapa E — Funcionalidades

- Completar controles.
- Validar áudio e câmera.
- Corrigir persistência.
- Finalizar recursos de qualidade de vida.
- Revisar menus e interface.

### Etapa F — Homologação

- Executar regressão completa.
- Verificar tradução PT-BR.
- Testar diferentes saves.
- Testar os mundos acessíveis.
- Executar testes prolongados.
- Documentar resultados.
- Registrar pendências remanescentes.

As etapas podem compartilhar tarefas de preparação, mas mudanças críticas devem ser validadas antes de avançar para componentes dependentes.

## 19. Requisitos de qualidade

São obrigatórios:

1. Preservar fidelidade ao jogo original.
2. Evitar mudanças desnecessárias.
3. Priorizar qualidade em vez de velocidade.
4. Não aceitar testes simulados como única comprovação de funcionalidade real.
5. Não considerar uma compilação bem-sucedida como prova de funcionamento.
6. Não afirmar que um requisito está concluído sem evidência.
7. Não ocultar falhas conhecidas.
8. Não modificar a versão global desnecessariamente.
9. Não comprometer a tradução PT-BR.
10. Manter histórico Git e mecanismos de rollback.
11. Criar testes para correções importantes.
12. Documentar decisões técnicas relevantes.
13. Não implementar DLSS, DLAA, Frame Generation ou ultrawide nesta missão.
14. Evitar mudanças que criem novas regressões.

## 20. Critérios de conclusão

Considerar a missão plenamente concluída somente quando:

- Os defeitos identificados estiverem corrigidos.
- As funcionalidades obrigatórias estiverem implementadas.
- As configurações de PC funcionarem no menu nativo aprovado.
- Os saves estiverem protegidos.
- A tradução PT-BR estiver preservada.
- 4K interno estiver comprovado.
- 60 FPS estiver validado com física e temporização corretas.
- Draw Distance estiver validada nos mundos relevantes.
- Os controles e menus estiverem funcionais.
- Os sistemas de áudio e câmera estiverem validados.
- A persistência estiver correta.
- Os testes automatizados cobrirem os principais cenários do jogo.
- Os testes de estabilidade e regressão estiverem documentados.
- Não existirem regressões críticas conhecidas sem tratamento.

Se algum requisito depender de hardware indisponível, cenário não reproduzido ou limitação técnica comprovada, registrar o bloqueio sem declarar conclusão.

## 21. Relatório final obrigatório

Ao terminar, entregar:

### Resumo executivo
- Estado atual do projeto.
- Correções realizadas.
- Melhorias implementadas.
- Resultados gerais dos testes.

### Correções
Para cada alteração:

- Problema original.
- Causa identificada.
- Solução implementada.
- Arquivos modificados.
- Teste executado.
- Evidência de funcionamento.

### Testes automatizados
- Quantidade executada.
- Testes aprovados.
- Testes reprovados.
- Testes bloqueados.
- Mundos cobertos.
- Configurações de vídeo avaliadas.
- Resultados de desempenho.
- Resultados de integridade dos saves.

### Pendências
Listar absolutamente tudo que não tenha sido concluído ou validado, com justificativas técnicas e recomendações.

### Entrega
- Localização do executável PT-BR.
- Identificação do build.
- Instruções de execução.
- Localização dos relatórios.
- Registro dos commits e checkpoints.

Não declarar sucesso total se houver requisitos obrigatórios pendentes.

---

## DIRETRIZ FINAL

**Corrigir, completar, automatizar, testar e validar o Banjo-Tooie XBLA PT-BR Recompiled, preservando integralmente a experiência original e a tradução brasileira.**

A edição de cópias isoladas dos saves está autorizada sempre que necessária para testes e automação, inclusive para liberar mundos e preparar cenários avançados.

O trabalho deve ser executado de forma autônoma, com prioridade para segurança, estabilidade, fidelidade e qualidade.

Não interromper a missão após a primeira correção bem-sucedida. Prosseguir pelas pendências identificadas, respeitando dependências, limites técnicos e segurança do ambiente.

**Não alterar os saves pessoais do usuário. Não realizar mudanças desnecessárias na versão global. Não reposicionar o menu CONFIGURAÇÕES DE PC, cuja localização atual está aprovada.**

Entregar um relatório verificável distinguindo funcionalidades concluídas, não validadas e pendentes.
