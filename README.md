# EstacionAqui — Estacionamento inteligente (IoT)

Trabalho final de Internet das Coisas — Prof. Marcos Augusto Campagnaro Mucelini (2º semestre/2026).

**Integrantes:** Edson Gabriel Pommer Cardoso, Fabiano Ferreira Rocha Maciel, Kalvin Fabrício Ferreira Reiter, Rafael de Paula Thober.

## O que é
Sistema que mostra, em tempo real, quais vagas de um estacionamento estão livres ou ocupadas.
Um ESP32 lê sensores ultrassônicos (HC-SR04), decide **localmente** se cada vaga está ocupada, acende LEDs
(verde = livre, vermelho = ocupada), mostra os totais em dois displays TM1637 e envia os dados por Wi-Fi
(HTTP/HTTPS) para um servidor que guarda o histórico e exibe um painel web.

Todo o projeto roda **simulado no Wokwi** (sem hardware físico).

Link do projeto no Wokwi: https://wokwi.com/projects/480779562144604161
Painel no ar: https://egpcardoso.pythonanywhere.com
Vídeo de demonstração: **COLE-AQUI-O-LINK-DO-VÍDEO**

## As quatro camadas
| Camada | O que usamos |
|---|---|
| Percepção | 4 sensores HC-SR04 (um por vaga) |
| Rede | Wi-Fi + HTTP(S), mensagens em JSON |
| Processamento | Regra local no ESP32 (< 50 cm por 3 leituras seguidas = ocupada) e servidor Flask com banco SQLite |
| Aplicação | LEDs, displays TM1637 e painel web |

## Estrutura do repositório
```
firmware/     código do ESP32 (sketch.ino), diagrama do Wokwi, bibliotecas e modelo de credenciais
servidor/     servidor Flask (app.py), dependências e script de teste da API
docs/         checklist de segurança (OWASP IoT Top 10)
```

## Payload (formato das mensagens)
Exemplo real enviado pelo ESP32 a cada mudança de estado e a cada 60 s por vaga (heartbeat):
```json
{"id":"A1","status":"ocupada","distancia_cm":19.1,"timestamp":"2026-11-21T14:50:19-03:00","livres":3,"ocupadas":1}
```
- `id`: vaga (A1 a A4) • `status`: `livre` ou `ocupada` • `distancia_cm`: número ou `null` (sem eco)
- `timestamp`: hora do ESP32 (NTP, fuso -03:00) • `livres`/`ocupadas`: totais no momento do envio

## Como executar em outra máquina

### 1) Servidor (PythonAnywhere, plano grátis)
1. Crie uma conta em pythonanywhere.com e uma pasta `estacionaqui` em *Files*; envie `servidor/app.py` para ela.
2. Em *Web* → *Add a new web app* → *Manual configuration* → Python 3.
3. No arquivo WSGI, deixe apenas (trocando usuário e chave):
```python
import os, sys
path = '/home/SEU-USUARIO/estacionaqui'
if path not in sys.path:
    sys.path.insert(0, path)
os.environ['API_KEY'] = 'SUA-CHAVE-ALEATORIA'   # gere com: openssl rand -hex 24
from app import app as application
```
4. Clique em *Reload* e abra `https://SEU-USUARIO.pythonanywhere.com` (o painel aparece vazio).
5. Teste a API (esperado: 401, 401, 400, 201): `API_KEY=SUA-CHAVE ./servidor/test_api.sh https://SEU-USUARIO.pythonanywhere.com`

Para rodar localmente: `cd servidor && pip install -r requirements.txt && API_KEY=SUA-CHAVE python app.py`
(o ESP32 do Wokwi só alcança servidores públicos, por isso usamos o PythonAnywhere).

### 2) Firmware (Wokwi)
1. Em wokwi.com crie um projeto **ESP32** e substitua `sketch.ino`, `diagram.json` e `libraries.txt` pelos de `firmware/`.
2. Crie um arquivo `config.h` copiando `config.example.h` e preencha `API_URL` e `API_KEY` reais.
3. Clique em Play. Arraste o controle de cada HC-SR04: abaixo de 50 cm a vaga fica ocupada (LED vermelho) e o painel atualiza.
4. Deixe a aba do Wokwi visível durante os testes (abas em segundo plano deixam a simulação lenta).

## Segurança
Credenciais fora do repositório (`config.h` no `.gitignore`, chave do servidor em variável de ambiente), chave de API
no cabeçalho `X-API-Key`, HTTPS, validação de entrada no servidor e checklist OWASP IoT Top 10 em `docs/SEGURANCA.md`.

## Uso de IA
Ferramentas de IA foram usadas como apoio; o que foi produzido com elas e como o grupo verificou está descrito no relatório técnico.
