'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');

const {
    ACTION,
    DEVICE,
    MSG,
    commandMessage,
    decodeFrames,
    encodeMessage,
} = require('../protocol');

test('완전한 TCP 프레임 여러 개를 해석한다', () => {
    const input = Buffer.from([
        MSG.EVENT, DEVICE.SENSOR, 0x01, 0,
        MSG.RESP, DEVICE.LED, ACTION.ON, 0,
    ]);

    const result = decodeFrames(input);
    assert.deepEqual(result.messages, [
        { type: MSG.EVENT, device: DEVICE.SENSOR, action: 0x01, value: 0 },
        { type: MSG.RESP, device: DEVICE.LED, action: ACTION.ON, value: 0 },
    ]);
    assert.equal(result.remaining.length, 0);
});

test('불완전한 TCP 프레임은 다음 입력을 위해 보존한다', () => {
    const result = decodeFrames(Buffer.from([MSG.EVENT, DEVICE.SENSOR, 0x01]));

    assert.deepEqual(result.messages, []);
    assert.deepEqual([...result.remaining], [MSG.EVENT, DEVICE.SENSOR, 0x01]);
});

test('카운트다운 범위를 검증한다', () => {
    assert.deepEqual(
        [...commandMessage('segment', { value: 9 })],
        [MSG.CMD, DEVICE.SEGMENT, ACTION.SET_NUMBER, 9],
    );
    assert.throws(() => commandMessage('segment', { value: 0 }), /1~9/);
    assert.throws(() => commandMessage('segment', { value: '5' }), /1~9/);
});

test('알 수 없는 명령과 잘못된 바이트를 거부한다', () => {
    assert.throws(() => commandMessage('destroy_everything'), /지원하지 않는 명령/);
    assert.throws(() => encodeMessage([MSG.CMD, DEVICE.LED, ACTION.ON, 256]), /0~255/);
});
