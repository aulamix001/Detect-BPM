# Revisão: tema, VUs e análise ao vivo

## Leitura anterior às alterações

Foram lidos todos os arquivos versionados: CMake, fontes, testes, PowerShell e
os documentos do projeto. O framework encontrado foi JUCE 8.0.6, C++17, com
AudioProcessorValueTreeState para parâmetros e Rubber Band 3.3.0 no render offline.
A maior parte da integração e da UI estava em `src/plugin/Plugin.cpp`.
O detector existente usa energia e spectral flux, FFT 1024/hop 128, e estima
BPM pela mediana de intervalos entre ataques com normalização metade/dobro.

Os medidores anteriores usavam pico por bloco e um decaimento no timer, sem RMS
ou referência VU calibrada. Os botões de análise/correção dependiam de um WAV
importado; tocar na entrada da DAW não alimentava o motor. Não havia ring buffer
ou leitura de posição PPQ para analisar a entrada.

O pedido final restringiu a terceira tarefa a análise sem alterar o áudio.
Captura/correção da entrada e lookahead não foram implementados nesta revisão.

## Alterações, na ordem dos commits

1. `a7e9967`: tema centralizado em `src/ui/Theme.h`, LookAndFeel JUCE, grafite,
   cobre, ações vermelho-tijolo e estados normal/hover/ativo/desabilitado.
2. `6e259b4`: RMS/VUs em `src/core/VuMeter.h`, escala e pico/clip separados.
3. Commit da análise: `src/core/RealtimeAnalyser.*`, integração no plugin,
   botão Analisar e testes de concorrência/entrada/estado.

Os IDs existentes dos parâmetros foram preservados. `analyseLive` foi
acrescentado ao final do layout, evitando reordenar os parâmetros anteriores.

## Uso e comportamento

1. Insira o plugin como efeito numa pista com entrada de áudio monitorada.
2. Inicie a reprodução do transporte da DAW e ligue **Analisar**.
3. A entrada passa inalterada. A UI mostra BPM estimado, confiança, BPM/PPQ do
   host, desvios dos últimos 16 ataques e contagem de pacotes perdidos.
4. Até confiança >= 65%, exibe **Analisando...**, sem apresentar um BPM confiável.
5. Desligar **Analisar** interrompe a alimentação da análise e congela o último
   resultado visível. Fechar o editor não destrói a thread ou o resultado.

**BPM do WAV** e **Corrigir** continuam sendo ações do motor offline, disponíveis
quando há arquivo importado. Não se habilitam para fingir uma correção da entrada.
O preview A/B fica indisponível com Analisar ligado, para manter a entrada intacta.
Os rótulos `Tempo real - futuro` e `0.1.0 - DEVELOPMENT` foram removidos da UI.

O toggle e os parâmetros da grade são salvos/restaurados via APVTS. Uma sessão
reaberta inicia uma nova janela de análise: a estimativa antiga não é restaurada
como se descrevesse o áudio atual. O estado do fluxo WAV permanece baseado no
caminho externo do arquivo; o render não é embutido no projeto da DAW.

## Fronteira entre threads

- O callback mede IN/OUT, consulta AudioPlayHead e copia áudio/timing para uma
  fila SPSC pré-alocada; não executa FFT, estimação de tempo, I/O ou locks.
- Os ponteiros para parâmetros usados no callback são obtidos na construção,
  evitando pesquisas/conversões de strings durante o processamento.
- A fila tem 1023 posições úteis de 256 frames, com no máximo dois canais.
  Quando cheia, descarta o pacote e incrementa um contador; nunca bloqueia o áudio.
- A thread de análise mantém uma janela de até oito segundos. A primeira análise
  exige dois segundos; as seguintes ocorrem aproximadamente a cada 0,5 segundo de
  áudio recebido. FFT, buffers dinâmicos e estimativa ficam nessa thread.
- Cada pacote guarda BPM e PPQ do host. A grade usa a subdivisão, swing e origem
  vigentes quando aquele áudio entrou. O desvio é positivo para atraso e negativo
  para adiantamento; sem BPM/PPQ válidos, mostra `--`.
- A repetição de PPQ no início de um loop não apaga o histórico de áudio contínuo.
  Stop/Play, mudança de sample rate, canais ou descontinuidade de pacotes reiniciam
  a janela para não concatenar trechos incompatíveis.
- O snapshot de resultado tem campos atômicos e contador de sequência; a leitura
  da GUI tem tentativas limitadas. Não existe mutex no caminho resultado → UI.
- Timer da UI: 30 fps. Desligado, não há cópia de áudio ou FFT; o worker dorme
  em intervalos de 25 ms. Isso é custo reduzido, não promessa de CPU literalmente zero.
- Destruição solicita cancelamento e faz join. Não há threads destacadas nem
  callbacks de worker que acessem o editor.

## VUs

RMS por canal, com exibição do maior canal. Referência implementada:
`VU = 20*log10(RMS) + 18`, onde amplitude digital 1 é a referência RMS.
O filtro de potência tem constante de 65 ms, atingindo aproximadamente 99,5% da
amplitude RMS de um degrau após 300 ms; a UI suaviza o ponteiro com 25 ms.
A escala vai de -20 a +3 VU, incluindo marcas -20, -10, -5, 0 e +3.

O pico de amostra é independente, e amplitude >= 1 acende o indicador por cerca
de um segundo. Não existe limiter ou clipper inserido no áudio. A balística é
uma aproximação de VU com medição RMS, não um instrumento certificado ANSI/IEC.
A referência acima não usa a normalização AES17 que faz um seno full-scale
corresponder a 0 dBFS RMS; o pico e o RMS, portanto, diferem para um seno.

