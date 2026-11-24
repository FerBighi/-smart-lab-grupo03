const express = require('express');
const cors = require('cors');
const fs = require('fs');
const path = require('path');

const app = express();
const PORT = 3000;
const LOG_FILE = path.join(__dirname, 'auditoria_acessos.json');

app.use(cors());
app.use(express.json());

// Evita tela de aviso/bloqueio em alguns túneis HTTP (ex: ngrok)
app.use((req, res, next) => {
  res.setHeader('ngrok-skip-browser-warning', 'true');
  next();
});

// Estado atual do laboratório (recebido do ESP32)
let estadoLab = {
  temperatura: 0,
  humidade: 0,
  portaAberta: false,
  fechaduraDestravada: false,
  alarmeAtivo: false,
  motivoAlarme: 'Nenhum',
  modoArmado: true, // true = Trancado/Armado | false = Em aula/Desarmado
  ultimaAtualizacao: null
};

// Comandos aguardando o próximo ciclo do ESP32
let comandosPendentes = {
  comandoLiberar: false,
  comandoSilenciar: false,
  modoArmado: true
};

// Lê o arquivo de log de auditoria
function lerLogs() {
  if (!fs.existsSync(LOG_FILE)) {
    fs.writeFileSync(LOG_FILE, JSON.stringify([], null, 2));
  }
  try {
    return JSON.parse(fs.readFileSync(LOG_FILE, 'utf8'));
  } catch (err) {
    console.error('Erro ao ler auditoria_acessos.json, recriando...', err);
    fs.writeFileSync(LOG_FILE, JSON.stringify([], null, 2));
    return [];
  }
}

// Salva novo registro com data e hora exata
function salvarLog(tipo, descricao) {
  const logs = lerLogs();
  const agora = new Date().toLocaleString('pt-BR', { timeZone: 'America/Sao_Paulo' });

  const novoRegistro = {
    id: logs.length > 0 ? logs[0].id + 1 : 1,
    dataHora: agora,
    tipo: tipo || 'EVENTO',
    descricao: descricao || ''
  };

  logs.unshift(novoRegistro);
  fs.writeFileSync(LOG_FILE, JSON.stringify(logs, null, 2));
  console.log(`[AUDITORIA] ${agora} | ${novoRegistro.tipo} | ${novoRegistro.descricao}`);
}

// --- ROTAS DO ESP32 ---

// ESP32 envia telemetria/estado e recebe comandos do painel
app.post('/api/status', (req, res) => {
  const body = req.body || {};

  estadoLab = {
    temperatura: typeof body.temperatura === 'number' ? body.temperatura : 0,
    humidade: typeof body.humidade === 'number' ? body.humidade : 0,
    portaAberta: Boolean(body.portaAberta),
    fechaduraDestravada: Boolean(body.fechaduraDestravada),
    alarmeAtivo: Boolean(body.alarmeAtivo),
    motivoAlarme: body.motivoAlarme || 'Nenhum',
    modoArmado: comandosPendentes.modoArmado,
    ultimaAtualizacao: new Date().toLocaleTimeString('pt-BR', { timeZone: 'America/Sao_Paulo' })
  };

  const respostaESP = {
    comandoLiberar: comandosPendentes.comandoLiberar,
    comandoSilenciar: comandosPendentes.comandoSilenciar,
    modoArmado: comandosPendentes.modoArmado
  };

  // Consome comandos de pulso único
  comandosPendentes.comandoLiberar = false;
  comandosPendentes.comandoSilenciar = false;

  res.json(respostaESP);
});

// ESP32 envia eventos para o log de auditoria
app.post('/api/logs', (req, res) => {
  const { tipo, descricao } = req.body || {};
  salvarLog(tipo || 'ACESSO', descricao || 'Evento registrado pelo ESP32');
  res.json({ ok: true });
});

// --- ROTAS DO DASHBOARD ---

app.get('/api/dashboard', (req, res) => {
  res.json({
    estado: estadoLab,
    logs: lerLogs()
  });
});

