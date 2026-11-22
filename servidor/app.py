"""EstacionAqui - servidor (camada de processamento + aplicacao).
Recebe leituras do ESP32 via HTTP, valida, guarda historico em SQLite e mostra um painel web.
A chave de API vem da variavel de ambiente API_KEY (nunca fica no codigo).
"""
import hmac
import os
import re
import sqlite3
from datetime import datetime, timezone

from flask import Flask, jsonify, request

BASE = os.path.dirname(os.path.abspath(__file__))
DB_PATH = os.path.join(BASE, "estacionaqui.db")
API_KEY = os.environ.get("API_KEY", "")
OFFLINE_APOS_S = 240      # sem leitura ha mais que isso = "sem sinal" (folga porque o simulador do Wokwi é lento)
MAX_HISTORICO = 5000       # linhas guardadas
ID_RE = re.compile(r"^[A-Za-z0-9-]{1,8}$")

app = Flask(__name__)
app.config["MAX_CONTENT_LENGTH"] = 2048  # payload do ESP32 tem ~120 bytes


def db():
    con = sqlite3.connect(DB_PATH)
    con.row_factory = sqlite3.Row
    return con


def init_db():
    with db() as con:
        con.execute("""CREATE TABLE IF NOT EXISTS leituras (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            vaga TEXT NOT NULL,
            status TEXT NOT NULL,
            distancia_cm REAL,
            ts_dispositivo TEXT,
            recebido_em TEXT NOT NULL)""")


init_db()


@app.after_request
def cabecalhos(resp):
    resp.headers["X-Content-Type-Options"] = "nosniff"
    resp.headers["Content-Security-Policy"] = (
        "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'")
    return resp


def chave_valida():
    recebida = request.headers.get("X-API-Key", "")
    return bool(API_KEY) and hmac.compare_digest(recebida.encode(), API_KEY.encode())


def validar(d):
    """Devolve (dados_limpos, None) ou (None, mensagem_de_erro)."""
    if not isinstance(d, dict):
        return None, "corpo deve ser um objeto JSON"
    vaga, status = d.get("id"), d.get("status")
    dist, ts = d.get("distancia_cm"), d.get("timestamp", "")
    if not isinstance(vaga, str) or not ID_RE.match(vaga):
        return None, "id invalido"
    if status not in ("livre", "ocupada"):
        return None, "status deve ser 'livre' ou 'ocupada'"
    if dist is not None:
        if isinstance(dist, bool) or not isinstance(dist, (int, float)) or not 0 <= dist <= 500:
            return None, "distancia_cm deve ser numero entre 0 e 500 (ou null)"
    if not isinstance(ts, str) or len(ts) > 40:
        return None, "timestamp invalido"
    return {"vaga": vaga, "status": status, "dist": dist, "ts": ts}, None


@app.post("/api/leituras")
def receber():
    if not chave_valida():
        return jsonify(erro="nao autorizado"), 401
    dados, erro = validar(request.get_json(silent=True))
    if erro:
        return jsonify(erro=erro), 400
    agora = datetime.now(timezone.utc).isoformat(timespec="seconds")
    with db() as con:
        con.execute(
            "INSERT INTO leituras (vaga, status, distancia_cm, ts_dispositivo, recebido_em) VALUES (?,?,?,?,?)",
            (dados["vaga"], dados["status"], dados["dist"], dados["ts"], agora))
        con.execute("DELETE FROM leituras WHERE id <= (SELECT MAX(id) FROM leituras) - ?", (MAX_HISTORICO,))
    return jsonify(ok=True), 201


@app.get("/api/estado")
def estado():
    agora = datetime.now(timezone.utc)
    with db() as con:
        linhas = con.execute("""
            SELECT l.vaga, l.status, l.distancia_cm, l.ts_dispositivo, l.recebido_em
            FROM leituras l JOIN (SELECT vaga, MAX(id) mid FROM leituras GROUP BY vaga) m ON l.id = m.mid
            ORDER BY l.vaga""").fetchall()
    vagas, livres, ocupadas, sem_sinal = [], 0, 0, 0
    for r in linhas:
        idade = (agora - datetime.fromisoformat(r["recebido_em"])).total_seconds()
        offline = idade > OFFLINE_APOS_S
        if offline:
            sem_sinal += 1
        elif r["status"] == "livre":
            livres += 1
        else:
            ocupadas += 1
        vagas.append({"id": r["vaga"], "status": r["status"], "distancia_cm": r["distancia_cm"],
                      "timestamp": r["ts_dispositivo"], "idade_s": int(idade), "sem_sinal": offline})
    return jsonify(vagas=vagas, livres=livres, ocupadas=ocupadas, sem_sinal=sem_sinal)


