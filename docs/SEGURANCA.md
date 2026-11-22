# Checklist de segurança — OWASP IoT Top 10 (2018)

Fonte da lista: OWASP IoT Top 10 2018 (https://owasp.org/www-project-internet-of-things/).
Situação: **Mitigado** = tratado no projeto • **Parcial** = tratado em parte • **Aceito** = risco conhecido, fora do escopo.

| # | Risco OWASP | Como aparece no EstacionAqui | Situação | O que fizemos / o que falta |
|---|---|---|---|---|
| I1 | Senhas fracas, adivinháveis ou fixas no código | Chave de API entre ESP32 e servidor | Mitigado | Chave aleatória de 48 caracteres hexadecimais, fora do repositório (`config.h` no `.gitignore`; no servidor, variável de ambiente `API_KEY`). Residual: a chave fica dentro do firmware gravado. |
| I2 | Serviços de rede inseguros | Servidor web exposto na internet | Parcial | Só `POST /api/leituras` altera dados e exige chave; `GET /api/estado` e `/api/historico` são públicos e somente leitura (dados de ocupação, sem informação pessoal). Falta limitar tentativas (rate limit). |
| I3 | Interfaces inseguras do ecossistema | API e painel web | Mitigado | Validação de chave (comparação em tempo constante), do formato do JSON, da faixa de valores (distância 0–500) e do tamanho do corpo (2 KB); consultas SQL parametrizadas; painel escreve texto com `textContent` (evita XSS); cabeçalhos CSP e `nosniff`. |
| I4 | Falta de mecanismo seguro de atualização | Firmware | Aceito | Sem atualização pelo ar (OTA); o firmware é atualizado manualmente. Melhoria futura. |
| I5 | Uso de componentes inseguros ou desatualizados | Bibliotecas do ESP32 e Flask | Parcial | Bibliotecas oficiais e atuais; versões ainda não fixadas (melhoria: fixar versões em `libraries.txt` e `requirements.txt`). |
| I6 | Proteção de privacidade insuficiente | Dados coletados | Mitigado | Só se coleta ocupação e distância por vaga; não há placa, imagem nem dado pessoal. |
| I7 | Transferência e armazenamento de dados inseguros | Dados em trânsito e no banco | Parcial | Em trânsito: HTTPS, com **Force HTTPS** ativado no servidor para recusar HTTP puro (correção feita a partir deste checklist). Residual: o firmware usa `setInsecure()` e não valida o certificado do servidor (permite ataque do tipo "homem no meio"); em produção, gravar o certificado raiz no ESP32. Banco SQLite sem criptografia (dados não sensíveis). |
| I8 | Falta de gerenciamento do dispositivo | Vários dispositivos/vagas | Parcial | O painel mostra "sem sinal" quando uma vaga fica 240 s sem enviar (monitoramento básico). Falta chave por dispositivo e inventário. |
| I9 | Configurações padrão inseguras | Chave de exemplo | Mitigado | O valor `troque-esta-chave` existe só no arquivo de exemplo; se `API_KEY` não estiver configurada no servidor, ele recusa todas as escritas. |
| I10 | Falta de proteção física | Hardware | Aceito | Projeto simulado no Wokwi, sem hardware físico. Em placa real: proteger porta serial e leitura da memória flash. |

## Correções implementadas a partir do checklist
1. **Force HTTPS no servidor** (I7): recusa conexões sem criptografia. Verificação: `curl -I http://SEU-USUARIO.pythonanywhere.com` deve redirecionar para `https://`.
2. **Chave de API nova e aleatória** (I1/I9), trocada no servidor e no `config.h` antes dos testes finais, porque a chave usada no desenvolvimento apareceu em capturas de tela.

## Adicional de segurança — onde está cada item
- Credenciais fora do repositório, com arquivo de exemplo e arquivo real no `.gitignore`: `firmware/config.example.h` e `.gitignore`.
- Checklist baseado no OWASP IoT Top 10: este documento.
- Correção decorrente do checklist: seção acima.
- Autenticação e criptografia na comunicação: chave de API no cabeçalho `X-API-Key` + HTTPS.