## Evidência de testes nesta máquina Linux

- Build Release do módulo VST3 Linux com GCC 14/JUCE 8.
- 14/14 testes CTest passaram, incluindo os nove casos offline anteriores, VU, fila,
  loop de bateria, baixo e lifecycle.
- Fila: 200.000 transferências ordenadas entre threads, wrap/full/empty e
  instrumentação de `new`/`delete` sem operações durante push/pop.
- Callback do processador: entrada e saída idênticas amostra a amostra com
  análise ativa e inativa; guard de `new`/`delete` sem operações no callback.
  Essa instrumentação não intercepta toda chamada possível a malloc/rotinas externas.
- Estado: toggle Analisar salvo/restaurado por get/setStateInformation.
- Lifecycle: 100 ciclos de processor/editor, 1.200 combinações de sample rate e
  buffer exercitando os caminhos idle e análise; dez destruições com trabalho
  offline pendente/cancelado. O teste de análise também destrói com fila pendente.
- ASan/UBSan: 13/13 testes do nucleo passaram, sem GUI; o teste adicional com
  retorno PPQ em loop tambem passou apos essa execucao completa.
- ThreadSanitizer: fila e fluxo de análise de bateria. O compilador avisa que
  `atomic_thread_fence` não é modelado pelo TSan. Todos os campos do payload são
  atômicos, mas o teste não prova a coerência do snapshot ou ausência de todos os
  problemas de concorrência no plugin/host. GUI/wrapper não foram testados com TSan.

Os testes musicais geram WAVs estéreo determinísticos de oito segundos, 48 kHz:

| Fixture | Resultado Release medido |
| --- | --- |
| Bateria sintetizada, 120 BPM, atraso 20 ms | 14 ataques; 120 BPM; confiança heurística 100%; desvio médio 20,00 ms; zero pacotes perdidos |
| Baixo sintetizado, atraso ~30 ms com pequena variação | 14 ataques; 119,51 BPM; confiança heurística 100%; desvio médio 30,61 ms; zero pacotes perdidos |

A bateria também exercita PPQ voltando ao início a cada quatro tempos, para
simular loop do transporte. Os arquivos audíveis ficam em
`build/plugin/live-fixtures/{drums-offset,bass-offset}.wav` nesta máquina.
No Windows, os testes os geram ao lado de SmartTimeRealtimeTests.exe, normalmente
em `build/windows/Release/live-fixtures`.

Uma primeira execução com ASan/UBSan falhou no teste de bateria: a espera fixa
de 400 ms não bastou para o worker instrumentado publicar o trecho completo,
embora BPM estivesse em 120 e não houvesse perda de pacotes. O teste agora aguarda
o progresso da janela, com timeout de 14 segundos após alimentar o áudio, e exige
os 14 ataques, além dos mesmos critérios de BPM/desvio. O log da falha original
foi preservado em `build/sanitize-live/revision-tests-first.log`.
Isso também demonstra que o processamento pode ficar atrasado sob instrumentação
ou carga: não há garantia de atualização instantânea nem prazo rígido de análise.

## Limitações reais

- Material musical é sintetizado; não houve escuta humana validada de gravação
  real de bateria/baixo nem teste desta revisão dentro do Studio One.
- Confiança é regularidade dos intervalos, não probabilidade calibrada. Há
  ambiguidade metade/dobro; o estimador normaliza candidatos para ~70–180 BPM.
- Ataques suaves, ruído e notas ligadas podem ser perdidos ou fundidos. Detecta
  transientes, não todas as notas de um instrumento.
- Janela inicial >= 2 s não implica resultado confiável em 2 s. Precisa de ataques
  suficientes e regulares; andamento lento e silêncio podem ficar em Analisando.
- Atualização em lotes de ~0,5 s, acrescida do custo do worker. Timestamps dos
  desvios referem-se à entrada, não ao instante em que a tela é atualizada.
- Timing é baseado nos dados que o host fornece por bloco e em BPM constante
  dentro do bloco. Sem PPQ não se inventa uma fase de grade. Rampas de tempo
  dentro do bloco podem adicionar erro à conversão para milissegundos.
- Só mono/estéreo. A janela e a fila são limitadas; pacotes perdidos são exibidos
  e a janela é reiniciada em descontinuidades.
- Não faz correção da entrada, captura ou lookahead. A latência de áudio continua
  zero porque o caminho Analisar é pass-through.
- Esta revisão foi compilada/testada em Linux. O build MSVC com dez testes
  anteriormente relatado pelo usuário corresponde à revisão anterior, não a esta.
  Build Windows, validador VST3 e teste em DAW desta revisão continuam pendentes.

## Arquivos de evidência

- `build/plugin/revision-tests.log`: suíte Release, 14/14.
- `build/plugin/Testing/Temporary/LastTest.log`: BPM, desvios e saídas detalhadas.
- `build/sanitize-live/revision-tests-final.log`: suíte ASan/UBSan, 13/13.
- `build/sanitize-live/loop-test.log`: bateria com PPQ em loop, ASan/UBSan, 1/1.
- `build/live-tsan-final.log`: execução TSan do fluxo de bateria; sem relatório de race.
- `build/queue-tsan`: teste da fila instrumentado com TSan, aprovado.
- [Editor real gerado pelo teste GUI](screenshots/editor.png): WAV sintetico
  importado, Analisar desligado e VUs em silencio. Não é uma imagem gerada por IA.

A medição grosseira de CPU de todo o processo de teste durante 100 ms com a
análise desligada ficou em ~0,17–0,19 ms na última execução Release. Não inclui
uma DAW real, não isola o callback e não constitui garantia de consumo em todos
os sistemas ou com várias instâncias.
