# Compilação e validação planejadas

**Documento da etapa inicial.** A implementação posterior e os resultados
efetivamente executados estão em [RESULTADOS.md](RESULTADOS.md). A matriz abaixo
continua como plano de aceite completo; não equivale ao conjunto menor de testes
já executados no núcleo.

Nenhum comando de compilação ou teste de DSP foi executado nesta etapa: o
repositório ainda não contém código ou CMake. Os comandos abaixo são o contrato
para a implementação seguinte, não instruções já verificadas.

## Contrato CMake

Targets previstos: `SmartTimeCore`, `SmartTimeTests`, `SmartTime_VST3`.
Opção `SMART_TIME_BUILD_PLUGIN=OFF` permite testar o core sem dependências da GUI.
`BUILD_TESTING=ON` registra testes no CTest. Usar C++17, warnings altos e builds
Debug/Release. Não baixar SDK separado que diverge do VST3 integrado ao JUCE.

Metadados previstos no `juce_add_plugin`: `PRODUCT_NAME "SMART TIME"`,
`COMPANY_NAME "CHi"`, versão `0.1.0`, `FORMATS VST3`,
`COPY_PLUGIN_AFTER_BUILD FALSE`, `IS_SYNTH FALSE`, `NEEDS_MIDI_INPUT FALSE` e
`NEEDS_MIDI_OUTPUT FALSE`. Fixar e manter estáveis manufacturer/plugin codes
válidos, bundle ID e IDs de parâmetros antes da primeira versão distribuída.

Linux, após dependências e projeto existirem:

```sh
cmake -S /workspace/Detect-BPM -B /workspace/Detect-BPM/build/core -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSMART_TIME_BUILD_PLUGIN=OFF -DBUILD_TESTING=ON
cmake --build /workspace/Detect-BPM/build/core --parallel 4
ctest --test-dir /workspace/Detect-BPM/build/core --output-on-failure --no-tests=error
```

Windows, terminal de desenvolvimento do Visual Studio 2022, na raiz do checkout:

```powershell
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64 `
  -DSMART_TIME_BUILD_PLUGIN=ON -DBUILD_TESTING=ON
cmake --build build/windows --config Release --parallel 4
ctest --test-dir build/windows -C Release --output-on-failure --no-tests=error
```

Saída esperada, a confirmar pelo build real:
`build/windows/SmartTime_artefacts/Release/VST3/SMART TIME.vst3`.
O bundle VST3 deve conter o módulo PE x64; uma biblioteca ELF renomeada não é
uma entrega Windows. Não copiar automaticamente para a pasta global de plugins.

## Matriz de testes obrigatórios

Todos os itens abaixo estão **não executados**.

| Área | Evidência necessária para aprovação |
| --- | --- |
| WAV | Importar arquivo real mono/estéreo; exportar, reabrir e conferir frames, canais, sample rate, amplitude finita; PCM16/24 e float conforme suporte declarado |
| Entradas inválidas | Cabeçalho truncado, extensão enganosa, silêncio, zero frames, arquivo enorme e falha de permissão retornam erro sem crash ou alocação descontrolada |
| Transientes | Cliques com posições conhecidas em 44,1/48/96 kHz: erro mediano <= 5 ms e recall >= 95% em fixture limpa; áudio real avaliado separadamente |
| BPM | Padrões conhecidos, sincopados e metade/dobro; erro <= 1 BPM nas fixtures regulares; silêncio não retorna alta confiança |
| Grade | Subdivisões, compasso, offset e extremos de swing; índices/destinos ordenados e cálculos sem overflow |
| Quantização | Intensidade 0 preserva posições; 100 aproxima ataques da grade; colisões, limites do clip e naturalidade determinísticos |
| Mapa | Testes aleatórios e casos adversos: começo/fim fixos, monotonicidade estrita, ausência de segmentos zero e stretch limitado |
| Render | Reanalisar saída: redução mensurável do erro temporal; identidade não degrada arquivo inutilmente; duração correta e sem NaN/Inf |
| Pitch | Tom sustentado conhecido e material tonal com mudanças locais de tempo; desvio <= 5 cents na parte sustentada com preservação ativada, regiões de transientes avaliadas à parte |
| Estéreo | L/R idênticos permanecem idênticos dentro da tolerância; par antíf ase preserva relação; impulsos nos dois canais não ganham desalinhamento |
| A/B | Alternar durante preview mantém posição e não produz descontinuidades espúrias; Stop repetido e EOF corretos |
| Sample rate/buffer | 44,1/48/96 kHz; blocos 32, 64, 256, 1024 e tamanhos variáveis; transições com preview ativo e worker pendente |
| Concorrência | Importar/analisar/renderizar/cancelar repetidamente; destruir com cada fase pendente; resultados antigos nunca substituem geração nova |
| GUI | Abrir/fechar/redimensionar repetidamente durante jobs; nenhum acesso a componentes destruídos |
| Estado | Salvar/reabrir sessão, parâmetros restaurados e áudio ausente tratado; sem I/O no callback |
| Host | 100 ciclos de inserir/remover e abrir/fechar GUI, Play/Stop repetido no Studio One Windows, incluindo projeto salvo/reaberto |
| Validador | Executar validator do VST3 SDK contra o bundle Release Windows e preservar saída, versão e código de retorno |
| Memória | ASan/UBSan e TSan em builds Linux separados quando suportados; Windows com sanitizador suportado e dumps de falhas |

Verificar o número real de testes e código de saída; zero testes não aprova
nenhum fluxo. Registrar fixtures, hashes, sample rate, versões de dependências,
compilador, host, parâmetros e resultados para reproduzir cada medição.

Para restrições do callback, instrumentar alocações/liberações durante
`processBlock`, auditar chamadas de I/O e sincronização e executar com cargas
variáveis. Ausência de crash não prova ausência de data races. Sanitizadores e
revisão do protocolo de ownership são verificações complementares.

## Escuta e aceite

Além das métricas, comparar original/corrigido em violão, guitarra, baixo e
bateria reais, com permissão de uso dos arquivos. Documentar artefatos, regiões
problemáticas e ajustes. Nenhuma métrica isolada garante qualidade musical.
Os limiares desta especificação são metas iniciais, ainda não resultados.

Só declarar a versão 0.1 aprovada após importar WAV real, exibir grade e ataques,
renderizar correção audível com pitch preservado, tocar A/B, exportar WAV válido
e passar os testes de ciclo de vida no alvo Windows. Tempo real permanece futuro.
