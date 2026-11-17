const express = require('express');
const cors = require('cors');
const path = require('path');

const app = express();
const PORT = 3000;

// Middlewares obrigatórios para comunicação JSON e arquivos estáticos (HTML/CSS/JS)
app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// Armazenamento em memória (Etapa 2.2 + Regra de Auditoria do Grupo 3)
let historicoLeituras = []; // Mantém as últimas 50 leituras
let logAuditoria = [];      // Registra horários exatos de abertura e intrusão
let comandoPendente = "NENHUM";
let modoArmado = false;     // false = Aula Ativa | true = Trancado/Armado

// Rota raiz: abre automaticamente o dashboard.html ao acessar http://localhost:3000
app.get('/', (req, res) => {
  res.sendFile(path.join(__dirname, 'public', 'dashboard.html'));
});

// =========================================================================
// 1. ROTA POST /api/telemetria (Recebe pacotes do ESP32 a cada 2 segundos)
// =========================================================================
app.post('/api/telemetria', (req, res) => {
  const dados = req.body;
  const agora = new Date();
  const horarioFormatado = agora.toLocaleTimeString('pt-BR');
  const dataHoraCompleta = agora.toLocaleString('pt-BR');

  // Adiciona o carimbo de data/hora (timestamp) exigido na Etapa 2.2
  const leituraComTimestamp = {
    ...dados,
    timestamp: horarioFormatado,
    data_hora: dataHoraCompleta,
    modo_armado: modoArmado
  };

  // Salva no histórico mantendo estritamente as últimas 50 leituras
  historicoLeituras.push(leituraComTimestamp);
  if (historicoLeituras.length > 50) {
    historicoLeituras.shift();
  }

  // Regra Grupo 3: Grava no Log de Auditoria sempre que o estado da porta mudar
  const leituraAnterior = historicoLeituras[historicoLeituras.length - 2];
  if (!leituraAnterior || leituraAnterior.variavel_principal_valor !== dados.variavel_principal_valor) {
    if (dados.variavel_principal_valor === 1) {
      logAuditoria.unshift({
        data_hora: dataHoraCompleta,
        origem: "Botoeira Local / Relé",
        evento: "Porta destravada e aberta (Autorizado)",
        status: "NORMAL"
      });
    } else if (dados.variavel_principal_valor === 2) {
      logAuditoria.unshift({
        data_hora: dataHoraCompleta,
        origem: "Sensor Magnético Porta",
        evento: "ALERTA: Intrusão ou Porta Esquecida Aberta!",
        status: "CRITICO"
      });
    }
  }

  // Prepara o comando pendente para devolver ao ESP32 e limpa a fila
  const comandoParaEnviar = comandoPendente;
  comandoPendente = "NENHUM";

  // Resposta padrão exigida pela Etapa 2.1 e 2.2
  res.status(201).json({
    status: "sucesso",
    comando: comandoParaEnviar,
    modo_armado: modoArmado
  });
});

// =========================================================================
// 2. ROTA GET /api/telemetria (Alimenta os KPIs, Gráfico e Log no Dashboard)
// =========================================================================
app.get('/api/telemetria', (req, res) => {
  const ultimaLeitura = historicoLeituras[historicoLeituras.length - 1] || null;

  res.json({
    grupo_id: "grupo_03",
    atual: ultimaLeitura,
    historico: historicoLeituras,
    logs: logAuditoria,
    modo_armado: modoArmado
  });
});

// =========================================================================
// 3. ROTA POST /api/comando (Recebe comandos dos botões do Dashboard Web)
// =========================================================================
app.post('/api/comando', (req, res) => {
  const { grupo_id, comando } = req.body;
  const dataHoraCompleta = new Date().toLocaleString('pt-BR');

  if (grupo_id && grupo_id !== "grupo_03") {
    return res.status(400).json({ status: "erro", mensagem: "Grupo inválido. Esperado: grupo_03" });
  }

  if (comando === "LIGAR" || comando === "PARAR") {
    comandoPendente = comando;

    logAuditoria.unshift({
      data_hora: dataHoraCompleta,
      origem: "Dashboard Web (Operador)",
      evento: comando === "LIGAR" ? "Comando: Liberar Porta (5s)" : "Comando: Trancar / Silenciar Alarme",
      status: "NORMAL"
    });
  } else if (comando === "ALTERNAR_ARMADO") {
    modoArmado = !modoArmado;
    comandoPendente = modoArmado ? "ARMAR" : "DESARMAR";

    logAuditoria.unshift({
      data_hora: dataHoraCompleta,
      origem: "Dashboard Web (Operador)",
      evento: modoArmado ? "Modo Trancado/Armado ATIVADO" : "Modo Aula Ativa (Desarmado)",
      status: modoArmado ? "ATENCAO" : "NORMAL"
    });
  }

  res.json({
    status: "agendado",
    comando_agendado: comandoPendente,
    modo_armado: modoArmado
  });
});

// Inicia o servidor escutando em toda a rede local (0.0.0.0) na porta 3000
app.listen(PORT, '0.0.0.0', () => {
  console.log(`=========================================================`);
  console.log(`🚀 Servidor Node.js - Grupo 03 (Controle de Acesso)`);
  console.log(`📡 Rodando na porta ${PORT} (Acesse: http://localhost:${PORT})`);
  console.log(`=========================================================`);
});
