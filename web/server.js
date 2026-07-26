'use strict';

const fs = require('fs');
const http = require('http');
const net = require('net');
const path = require('path');
const WebSocket = require('ws');

const {
    EVENT,
    MSG,
    commandMessage,
    decodeFrames,
} = require('./protocol');

const PI_HOST = process.argv[2] || '127.0.0.1';
const PI_PORT = 8080;
const WEB_PORT = 60000;
const RECONNECT_DELAY_MS = 3000;

const INDEX_FILE = path.join(__dirname, 'index.html');

const httpServer = http.createServer((request, response) => {
    let pathname;
    try {
        pathname = new URL(request.url, 'http://localhost').pathname;
    } catch {
        response.writeHead(400);
        response.end('Bad request');
        return;
    }

    if (request.method !== 'GET' || (pathname !== '/' && pathname !== '/index.html')) {
        response.writeHead(404);
        response.end('Not found');
        return;
    }

    fs.readFile(INDEX_FILE, (error, data) => {
        if (error) {
            response.writeHead(500);
            response.end('Internal server error');
            return;
        }
        response.writeHead(200, {
            'Content-Type': 'text/html; charset=utf-8',
            'X-Content-Type-Options': 'nosniff',
        });
        response.end(data);
    });
});

const wss = new WebSocket.Server({
    server: httpServer,
    maxPayload: 1024,
});
const wsClients = new Set();

function broadcast(message) {
    const encoded = JSON.stringify(message);
    for (const client of wsClients) {
        if (client.readyState === WebSocket.OPEN) {
            client.send(encoded);
        }
    }
}

let tcp = null;
let tcpBuffer = Buffer.alloc(0);
let reconnectTimer = null;

function isTcpConnected() {
    return tcp !== null && !tcp.destroyed && tcp.readyState === 'open';
}

function scheduleReconnect() {
    if (reconnectTimer !== null) {
        return;
    }
    reconnectTimer = setTimeout(() => {
        reconnectTimer = null;
        connectPI();
    }, RECONNECT_DELAY_MS);
}

function handleTcpMessage(message) {
    const { type, device, action, value } = message;

    if (type === MSG.EVENT) {
        const labels = {
            [EVENT.INTRUSION]: '침입 감지!',
            [EVENT.ALARM_ON]: '경보 활성화 (LED HIGH + 부저)',
            [EVENT.ALARM_TRIGGERED]: '카운트다운 완료 — 부저 울림!',
            [EVENT.COUNTDOWN]: `카운트다운: ${value}`,
        };
        broadcast({
            type: 'event',
            action,
            device,
            value,
            label: labels[action] || `이벤트 0x${action.toString(16)}`,
        });
    } else if (type === MSG.RESP) {
        broadcast({ type: 'resp', device, action, value });
    }
}

function connectPI() {
    const socket = new net.Socket();
    tcp = socket;
    tcpBuffer = Buffer.alloc(0);

    socket.connect(PI_PORT, PI_HOST, () => {
        console.log(`[TCP] 라즈베리파이 ${PI_HOST}:${PI_PORT} 연결됨`);
        broadcast({ type: 'status', connected: true });
    });

    socket.on('data', data => {
        if (tcp !== socket) {
            return;
        }
        const decoded = decodeFrames(Buffer.concat([tcpBuffer, data]));
        tcpBuffer = decoded.remaining;
        decoded.messages.forEach(handleTcpMessage);
    });

    socket.on('close', () => {
        if (tcp !== socket) {
            return;
        }
        tcp = null;
        tcpBuffer = Buffer.alloc(0);
        console.log('[TCP] 연결 끊김 — 3초 후 재연결');
        broadcast({ type: 'status', connected: false });
        scheduleReconnect();
    });

    socket.on('error', error => {
        console.error('[TCP 오류]', error.message);
    });
}

function sendPI(message) {
    if (!isTcpConnected()) {
        return false;
    }
    return tcp.write(message);
}

wss.on('connection', ws => {
    wsClients.add(ws);
    ws.send(JSON.stringify({ type: 'status', connected: isTcpConnected() }));
    console.log('[WS] 브라우저 접속');

    ws.on('message', raw => {
        try {
            const payload = JSON.parse(raw.toString());
            const message = commandMessage(payload.cmd, payload);
            if (!sendPI(message)) {
                ws.send(JSON.stringify({ type: 'error', message: '장치 서버에 연결되어 있지 않습니다.' }));
            }
        } catch (error) {
            ws.send(JSON.stringify({ type: 'error', message: error.message }));
        }
    });
    ws.on('close', () => wsClients.delete(ws));
});

connectPI();
httpServer.listen(WEB_PORT, () => console.log(`[웹] http://localhost:${WEB_PORT}`));