app.post('/api/comando/liberar', (req, res) => {
  comandosPendentes.comandoLiberar = true;
  res.json({ ok: true, mensagem: 'Comando de liberação (5s) enviado ao ESP32.' });
});

app.post('/api/comando/modo', (req, res) => {
  const novoModo = Boolean(req.body && req.body.modoArmado);
  comandosPendentes.modoArmado = novoModo;
  estadoLab.modoArmado = novoModo;

  salvarLog(
    'ALTERACAO_MODO',
    `Modo do laboratório alterado para: ${novoModo ? 'TRANCADO / ARMADO' : 'EM AULA / DESARMADO'}`
  );
  res.json({ ok: true, modoArmado: novoModo });
});

app.post('/api/comando/silenciar', (req, res) => {
  comandosPendentes.comandoSilenciar = true;
  salvarLog('ALARME_SILENCIADO', 'Alarme silenciado manualmente pelo Dashboard.');
  res.json({ ok: true });
});

// --- INTERFACE WEB DO DASHBOARD LOCAL ---
app.get('/', (req, res) => {
  res.send(`
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Lab Access & Security - Grupo 3</title>
  <style>
    body { font-family: 'Segoe UI', Tahoma, sans-serif; background: #0f172a; color: #f8fafc; margin: 0; padding: 20px; }
    .container { max-width: 980px; margin: auto; }
    h1 { border-bottom: 2px solid #334155; padding-bottom: 12px; display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 10px; }
    .sync { font-size: 13px; color: #94a3b8; font-weight: normal; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(190px, 1fr)); gap: 15px; margin-bottom: 20px; }
    .card { background: #1e293b; padding: 18px; border-radius: 10px; border: 1px solid #334155; }
    .card h3 { margin: 0 0 8px 0; font-size: 12px; color: #94a3b8; text-transform: uppercase; letter-spacing: 0.5px; }
    .valor { font-size: 22px; font-weight: bold; }
    .alerta-banner { background: #dc2626; color: white; padding: 14px 18px; border-radius: 8px; font-weight: bold; margin-bottom: 20px; display: none; }
    .controles { display: flex; gap: 10px; flex-wrap: wrap; margin-bottom: 25px; }
    button { padding: 12px 18px; border: none; border-radius: 6px; font-weight: bold; cursor: pointer; font-size: 14px; transition: opacity 0.2s; }
    button:hover { opacity: 0.9; }
    .btn-liberar { background: #10b981; color: white; }
    .btn-armar { background: #f59e0b; color: #0f172a; }
    .btn-silenciar { background: #ef4444; color: white; }
    table { width: 100%; border-collapse: collapse; background: #1e293b; border-radius: 8px; overflow: hidden; }
    th, td { padding: 12px 14px; text-align: left; border-bottom: 1px solid #334155; font-size: 14px; }
    th { background: #334155; color: #e2e8f0; }
    .tr-alerta { background: rgba(220, 38, 38, 0.22); }
    .badge { padding: 4px 8px; border-radius: 4px; font-size: 12px; font-weight: bold; display: inline-block; }
    .badge-alerta { background: #dc2626; color: #fff; }
    .badge-ok { background: #059669; color: #fff; }
    .badge-info { background: #2563eb; color: #fff; }
  </style>
</head>
<body>
  <div class="container">
    <h1>
      <span>🔐 Lab Access & Security — Grupo 3</span>
      <span class="sync" id="sync">Aguardando ESP32...</span>
    </h1>

    <div id="bannerAlarme" class="alerta-banner">
      🚨 ALARME ATIVO: <span id="motivoAlarme"></span>
    </div>

    <div class="controles">
      <button class="btn-liberar" onclick="liberarPorta()">🔓 Liberar Porta (5s)</button>
      <button class="btn-armar" id="btnModo" onclick="alternarModo()">🛡️ Modo: TRANCADO/ARMADO</button>
      <button class="btn-silenciar" onclick="silenciarAlarme()">🔕 Silenciar Alarme</button>
    </div>

    <div class="grid">
      <div class="card">
        <h3>Estado da Porta</h3>
        <div class="valor" id="stPorta">--</div>
      </div>
      <div class="card">
        <h3>Fechadura (Relé GPIO 26)</h3>
        <div class="valor" id="stFechadura">--</div>
      </div>
      <div class="card">
        <h3>Temp. Hall (DHT)</h3>
        <div class="valor" id="stTemp">-- °C</div>
      </div>
      <div class="card">
        <h3>Umidade Hall</h3>
        <div class="valor" id="stUmid">-- %</div>
      </div>
    </div>

    <h2>📋 Log de Auditoria de Acessos</h2>
    <table>
      <thead>
        <tr>
          <th>Data / Hora</th>
          <th>Evento</th>
          <th>Detalhes</th>
        </tr>
      </thead>
      <tbody id="tabelaLogs"></tbody>
    </table>
  </div>

  <script>
    let modoAtual = true;

    function classeBadge(tipo) {
      if (tipo === 'INTRUSAO' || tipo === 'PORTA_ABERTA_TEMPO') return 'badge badge-alerta';
      if (tipo === 'ABERTURA_AUTORIZADA') return 'badge badge-ok';
      return 'badge badge-info';
    }

    async function atualizarDashboard() {
      try {
        const res = await fetch('/api/dashboard');
        const data = await res.json();
        const st = data.estado;

        document.getElementById('sync').innerText = 'Última sinc: ' + (st.ultimaAtualizacao || 'Offline');

        const elPorta = document.getElementById('stPorta');
        elPorta.innerText = st.portaAberta ? '🚪 ABERTA' : '🔒 FECHADA';
        elPorta.style.color = st.portaAberta ? '#f87171' : '#34d399';

        const elFech = document.getElementById('stFechadura');
        elFech.innerText = st.fechaduraDestravada ? '⚡ DESTRAVADA (5s)' : '🔐 TRANCADA';
        elFech.style.color = st.fechaduraDestravada ? '#fbbf24' : '#93c5fd';

        document.getElementById('stTemp').innerText = Number(st.temperatura || 0).toFixed(1) + ' °C';
        document.getElementById('stUmid').innerText = Number(st.humidade || 0).toFixed(1) + ' %';

        modoAtual = Boolean(st.modoArmado);
        const btnModo = document.getElementById('btnModo');
        btnModo.innerText = modoAtual ? '🛡️ Modo: TRANCADO/ARMADO' : '🟢 Modo: EM AULA (DESARMADO)';
        btnModo.style.background = modoAtual ? '#f59e0b' : '#3b82f6';
        btnModo.style.color = modoAtual ? '#0f172a' : '#ffffff';

        const banner = document.getElementById('bannerAlarme');
        if (st.alarmeAtivo) {
          banner.style.display = 'block';
          document.getElementById('motivoAlarme').innerText = st.motivoAlarme;
        } else {
          banner.style.display = 'none';
        }

        const tbody = document.getElementById('tabelaLogs');
        tbody.innerHTML = (data.logs || []).map(l => {
          const isAlerta = l.tipo === 'INTRUSAO' || l.tipo === 'PORTA_ABERTA_TEMPO';
          return \`
            <tr class="\${isAlerta ? 'tr-alerta' : ''}">
              <td>\${l.dataHora}</td>
              <td><span class="\${classeBadge(l.tipo)}">\${l.tipo}</span></td>
              <td>\${l.descricao}</td>
            </tr>
          \`;
        }).join('');
      } catch (e) {
        document.getElementById('sync').innerText = 'Erro ao conectar ao servidor';
      }
    }

    async function liberarPorta() {
      await fetch('/api/comando/liberar', { method: 'POST' });
    }

    async function alternarModo() {
      await fetch('/api/comando/modo', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ modoArmado: !modoAtual })
      });
      atualizarDashboard();
    }

    async function silenciarAlarme() {
      await fetch('/api/comando/silenciar', { method: 'POST' });
    }

    setInterval(atualizarDashboard, 1500);
    atualizarDashboard();
  </script>
</body>
</html>
  `);
});

app.listen(PORT, '0.0.0.0', () => {
  console.log(\`Servidor Lab Access rodando em http://localhost:\${PORT}\`);
  console.log(\`Arquivo de log: \${LOG_FILE}\`);
});