# CHi SMART TIME 0.1.0 — resultado do desenvolvimento

Data: 2026-11-08. **Candidato em desenvolvimento; versão 0.1 não aprovada.**

## Entregas presentes

| Caminho | Conteúdo |
| --- | --- |
| `CMakeLists.txt` | C++17, dependências fixadas, targets core, testes, JUCE VST3 e lifecycle |
| `src/core/Engine.h` | Tipos de áudio, análise, parâmetros, cancelamento e contratos DSP |
| `src/core/Engine.cpp` | FFT própria, energia/spectral flux, ataques, BPM, grade/quantização, mapa monotônico, Rubber Band R3 e resampling opcional |
| `src/core/Wav.cpp` | Leitura PCM16/24/32 e float32, mono/estéreo; escrita float32 com fact chunk |
| `src/plugin/Plugin.cpp` | Processador JUCE, estado/parametrização, worker cancelável, slots de preview e editor redimensionável com controles reais |
| `tests/CoreTests.cpp` | Nove casos de teste do núcleo |
| `tests/PluginLifecycle.cpp` | Ciclo de vida do processador/editor e destruição com trabalho pendente |
| `scripts/build-windows.ps1` | Build MSVC Release x64, CTest, validação externa e pacote candidato; sem instalação global |
| `docs/REQUISITOS-WINDOWS.md` | Ferramentas e requisitos para o PC Windows |

A organização de classes completa do documento de arquitetura continua proposta;
o primeiro código agrupa responsabilidades em Engine, Processor e Editor.
Pacote para levar o projeto ao PC Windows:
`dist/CHi-SMART-TIME-0.1.0-source.zip`. Contém fontes e instruções, sem binário
Windows, dependências baixadas, credenciais ou metadados Git.

## Dependências utilizadas

- GCC 14.2.0, CMake 3.31.6 e Ninja 1.11.1 no Linux.
- JUCE 8.0.6, commit `51a8a6d7aeae7326956d747737ccf1575e61e209`.
- Rubber Band 3.3.0, commit `2be46b0dffb13273a67396c77bc9278736bb03d2`.
- Build single-file oficial do Rubber Band, com FFT/resampler internos.
- Headers Linux extraídos de pacotes Debian assinados/verificados pelo APT para
  `/workspace/.smart-time-sysroot`. Ferramentas Python em
  `/workspace/.smart-time-tools`. Nenhum pacote do sistema foi substituído.
- Xvfb local em display `:97` para executar os testes GUI sem desktop conectado.

A origem snapshot.debian.org configurada na máquina retornou 403 do proxy.
Os headers necessários foram obtidos do repositório oficial deb.debian.org
usando índice APT assinado, sem desabilitar verificação TLS ou assinaturas.
Não foi necessária alteração na política de rede.

JUCE/Rubber Band mantêm seus termos abertos/comerciais. Antes da distribuição,
definir uma opção compatível e preservar os avisos transitivos. Nenhuma licença
comercial foi adquirida ou presumida.

## Resultados executados no Linux

| Verificação | Resultado |
| --- | --- |
| Núcleo, Release | 9/9 testes passaram |
| Núcleo, Debug com ASan + UBSan | 9/9 testes passaram; nenhuma falha reportada pelos sanitizadores |
| Integração JUCE Release | Build completou, incluindo módulo VST3 Linux e helper de metadados |
| Suíte com integração | 10/10 testes passaram |
| Lifecycle | 100 ciclos de processor/editor/state; 1.200 combinações de sample rate/buffer; dez destruições com renderização pendente ou cancelada |
| I/O real em disco | WAV sintético importado, corrigido, exportado e reaberto; dados PCM da exportação conferidos |
| Timing em fixture sintética | Erro médio 1.800 → 112,5 frames a 48 kHz, ou 37,5 → 2,34 ms |
| Afinação em fixture de tom | Tom de referência 440 Hz, medição 440,16 Hz, desvio de aproximadamente 0,63 cent |
| Estéreo | Canais idênticos renderizados com diferença < 1e-5; detecção manteve ataques com canais em oposição de fase |
| Cancelamento | Pré-cancelamento e cancelamento solicitado por outra thread durante renderização passaram |

