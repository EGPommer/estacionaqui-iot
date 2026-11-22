/*
  ============================================================
  EstacionAqui - Estacionamento inteligente
  Placa: ESP32 | Sensores: HC-SR04 (ultrassônico) | Displays: TM1637
  ============================================================

  O QUE O PROJETO FAZ (em uma frase):
  Mede se cada vaga tem um carro, mostra isso em LEDs e displays na hora,
  e manda o resultado pela internet para um painel web.

  AS 4 CAMADAS DO TRABALHO:
   1) Percepção    -> os sensores HC-SR04 medem a distância até o carro
   2) Rede         -> Wi-Fi + HTTP levam os dados até o servidor
   3) Processamento-> o ESP32 decide sozinho (ocupada/livre) e o servidor guarda o histórico
   4) Aplicação    -> LEDs, displays e o painel web mostram o resultado para o usuário

  COMO O CÓDIGO ESTÁ ORGANIZADO:
   - loop()        : lê os sensores, decide se a vaga está ocupada, acende LEDs/displays
                     e avisa "essa vaga mudou, precisa enviar".
   - tarefaEnvio() : fica rodando "ao lado" (em outro núcleo do ESP32) só cuidando de enviar
                     para o servidor. Assim, se a internet estiver lenta, os LEDs
                     continuam reagindo na hora.
*/

// ---------- Bibliotecas (códigos prontos que a gente usa) ----------
#include <WiFi.h>              // conectar o ESP32 no Wi-Fi
#include <HTTPClient.h>        // fazer requisições HTTP (o POST para o servidor)
#include <WiFiClientSecure.h>  // conexão segura (HTTPS), necessária para o servidor do PythonAnywhere
#include <TM1637Display.h>     // controlar os displays de 4 dígitos
#include <time.h>              // pegar a data e a hora (para o timestamp das mensagens)
#include "config.h"            // senhas e endereço do servidor (esse arquivo NÃO vai para o GitHub; o modelo está em config.example.h)

// ---------- Configuração das vagas e dos pinos ----------
const uint8_t N_VAGAS = 4;                         // quantidade de vagas do estacionamento
const char*   IDS[N_VAGAS]   = {"A1", "A2", "A3", "A4"};  // nome de cada vaga (vai dentro da mensagem enviada)
const uint8_t TRIG[N_VAGAS]  = {32, 25, 27, 13};   // pino que "dispara" o som de cada sensor
const uint8_t ECHO[N_VAGAS]  = {33, 26, 14, 34};   // pino que "ouve" o eco de cada sensor
const uint8_t LED_V[N_VAGAS] = {23, 22, 19, 18};   // LED verde de cada vaga (acende quando está livre)
const uint8_t LED_R[N_VAGAS] = {5, 15, 2, 17};     // LED vermelho de cada vaga (acende quando está ocupada)

const uint8_t PIN_CLK   = 21;  // fio de "relógio" dos dois displays (os dois dividem o mesmo para economizar pino)
const uint8_t PIN_DIO_L = 16;  // fio de dados do display de vagas LIVRES
const uint8_t PIN_DIO_O = 4;   // fio de dados do display de vagas OCUPADAS

// ---------- Parâmetros da regra local (R2) ----------
const float         LIMIAR_CM         = 50.0;   // se o carro estiver a menos de 50 cm do sensor, a vaga conta como ocupada
const uint8_t       CONFIRMACOES      = 3;      // precisa de 3 leituras seguidas iguais para mudar de estado (evita "pisca-pisca" por ruído)
const unsigned long INTERVALO_LEITURA = 500;    // lê os sensores a cada 500 ms (meio segundo)
const unsigned long HEARTBEAT_MS      = 60000;  // a cada 60 s cada vaga é reenviada mesmo sem mudar (serve para o painel saber que o ESP32 está vivo)

TM1637Display dispLivres(PIN_CLK, PIN_DIO_L);      // cria o objeto do display de vagas livres
TM1637Display dispOcupadas(PIN_CLK, PIN_DIO_O);    // cria o objeto do display de vagas ocupadas

// ---------- Estado das vagas ----------
// "volatile" = avisa o compilador que essa variável é usada por dois "fluxos" ao mesmo tempo
// (o loop e a tarefa de envio), então ele sempre deve ler o valor mais recente.
volatile bool    ocupada[N_VAGAS]   = {false};            // true = tem carro; todas começam livres
volatile float   distancia[N_VAGAS] = {-1, -1, -1, -1};   // última distância medida em cm (-1 = sem eco)
volatile uint8_t contador[N_VAGAS]  = {0};                // quantas leituras seguidas "discordam" do estado atual (confirmação)
volatile bool    pendente[N_VAGAS]  = {false};            // true = a vaga mudou de estado e ainda não foi enviada ao servidor

