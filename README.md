# CHi SMART TIME

Intelligent Time Correction — versão planejada 0.1.0, VST3 Windows 11 x64.

Fabricante e logotipo: **CHi**.

## Baixar o projeto

[Download do projeto-fonte ZIP](https://github.com/aulamix001/Detect-BPM/archive/refs/heads/main.zip).
Extraia o ZIP e abra a pasta que contém este README e `CMakeLists.txt`.
Esse download contém fontes; não contém um VST3 Windows compilado.

Projeto em desenvolvimento: núcleo offline C++17, integração Rubber Band 3.3.0,
wrapper/editor JUCE 8.0.6, CMake e script Windows Release x64. O núcleo passou
nos nove testes automatizados Linux em Release e com ASan/UBSan. Isso não aprova
o produto para uso em DAW: o binário Windows e os testes Studio One continuam
pendentes. Veja o [relatório atual](docs/RESULTADOS.md).

- [Auditoria do ambiente](docs/AUDITORIA.md)
- [Arquitetura técnica e contratos de classes](docs/ARQUITETURA.md)
- [Plano de validação e compilação](docs/VALIDACAO.md)
- [O que instalar no PC Windows](docs/REQUISITOS-WINDOWS.md)

O fluxo principal será importar um WAV, analisar ataques, construir e editar a
grade, renderizar a correção, comparar original/corrigido e exportar WAV.
Correção da entrada em tempo real pertence a uma versão posterior.

## Compilar no Windows

Pré-requisitos: Windows x64, Visual Studio 2022 com **Desktop development with
C++**, Windows SDK, Git e CMake >= 3.24. O CMake baixa as versões fixadas de JUCE
e Rubber Band por HTTPS. Na raiz do checkout:

```powershell
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build/windows --config Release --parallel 4
ctest --test-dir build/windows -C Release --output-on-failure --no-tests=error
```

Bundle esperado: `build/windows/SmartTime_artefacts/Release/VST3/SMART TIME.vst3`.
É uma pasta VST3 completa; não distribuir só o módulo interno. Não há cópia
automática para a pasta global de plugins.

O [script Windows](scripts/build-windows.ps1) automatiza build/testes e exige
`-ValidatorPath C:\caminho\validator.exe` para validar e empacotar um candidato
em `dist`. O validator externo não é necessário para compilar o plugin. Sem
ele, o script informa a localização do build e termina sem empacotar. O script
Windows ainda não foi executado nesta máquina Linux.

Para gerar somente o bundle candidato, executando os testes mas sem o validator
externo, use no Developer PowerShell for VS 2022:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -BuildOnly
```

Esse modo não instala nem empacota o plugin e não o aprova para uso no Studio One.

## Testar o núcleo sem GUI

```sh
cmake -S . -B build/core -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSMART_TIME_BUILD_PLUGIN=OFF -DBUILD_TESTING=ON
cmake --build build/core --parallel 4
ctest --test-dir build/core --output-on-failure --no-tests=error
```

O teste `core_render` deixa WAVs sintéticos original/corrigido em
`build/core/test-audio`. Eles não substituem escuta de instrumentos reais.

## Dependências e distribuição

JUCE 8.0.6: `51a8a6d7aeae7326956d747737ccf1575e61e209`.
Rubber Band 3.3.0: `2be46b0dffb13273a67396c77bc9278736bb03d2`.
O Rubber Band é compilado pelo single-file build oficial com FFT/resampler
internos, sem DLL Rubber Band adicional. Revisar licenças abertas/comerciais e
avisos transitivos antes de distribuir um produto proprietário. Nenhuma licença
comercial foi presumida ou adquirida por este projeto.
