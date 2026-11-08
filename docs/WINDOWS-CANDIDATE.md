# VST3 Windows x64 gerado

O candidato foi compilado com MSVC no runner `windows-2022` do GitHub Actions.
Fonte do build: `b1b363659ac9efc200c10729ecfca4c150d50e6b`.
O código do plugin é o da revisão `912ade1`; os commits posteriores preparam o CI.

- [Execução concluída com sucesso](https://github.com/aulamix001/Detect-BPM/actions/runs/42520060464).
- [Artefato Windows](https://github.com/aulamix001/Detect-BPM/actions/runs/42520060464/artifacts/13632514687).
- Nome: `CHi-SMART-TIME-Windows-x64-candidate`.
- Tamanho informado pelo GitHub: 2,84 MB.
- SHA256 informado pelo serviço para o artefato:
  `18e392f96c0336cfa16f25c865cd77651c1712c55d0e3b6f25e74d865fecad73`.
- Retenção configurada: 30 dias. É necessário login no GitHub para baixar artefatos.

## O que passou

O job concluiu com sucesso as etapas de build Release x64/CTest, obtenção do
pluginval com SHA256 conferido, validação pluginval 1.0.4 no nível 5,
empacotamento e upload. O script de build executa os 14 testes CTest atuais e
interrompe em qualquer falha ou execução sem testes.

O pluginval valida o carregamento real do VST3 e exercita processamento, estado,
automação, editor e buses. Isso não é execução do validator.exe oficial do SDK:
essa ferramenta externa não foi fornecida. O candidato não foi testado no
Studio One, nem foi aprovado para produção ou distribuído como versão estável.

Na primeira execução do CI, build/CTest passaram, mas a etapa de pluginval
retornou erro e não publicou o candidato. A segunda execução passou após ajustar
a chamada para `Start-Process -Wait`, usar diretórios absolutos e capturar stdout/
stderr. Não foi necessário alterar o DSP. O motivo exato da primeira falha não
foi confirmado, pois os logs dessa execução exigem acesso autenticado. Não se
deve interpretar a segunda execução como prova de estabilidade em todos os hosts.

## Download e conteúdo

Entre no GitHub e abra o artefato acima. Se aparecer 404 quando estiver
desconectado, faça login e use a seção **Artifacts** na página da execução.

O download do GitHub é um ZIP de artefato contendo o ZIP interno:

```text
CHi-SMART-TIME-Windows-x64-candidate.zip
  SMART TIME.vst3/
    Contents/x86_64-win/SMART TIME.vst3
    Contents/Resources/moduleinfo.json
  CANDIDATE.txt
  SHA256.txt
```

Extraia os dois ZIPs. Preserve a pasta `SMART TIME.vst3` inteira, não apenas o
módulo interno. `SHA256.txt` contém o hash do módulo, diferente do hash do ZIP do
artefato informado acima. Nenhuma instalação global foi executada.

O candidato inclui correção offline e análise ao vivo opcional sem alterar a
entrada. Ainda não inclui correção ao vivo, captura ou lookahead. Permanecem
as limitações de detecção/tempo e os requisitos de licenciamento de JUCE e
Rubber Band documentados no projeto. O artefato destina-se a testes.