@app.get("/api/historico")
def historico():
    try:
        limite = max(1, min(int(request.args.get("limite", 30)), 200))
    except ValueError:
        limite = 30
    with db() as con:
        linhas = con.execute(
            "SELECT vaga, status, distancia_cm, ts_dispositivo, recebido_em FROM leituras ORDER BY id DESC LIMIT ?",
            (limite,)).fetchall()
    return jsonify([dict(r) for r in linhas])


PAINEL = """<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>EstacionAqui</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--txt:#1b1f24;--mut:#6b7280;--ok:#16a34a;--oc:#dc2626;--off:#9ca3af;--bd:#e5e7eb}
@media (prefers-color-scheme:dark){:root{--bg:#111418;--card:#1b2027;--txt:#e8eaed;--mut:#9aa3af;--bd:#2d343d}}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,sans-serif;background:var(--bg);color:var(--txt);padding:16px}
h1{margin:0 0 12px;font-size:1.4rem}.tot{display:flex;gap:12px;flex-wrap:wrap;margin-bottom:16px}
.t{flex:1;min-width:120px;background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:12px;text-align:center}
.t b{display:block;font-size:2rem}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:12px;margin-bottom:20px}
.v{background:var(--card);border:2px solid var(--off);border-radius:12px;padding:14px;text-align:center}
.v.livre{border-color:var(--ok)}.v.ocupada{border-color:var(--oc)}.v h2{margin:0;font-size:1.3rem}
.v small{color:var(--mut);display:block;margin-top:4px}.livre .s{color:var(--ok)}.ocupada .s{color:var(--oc)}.s{font-weight:700;margin-top:6px}
.tab{overflow-x:auto}table{width:100%;border-collapse:collapse;background:var(--card);border-radius:12px;overflow:hidden}
th,td{padding:8px 10px;text-align:left;border-bottom:1px solid var(--bd);font-size:.9rem}th{color:var(--mut)}
</style></head><body>
<h1>EstacionAqui</h1>
<div class="tot"><div class="t"><b id="liv">-</b>Livres</div><div class="t"><b id="ocu">-</b>Ocupadas</div><div class="t"><b id="sem">-</b>Sem sinal</div></div>
<div class="grid" id="vagas"></div>
<h3>Hist&oacute;rico recente</h3>
<div class="tab"><table><thead><tr><th>Vaga</th><th>Status</th><th>Dist. (cm)</th><th>Hora do dispositivo</th></tr></thead><tbody id="hist"></tbody></table></div>
<script>
function el(tag, cls, txt){var e=document.createElement(tag);if(cls)e.className=cls;if(txt!==undefined)e.textContent=txt;return e;}
async function atualizar(){
  try{
    var e=await (await fetch('/api/estado')).json();
    document.getElementById('liv').textContent=e.livres;
    document.getElementById('ocu').textContent=e.ocupadas;
    document.getElementById('sem').textContent=e.sem_sinal;
    var g=document.getElementById('vagas');g.replaceChildren();
    e.vagas.forEach(function(v){
      var c=el('div','v '+(v.sem_sinal?'':v.status));
      c.appendChild(el('h2',null,'Vaga '+v.id));
      c.appendChild(el('div','s',v.sem_sinal?'SEM SINAL':v.status.toUpperCase()));
      c.appendChild(el('small',null,v.distancia_cm===null?'sem eco':v.distancia_cm+' cm'));
      g.appendChild(c);
    });
    var h=await (await fetch('/api/historico?limite=20')).json();
    var tb=document.getElementById('hist');tb.replaceChildren();
    h.forEach(function(r){
      var tr=el('tr');
      [r.vaga,r.status,r.distancia_cm===null?'-':r.distancia_cm,r.ts_dispositivo||r.recebido_em].forEach(function(x){tr.appendChild(el('td',null,String(x)));});
      tb.appendChild(tr);
    });
  }catch(err){}
}
atualizar();setInterval(atualizar,3000);
</script></body></html>"""


@app.get("/")
def painel():
    return PAINEL


if __name__ == "__main__":
    app.run(debug=False, port=5000)