unsigned long tLeitura = 0;  // guarda o momento da última leitura dos sensores
unsigned long tWifi    = 0;  // guarda o momento da última tentativa de conectar no Wi-Fi

// ---------- Percepção: medir a distância ----------
// Devolve a distância em cm, ou -1 se não voltou eco (nada na frente = vaga livre).
float lerDistancia(uint8_t i) {
  digitalWrite(TRIG[i], LOW);   delayMicroseconds(2);   // garante que o pino começa desligado (limpa o sinal)
  digitalWrite(TRIG[i], HIGH);  delayMicroseconds(10);  // liga por 10 microssegundos: é o "grito" do sensor
  digitalWrite(TRIG[i], LOW);                           // desliga de novo: o sensor manda o som
  unsigned long dur = pulseIn(ECHO[i], HIGH, 30000UL);  // mede quanto tempo o eco demora a voltar (desiste após 30 ms)
  if (dur == 0) return -1;                              // 0 = não voltou nada dentro do tempo: não há objeto perto
  return dur * 0.0343f / 2.0f;                          // converte tempo em cm: o som anda 0,0343 cm por microssegundo; divide por 2 porque o tempo é de ida e volta
}

// ---------- Atuadores: LEDs e displays ----------
// Acende o LED certo de cada vaga e atualiza os dois contadores.
void atualizaSaidas() {
  uint8_t livres = 0, ocup = 0;                        // contadores começam em zero
  for (uint8_t i = 0; i < N_VAGAS; i++) {              // passa por todas as vagas
    bool oc = ocupada[i];                              // copia o estado da vaga para uma variável local
    digitalWrite(LED_V[i], oc ? LOW : HIGH);           // verde acende se estiver livre, apaga se estiver ocupada
    digitalWrite(LED_R[i], oc ? HIGH : LOW);           // vermelho faz o contrário
    oc ? ocup++ : livres++;                            // soma 1 no contador de ocupadas ou no de livres
  }
  dispLivres.showNumberDec(livres, false);             // mostra o total de livres no display (false = sem zeros à esquerda)
  dispOcupadas.showNumberDec(ocup, false);             // mostra o total de ocupadas no outro display
}

// ---------- Rede ----------
// Garante que o Wi-Fi está conectado, sem travar o programa.
void conectaWifi() {
  if (WiFi.status() == WL_CONNECTED) return;           // já está conectado: não precisa fazer nada
  if (millis() - tWifi < 10000 && tWifi != 0) return;  // só tenta de novo depois de 10 s da última tentativa
  tWifi = millis();                                    // anota o momento desta tentativa
  WiFi.disconnect();                                   // limpa qualquer conexão pela metade
  WiFi.begin(WIFI_SSID, WIFI_PASS);                    // começa a conectar (a rede do Wokwi é a "Wokwi-GUEST")
}

// Diz se o relógio do ESP32 já foi acertado pela internet (NTP).
bool horaSincronizada() {
  struct tm t;                                         // estrutura que guarda data e hora
  return getLocalTime(&t, 0);                          // true se já tem hora válida (o 0 significa "não espere")
}

// Devolve a data e hora atuais em texto, ex.: 2026-11-21T14:49:47-03:00
String timestamp() {
  struct tm t;
  if (!getLocalTime(&t, 0)) return "sem-hora";         // se o relógio ainda não foi acertado, avisa em vez de inventar
  char buf[32];                                        // espaço para o texto (32 é folga suficiente)
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S-03:00", &t);  // formata: ano-mês-diaThora:min:seg, fuso de Brasília (-03:00)
  return String(buf);                                  // devolve o texto pronto
}

