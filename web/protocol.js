'use strict';

const MESSAGE_SIZE = 4;

const MSG = Object.freeze({
    CMD: 0x01,
    EVENT: 0x02,
    RESP: 0x03,
    QUERY: 0x04,
});

const DEVICE = Object.freeze({
    SYSTEM: 0x00,
    LED: 0x01,
    BUZZER: 0x02,
    SENSOR: 0x03,
    SEGMENT: 0x04,
});

const ACTION = Object.freeze({
    ON: 0x01,
    OFF: 0x02,
    SET_BRIGHTNESS: 0x03,
    SET_NUMBER: 0x04,
    GET_STATUS: 0x05,
    ALARM_OFF: 0x06,
    PLAY_MELODY: 0x07,
    GET_LUX: 0x08,
});

const EVENT = Object.freeze({
    INTRUSION: 0x01,
    ALARM_ON: 0x02,
    COUNTDOWN: 0x03,
    ALARM_TRIGGERED: 0x04,
});

const FIXED_COMMANDS = Object.freeze({
    led_on:        [MSG.CMD, DEVICE.LED, ACTION.ON, 0],
    led_off:       [MSG.CMD, DEVICE.LED, ACTION.OFF, 0],
    led_low:       [MSG.CMD, DEVICE.LED, ACTION.SET_BRIGHTNESS, 0x01],
    led_mid:       [MSG.CMD, DEVICE.LED, ACTION.SET_BRIGHTNESS, 0x02],
    led_high:      [MSG.CMD, DEVICE.LED, ACTION.SET_BRIGHTNESS, 0x03],
    buzzer_on:     [MSG.CMD, DEVICE.BUZZER, ACTION.ON, 0],
    buzzer_off:    [MSG.CMD, DEVICE.BUZZER, ACTION.OFF, 0],
    buzzer_melody: [MSG.CMD, DEVICE.BUZZER, ACTION.PLAY_MELODY, 0],
    query_led:     [MSG.QUERY, DEVICE.LED, ACTION.GET_STATUS, 0],
    query_buzzer:  [MSG.QUERY, DEVICE.BUZZER, ACTION.GET_STATUS, 0],
    query_sensor:  [MSG.QUERY, DEVICE.SENSOR, ACTION.GET_STATUS, 0],
    query_lux:     [MSG.QUERY, DEVICE.SENSOR, ACTION.GET_LUX, 0],
    segment_stop:  [MSG.CMD, DEVICE.SEGMENT, ACTION.OFF, 0],
});

function encodeMessage(fields) {
    if (!Array.isArray(fields) || fields.length !== MESSAGE_SIZE) {
        throw new TypeError('프로토콜 메시지는 4개 필드여야 합니다.');
    }
    if (!fields.every(value => Number.isInteger(value) && value >= 0 && value <= 255)) {
        throw new RangeError('프로토콜 필드는 0~255 정수여야 합니다.');
    }
    return Buffer.from(fields);
}

function decodeFrames(buffer) {
    if (!Buffer.isBuffer(buffer)) {
        throw new TypeError('TCP 입력은 Buffer여야 합니다.');
    }

    const messages = [];
    let offset = 0;
    while (buffer.length - offset >= MESSAGE_SIZE) {
        const [type, device, action, value] = buffer.subarray(offset, offset + MESSAGE_SIZE);
        messages.push({ type, device, action, value });
        offset += MESSAGE_SIZE;
    }

    return {
        messages,
        remaining: Buffer.from(buffer.subarray(offset)),
    };
}

function commandMessage(command, payload = {}) {
    if (command === 'segment') {
        const value = payload.value;
        if (!Number.isInteger(value) || value < 1 || value > 9) {
            throw new RangeError('카운트다운 값은 1~9 정수여야 합니다.');
        }
        return encodeMessage([MSG.CMD, DEVICE.SEGMENT, ACTION.SET_NUMBER, value]);
    }

    const fields = FIXED_COMMANDS[command];
    if (!fields) {
        throw new RangeError(`지원하지 않는 명령: ${String(command)}`);
    }
    return encodeMessage(fields);
}

module.exports = {
    ACTION,
    DEVICE,
    EVENT,
    MESSAGE_SIZE,
    MSG,
    commandMessage,
    decodeFrames,
    encodeMessage,
};
