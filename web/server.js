const WebSocket = require('ws');
const net       = require('net');
const http      = require('http');
const fs        = require('fs');
const path      = require('path');

const PI_HOST  = process.argv[2] || '127.0.0.1';
const PI_PORT  = 8080;
const WEB_PORT = 60000;

/* ── 프로토콜 상수 ── */
const MSG_CMD   = 0x01, MSG_EVENT = 0x02, MSG_RESP = 0x03, MSG_QUERY = 0x04;
const DEV_LED   = 0x01, DEV_BUZZER = 0x02, DEV_SENSOR = 0x03, DEV_SEGMENT = 0x04;
const ACT_ON    = 0x01, ACT_OFF   = 0x02, ACT_SET_BRIGHTNESS = 0x03;
const ACT_SET_NUMBER = 0x04, ACT_GET_STATUS = 0x05, ACT_PLAY_MELODY = 0x07, ACT_GET_LUX = 0x08;
const EVT_INTRUSION = 0x01, EVT_ALARM_ON = 0x02, EVT_COUNTDOWN = 0x03, EVT_ALARM_TRIGGERED = 0x04;

/* ── HTTP 서버 ── */
const httpServer = http.createServer((req, res) => {
    const filePath = path.join(__dirname, req.url === '/' ? 'index.html' : req.url);
    fs.readFile(filePath, (err, data) => {
        if (err) { res.writeHead(404); res.end('Not found'); return; }
        const mime = { '.html': 'text/html', '.js': 'application/javascript', '.css': 'text/css' };
        res.writeHead(200, { 'Content-Type': mime[path.extname(filePath)] || 'text/plain' });
        res.end(data);
    });
});

/* ── WebSocket 서버 ── */
const wss = new WebSocket.Server({ server: httpServer });
const wsClients = new Set();

function broadcast(obj) {
    const msg = JSON.stringify(obj);
    wsClients.forEach(ws => { if (ws.readyState === WebSocket.OPEN) ws.send(msg); });
}

/* ── 라즈베리파이 TCP 연결 ── */
let tcp = null;
let tcpBuf = Buffer.alloc(0);

function connectPI() {
    tcp = new net.Socket();

    tcp.connect(PI_PORT, PI_HOST, () => {
        console.log(`[TCP] 라즈베리파이 ${PI_HOST}:${PI_PORT} 연결됨`);
        broadcast({ type: 'status', connected: true });
    });

    tcp.on('data', data => {
        tcpBuf = Buffer.concat([tcpBuf, data]);
        while (tcpBuf.length >= 4) {
            const [type, device, action, value] = tcpBuf;
            tcpBuf = tcpBuf.slice(4);

            if (type === MSG_EVENT) {
                const labels = {
                    [EVT_INTRUSION]:       '침입 감지!',
                    [EVT_ALARM_ON]:        '경보 활성화 (LED HIGH + 부저)',
                    [EVT_ALARM_TRIGGERED]: '카운트다운 완료 — 부저 울림!',
                    [EVT_COUNTDOWN]:       `카운트다운: ${value}`,
                };
                broadcast({ type: 'event', action, device, value, label: labels[action] || `이벤트 0x${action.toString(16)}` });
            } else if (type === MSG_RESP) {
                broadcast({ type: 'resp', device, action, value });
            }
        }
    });

    tcp.on('close', () => {
        console.log('[TCP] 연결 끊김 — 3초 후 재연결');
        broadcast({ type: 'status', connected: false });
        setTimeout(connectPI, 3000);
    });

    tcp.on('error', err => console.error('[TCP 오류]', err.message));
}

function sendPI(type, device, action, value) {
    if (tcp && !tcp.destroyed) tcp.write(Buffer.from([type, device, action, value]));
}

/* ── 브라우저 명령 처리 ── */
const handlers = {
    led_on:        () => sendPI(MSG_CMD,   DEV_LED,     ACT_ON,             0),
    led_off:       () => sendPI(MSG_CMD,   DEV_LED,     ACT_OFF,            0),
    led_low:       () => sendPI(MSG_CMD,   DEV_LED,     ACT_SET_BRIGHTNESS, 0x01),
    led_mid:       () => sendPI(MSG_CMD,   DEV_LED,     ACT_SET_BRIGHTNESS, 0x02),
    led_high:      () => sendPI(MSG_CMD,   DEV_LED,     ACT_SET_BRIGHTNESS, 0x03),
    buzzer_on:     () => sendPI(MSG_CMD,   DEV_BUZZER,  ACT_ON,             0),
    buzzer_off:    () => sendPI(MSG_CMD,   DEV_BUZZER,  ACT_OFF,            0),
    buzzer_melody: () => sendPI(MSG_CMD,   DEV_BUZZER,  ACT_PLAY_MELODY,    0),
    query_led:     () => sendPI(MSG_QUERY, DEV_LED,     ACT_GET_STATUS,     0),
    query_buzzer:  () => sendPI(MSG_QUERY, DEV_BUZZER,  ACT_GET_STATUS,     0),
    query_sensor:  () => sendPI(MSG_QUERY, DEV_SENSOR,  ACT_GET_STATUS,     0),
    query_lux:     () => sendPI(MSG_QUERY, DEV_SENSOR,  ACT_GET_LUX,        0),
    segment:       msg => sendPI(MSG_CMD,  DEV_SEGMENT, ACT_SET_NUMBER, msg.value || 5),
    segment_stop:  ()  => sendPI(MSG_CMD,  DEV_SEGMENT, ACT_OFF,        0),
};

wss.on('connection', ws => {
    wsClients.add(ws);
    console.log('[WS] 브라우저 접속');
    ws.on('message', raw => {
        try {
            const msg = JSON.parse(raw);
            if (handlers[msg.cmd]) handlers[msg.cmd](msg);
        } catch (e) { console.error('[WS 오류]', e.message); }
    });
    ws.on('close', () => wsClients.delete(ws));
});

connectPI();
httpServer.listen(WEB_PORT, () => console.log(`[웹] http://localhost:${WEB_PORT}`));