// Monta a mensagem (JSON) de uma vaga, com o estado mais recente no momento do envio.
void montaPayload(uint8_t i, char* out, size_t n) {
  uint8_t ocup = 0;                                    // vai contar quantas vagas estão ocupadas no total
  for (uint8_t k = 0; k < N_VAGAS; k++) if (ocupada[k]) ocup++;   // conta as ocupadas

  bool  oc = ocupada[i];                               // copia o estado dessa vaga (evita mudar no meio da montagem)
  float d  = distancia[i];                             // copia a distância dessa vaga
  char dist[12];                                       // texto da distância
  if (d < 0) strcpy(dist, "null");                     // sem eco: manda "null" (valor vazio no JSON)
  else snprintf(dist, sizeof(dist), "%.1f", d);        // senão: número com 1 casa decimal

  snprintf(out, n,                                     // escreve o JSON dentro do texto "out"
    "{\"id\":\"%s\",\"status\":\"%s\",\"distancia_cm\":%s,\"timestamp\":\"%s\",\"livres\":%u,\"ocupadas\":%u}",
    IDS[i],                                            // id da vaga, ex.: "A1"
    oc ? "ocupada" : "livre",                          // status em texto
    dist,                                              // distância em cm (ou null)
    timestamp().c_str(),                               // data e hora da mensagem
    N_VAGAS - ocup,                                    // total de vagas livres
    ocup);                                             // total de vagas ocupadas
}

// Envia UMA mensagem para o servidor. Devolve true se deu certo.
bool enviaPayload(WiFiClientSecure& client, const char* payload) {
  unsigned long t0 = millis();                         // anota a hora de início para medir quanto o envio demorou
  HTTPClient http;                                     // cria o "navegador" que vai fazer o POST
  http.setReuse(true);                                 // tenta manter a conexão aberta para o próximo envio (mais rápido)
  http.setTimeout(5000);                               // desiste se o servidor não responder em 5 s
  http.begin(client, API_URL);                         // prepara o envio para o endereço do servidor
  http.addHeader("Content-Type", "application/json");  // avisa ao servidor que o conteúdo é um JSON
  http.addHeader("X-API-Key", API_KEY);                // manda a chave secreta: é assim que o servidor sabe que somos nós
  int code = http.POST(payload);                       // envia e guarda o código de resposta (201 = recebido, 401 = chave errada, 400 = dados inválidos)
  http.end();                                          // encerra esta requisição
  Serial.printf("[HTTP] %s -> %d (%lu ms)\n", payload, code, millis() - t0);  // imprime no Serial: o que foi enviado, a resposta e o tempo gasto
  bool ok = code >= 200 && code < 300;                 // qualquer código 2xx significa sucesso
  if (!ok) client.stop();                              // se falhou, fecha a conexão para recomeçar limpa na próxima
  return ok;                                           // diz ao chamador se deu certo
}

// Tarefa de envio: roda em paralelo ao loop(), então uma rede lenta nunca atrasa os sensores.
// Ordem de prioridade: 1º vagas que mudaram de estado, 2º "sinal de vida" (heartbeat).
void tarefaEnvio(void*) {
  static WiFiClientSecure client;                      // conexão segura usada em todos os envios ("static" = continua existindo)
  client.setInsecure();                                // não confere o certificado do servidor (ok no teste; é um risco para citar no checklist OWASP)
  char payload[220];                                   // espaço para o texto da mensagem
  unsigned long tHb = 0;                               // momento do último heartbeat
  uint8_t proximaHb = 0;                               // qual vaga será reenviada no próximo heartbeat (vai girando 0,1,2,3...)

  for (;;) {                                           // repete para sempre
    if (WiFi.status() != WL_CONNECTED) {               // sem Wi-Fi: não dá para enviar
      vTaskDelay(pdMS_TO_TICKS(500));                  // espera meio segundo...
      continue;                                        // ...e volta ao início do ciclo
    }

    int i = -1;                                        // -1 = nenhuma vaga pendente ainda
    for (uint8_t k = 0; k < N_VAGAS; k++)              // procura a primeira vaga que mudou de estado
      if (pendente[k]) { i = k; break; }

    if (i >= 0) {                                      // achou uma vaga pendente: ela tem prioridade
      pendente[i] = false;                             // tira a marca (se mudar de novo, o loop() marca outra vez)
      montaPayload(i, payload, sizeof(payload));       // monta a mensagem com o estado MAIS RECENTE dela
      if (!enviaPayload(client, payload)) {            // tenta enviar; se falhou...
        pendente[i] = true;                            // ...marca de novo como pendente para não perder
        vTaskDelay(pdMS_TO_TICKS(10000));              // ...e espera 10 s antes de tentar outra vez
      }
    } else if (horaSincronizada() && millis() - tHb >= HEARTBEAT_MS / N_VAGAS) {  // sem pendências: já está na hora de um heartbeat?
      tHb = millis();                                  // anota o momento deste heartbeat
      uint8_t k = proximaHb;                           // escolhe a vaga da vez
      proximaHb = (proximaHb + 1) % N_VAGAS;           // avança para a próxima (depois da A4 volta para a A1)
      if (contador[k] == 0) {                          // só envia se a vaga NÃO estiver no meio de uma confirmação (evita dado contraditório)
        montaPayload(k, payload, sizeof(payload));     // monta a mensagem dessa vaga
        enviaPayload(client, payload);                 // envia (se falhar, não tem problema: o próximo heartbeat repete)
      }
    } else {                                           // nada para fazer agora
      vTaskDelay(pdMS_TO_TICKS(100));                  // descansa 100 ms para não gastar processador à toa
    }

    vTaskDelay(pdMS_TO_TICKS(20));                     // pausa curta ao fim de cada volta: deixa o ESP32 "respirar" entre um envio e outro (sem isso o vigia de travamento reinicia a placa)
  }
}

