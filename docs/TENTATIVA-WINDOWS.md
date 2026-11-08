# Compilação Windows — tentativa de 2026-11-08

Resultado: nenhum binário VST3 Windows produzido. Os artefatos Linux anteriores
continuam sendo Linux. Não foram renomeados ou apresentados como Windows.

## Diagnóstico verificado

1. Instalado MinGW x64 GCC 14 em prefixo local por extração de pacotes Debian
   autenticados pelo APT, sem alterar o sistema.
2. O núcleo C++ compilou para objetos Windows, mas JUCE 8.0.6 recusou a toolchain
   explicitamente: `MinGW is not supported. Please use an alternative compiler.`
   Não foi modificado o JUCE para remover essa restrição. O arquivo de toolchain
   MinGW experimental foi removido para não sugerir uma compilação suportada.
3. Preparada alternativa LLVM/Clang com ABI Microsoft e headers/libraries do
   Windows SDK/CRT oficiais. A ferramenta xwin 0.10.0 foi obtida da release oficial
   do projeto e seu arquivo foi verificado com o SHA256 publicado.
4. A obtenção do SDK foi bloqueada pela política de rede. A URL
   `https://aka.ms/vs/17/release/channel` responde com redirecionamento para
   `https://download.visualstudio.microsoft.com/.../VisualStudio.17.Release.chman`.
   A conexão ao destino redirecionado retorna 403 pelo proxy.
5. A leitura do rascunho de configuração retornou `INVALID_ARGUMENT` e
   `The current environment draft could not be verified`. Nenhuma lista de
   domínios desconhecida foi sobrescrita.

## Ação necessária

Adicionar `download.visualstudio.microsoft.com` aos domínios permitidos nas
configurações do ambiente, preservando as regras existentes, e salvar.
Depois disso, repetir a obtenção do SDK com xwin e continuar a compilação.
Se o manifesto oficial referenciar outro domínio bloqueado, diagnosticá-lo antes
de solicitar uma adição específica. Não desabilitar TLS, assinaturas ou checksums.

Essa alternativa usa Clang com ABI Microsoft; não é um build feito com cl.exe
do MSVC. Build/validação em Windows e Studio One continuam necessários para a
aprovação da versão 0.1, mesmo que um binário candidato seja gerado por cross-build.

## Configuração CMake

Foi adicionado suporte a um pacote JUCE instalado de versão exata 8.0.6,
mantendo o download por commit quando o pacote não está disponível. A opção
`SMART_TIME_VST3_MANIFEST` permite desligar a execução do helper para um alvo
que não pode rodar no host. O padrão continua ligado. O include dos testes de
ciclo de vida agora é obtido do target JUCE, funcionando também com pacote
instalado. Essas alterações não constituem um build Windows concluído.
