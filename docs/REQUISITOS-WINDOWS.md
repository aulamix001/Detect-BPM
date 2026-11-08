# O que instalar no PC Windows

## Para desenvolver e compilar

- Windows 11 de 64 bits.
- [Visual Studio 2022 Community](https://visualstudio.microsoft.com/vs/older-downloads/),
  carga **Desenvolvimento para desktop com C++**.
- No instalador do Visual Studio: **MSVC v143 x64/x86**, **Windows SDK** e
  **ferramentas CMake para C++ no Windows**.
- [Git para Windows](https://git-scm.com/downloads/win).
- CMake >= 3.24 disponível no terminal. Pode ser o incluído no Visual Studio.
- Internet na primeira configuração, para baixar JUCE 8.0.6 e Rubber Band 3.3.0.
- Recomendação de hardware para compilação: 16 GB de RAM, SSD com 15 GB livres,
  CPU x64 com quatro ou mais núcleos.

Não é necessário instalar JUCE ou Rubber Band manualmente. O CMake busca e
compila as versões fixadas. O VST3 SDK usado pelo plugin vem com o JUCE.

Abra o **Developer PowerShell for VS 2022**, entre na pasta do projeto e confira:

```powershell
cmake --version
git --version
cl
```

O comando `cl` sem arquivos mostra a identificação do compilador e pode retornar
código diferente de zero por falta de entrada; serve aqui apenas para identificar
a ferramenta. O gerador Visual Studio com `-A x64` seleciona o alvo de 64 bits.
Siga os comandos do README para compilar e executar testes.

Para gerar somente o candidato no PowerShell, na raiz extraída do projeto:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -BuildOnly
```

A saída será a pasta completa
`build\windows\SmartTime_artefacts\Release\VST3\SMART TIME.vst3`.
`-BuildOnly` executa os testes CTest; não executa o validator externo, não instala
o plugin e não afirma aprovação no Studio One. O script ainda requer validação
em uma máquina Windows real.

## Para validar

- Studio One x64 com suporte a VST3.
- `validator.exe` do VST3 SDK para o script de empacotamento. É uma ferramenta
  separada de testes; não é pré-requisito para compilar o módulo com o SDK do JUCE.
- WAVs de violão, guitarra, baixo e bateria com autorização de uso.
- Fones ou monitores para avaliar qualidade da correção; interface ASIO é
  recomendada para gravação e monitoração de baixa latência, mas a correção
  offline de arquivos não depende dela.

## Para apenas usar o plugin pronto

Windows x64 e DAW compatível com VST3. Não são necessários Visual Studio,
Git ou CMake. Os requisitos exatos de runtime serão confirmados pelo build
Windows; não presumir que uma máquina sem os redistribuíveis necessários já
consegue carregar o módulo.

O plugin Windows ainda não foi compilado/validado nesta máquina Linux.
Instalar as ferramentas no seu PC prepara a compilação local, mas não concede
a esta sessão acesso remoto ao seu Windows.