Logs completos da instância atual:

- `build/core/test-results.log`
- `build/sanitize/test-results.log`
- `build/plugin/test-results.log`
- `build/plugin/lifecycle-results.log` (reexecução do teste GUI com snapshot)
- `build/plugin/editor.png` (snapshot do editor real com WAV sintético carregado)

Fixtures audíveis geradas, ambas sintéticas:

- `build/core/test-audio/original.wav`
- `build/core/test-audio/corrected.wav`

As medições não avaliam qualidade musical de violão, guitarra, baixo ou bateria
reais. A confiança do estimador BPM é uma heurística de regularidade, ainda sem
calibração em um corpus musical. Não representa probabilidade de correção de notas.

## Artefato efetivamente compilado

```text
build/plugin/SmartTime_artefacts/Release/VST3/SMART TIME.vst3/
  Contents/x86_64-linux/SMART TIME.so
  Contents/Resources/moduleinfo.json
```

Esse bundle é **Linux ELF x64**, não Windows. Não pode ser instalado no Studio
One Windows. A geração de metadados pelo helper JUCE terminou com sucesso; isso
não substitui a execução de um validador VST3 completo ou testes em DAW.

**Não existe `SMART TIME.vst3` Windows PE x64 gerado nesta sessão.** A máquina
Debian não tem Windows SDK, MSVC, Studio One ou executor Windows conectado.

## Comandos Windows, ainda não executados

Na raiz do projeto, com Visual Studio 2022, MSVC v143 e Windows SDK:

```powershell
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build/windows --config Release --parallel 4
ctest --test-dir build/windows -C Release --output-on-failure --no-tests=error
```

Saída Windows esperada:
`build/windows/SmartTime_artefacts/Release/VST3/SMART TIME.vst3`.
Para validar e empacotar, executar `scripts/build-windows.ps1` com
`-ValidatorPath` apontando para o validator.exe externo. Se ele faltar ou algum
teste falhar, o script não cria o pacote candidato. O script não foi executado
no Windows e seu funcionamento nesse alvo ainda precisa ser confirmado.

## Limitações e verificações pendentes

- Build MSVC/Windows, validator.exe e Studio One não executados. Não declarar
  compatibilidade ou estabilidade Windows como aprovadas.
- Os testes de lifecycle instanciam o processador/editor diretamente; não
  simulam todos os contratos de carga/descarga do wrapper VST3 em uma DAW.
- O núcleo foi testado com sanitizadores; o teste de integração GUI não foi
  executado com ASan/UBSan/TSan. Ausência de races não está comprovada por essas
  execuções; há revisão do protocolo de slots, mas TSan continua pendente.
- Falta escuta de gravações reais e avaliação de artefatos. O detector identifica
  transientes, não notas perfeitas ou vozes independentes em áudio polifônico.
- BPM pode apresentar ambiguidade metade/dobro; ajustar BPM e origem manualmente.
- Naturalidade usa zona morta temporal; não há modelo musical aprendido.
- Preview com sample rate diferente usa interpolação linear, ainda sem conversor
  com filtragem antialias para reduções de sample rate.
- WAV extensível, RF64, formatos comprimidos e mais de dois canais não são
  suportados. Limites: dez minutos, 512 MiB de arquivo e 256 MiB de PCM por clip.
- O estado salva parâmetros e caminho externo; restaurar o arquivo é agendado
  quando o editor abre. Render corrigido não é embutido no projeto da DAW.
- A interface segue a direção visual vintage com CHi, medidores e controles
  JUCE; não é uma reprodução pixel a pixel da referência.
- Correção da entrada em tempo real permanece desabilitada e rotulada futura.
- Nenhuma cópia foi feita para uma pasta global de plugins; nenhuma publicação
  ou instalação Windows foi realizada.

O bloqueio externo para concluir a entrega solicitada é executar o projeto em
um Windows x64 com MSVC e concluir a validação nesse alvo.
