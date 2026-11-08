# Auditoria — 2026-11-08

## Evidência coletada

| Item | Resultado observado |
| --- | --- |
| Checkout | `/workspace/Detect-BPM` |
| Repositório | `aulamix001/Detect-BPM`, GitHub |
| Estado inicial | Sem commits locais, branch simbólica `work`, sem arquivos versionados |
| Remoto | `git ls-remote origin` terminou com código 0 e sem refs |
| Branch solicitada | `git ls-remote --exit-code origin refs/heads/main`: código 2, sem correspondência |
| Sistema | Debian GNU/Linux 13, Linux x86_64 |
| Compilador | GCC / G++ 14.2.0 |
| Recursos | 5 processadores lógicos reportados; 33 GiB RAM total, aproximadamente 32 GiB disponíveis; 60 GiB de disco disponíveis |
| Ferramentas presentes | Git, Python 3, curl, pkg-config |
| Ferramentas ausentes no PATH | CMake, Ninja, clang++, cl, MSBuild, Wine, compilador MinGW x64 |
| JUCE | Não localizado em `/workspace`, `/opt` ou `/usr/share/juce` |
| Rubber Band | Runtime `librubberband.so.2` presente; headers e metadados pkg-config ausentes |
| Dependências Linux de desenvolvimento | ALSA, FreeType, X11, XRandR, Xinerama e XCursor não disponíveis via pkg-config |
| Instruções locais | Nenhum `AGENTS.md` encontrado em `/workspace` |
| Rede | Política restrita aplicada; leituras HTTPS dos arquivos oficiais de licença JUCE e Rubber Band funcionaram com verificação TLS |

Não foram instaladas dependências nesta etapa de auditoria e arquitetura.
Nenhum código anterior foi modificado. Os únicos arquivos adicionados são os
documentos de planejamento. A leitura do rascunho de configuração do ambiente
falhou duas vezes com `INVALID_ARGUMENT`: “The current environment draft could
not be verified”. Não foi confirmada nenhuma persistência de configuração.

## O que este ambiente permite preparar

Desenvolvimento C++ e testes do núcleo DSP portável depois da instalação de
CMake/Ninja e das bibliotecas de desenvolvimento. Um VST3 Linux poderia servir
como verificação adicional da integração JUCE, mas não valida o binário Windows.

O artefato solicitado depende de Windows 11 x64, Visual Studio 2022 com MSVC v143,
Windows SDK e CMake. Studio One e o validador VST3 precisam executar no Windows
para validar o alvo de entrega. Eles não estão disponíveis nesta máquina.

## Dependências propostas

- C++17; CMake >= 3.24; Ninja para validação Linux.
- JUCE 8, versão exata e commit a fixar antes da integração; VST3 SDK do próprio JUCE.
- Rubber Band, versão e commit a fixar antes da integração, com API de mapa temporal.
- MSVC v143 e Windows SDK no ambiente Windows.
- Sem servidor, banco de dados ou chave de API para o fluxo offline.

Fixar dependências por commit e manter TLS e verificações de integridade ativos.
Não depender da biblioteca compartilhada Linux existente para distribuir no Windows.
Usar no máximo quatro jobs inicialmente para builds locais.

## Licenças

Fontes consultadas nesta auditoria:

- https://raw.githubusercontent.com/juce-framework/JUCE/8.0.6/LICENSE.md
- https://juce.com/legal/juce-8-licence/
- https://raw.githubusercontent.com/breakfastquay/rubberband/default/COPYING
- https://breakfastquay.com/rubberband/

JUCE 8 oferece AGPLv3 ou licença comercial. Rubber Band oferece GPL ou licença
comercial; o arquivo COPYING consultado contém GPL versão 2. Confirmar os termos
completos da versão fixada, incluindo avisos nos fontes e dependências transitivas.
Não presumir que essas bibliotecas permitem distribuição proprietária gratuita.
Registrar a opção de licenciamento antes de distribuir; a auditoria não estabelece
que licenças comerciais foram adquiridas. O nome e logotipo do fabricante serão
exclusivamente CHi.