// ---------- setup(): roda uma vez quando o ESP32 liga ----------
void setup() {
  Serial.begin(115200);                                // liga o Monitor Serial (só para a gente acompanhar/depurar)
  for (uint8_t i = 0; i < N_VAGAS; i++) {              // configura os pinos de cada vaga
    pinMode(TRIG[i], OUTPUT);                          // TRIG manda sinal para fora = saída
    pinMode(ECHO[i], INPUT);                           // ECHO recebe sinal = entrada
    pinMode(LED_V[i], OUTPUT);                         // LED verde = saída
    pinMode(LED_R[i], OUTPUT);                         // LED vermelho = saída
  }
  dispLivres.setBrightness(5);                         // brilho do display de livres (vai de 0 a 7)
  dispOcupadas.setBrightness(5);                       // brilho do display de ocupadas
  atualizaSaidas();                                    // já mostra o estado inicial (tudo livre) antes de conectar

  xTaskCreatePinnedToCore(tarefaEnvio, "envio", 16384, NULL, 0, NULL, 0);  // inicia a tarefa de envio no núcleo 0 (o loop() fica no núcleo 1); 16384 = memória reservada; prioridade 0 (a mais baixa) para não prender o processador e deixar o "vigia" do ESP32 respirar

  WiFi.mode(WIFI_STA);                                 // ESP32 como "cliente" Wi-Fi (conecta numa rede, não cria uma)
  configTime(-3 * 3600, 0, "pool.ntp.org");            // pede a hora para a internet: fuso -3h (Brasília), sem horário de verão
  conectaWifi();                                       // começa a conectar no Wi-Fi
}

// ---------- loop(): repete sem parar ----------
void loop() {
  conectaWifi();                                       // se o Wi-Fi caiu, tenta reconectar (sem travar)

  // DECISÃO LOCAL (R2): acontece aqui dentro do ESP32 e nunca espera a internet
  if (millis() - tLeitura >= INTERVALO_LEITURA) {      // já passaram 500 ms desde a última leitura?
    tLeitura = millis();                               // anota o momento desta leitura
    bool mudou = false;                                // vai virar true se alguma vaga mudar de estado
    for (uint8_t i = 0; i < N_VAGAS; i++) {            // repete para cada vaga
      float d = lerDistancia(i);                       // mede a distância dessa vaga
      distancia[i] = d;                                // guarda para usar no envio
      bool leituraOcupada = (d > 0 && d < LIMIAR_CM);  // ocupada = houve eco E está a menos de 50 cm
      if (leituraOcupada != ocupada[i]) {              // a leitura "discorda" do estado atual?
        if (contador[i] + 1 >= CONFIRMACOES) {         // já é a 3ª vez seguida que discorda?
          ocupada[i] = leituraOcupada;                 // então confirma: muda o estado da vaga
          contador[i] = 0;                             // zera a contagem
          pendente[i] = true;                          // marca para a tarefa de envio mandar ao servidor
          mudou = true;                                // avisa que precisa atualizar LEDs e displays
          Serial.printf("Vaga %s -> %s\n", IDS[i], leituraOcupada ? "OCUPADA" : "LIVRE");  // registra no Serial a mudança
        } else {
          contador[i] = contador[i] + 1;               // ainda não deu 3: soma mais 1 na contagem (escrito assim porque a variável é volatile)
        }
      } else {
        contador[i] = 0;                               // leitura igual ao estado atual: era só ruído, zera a contagem
      }
    }
    if (mudou) atualizaSaidas();                       // se alguma vaga mudou, atualiza LEDs e displays na hora
  }
}
