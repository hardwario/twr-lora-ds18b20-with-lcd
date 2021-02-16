var cursor = 0;
var buffer;

function Decoder(bytes, port) {
    buffer = bytes;

    var header = u8();
    var voltage = u8();
    var temperature = s16();

    return {
        header: header,
        voltage: voltage === 0xff ? null : voltage / 10,
        temperature: temperature === 0x7fff ? null : temperature / 10,
    };
}

function u8() {
    var value = buffer.slice(cursor);
    value = value[0];
    cursor = cursor + 1;
    return value;
}

function s16() {
    var value = buffer.slice(cursor);
    value = value[1] | value[0] << 8;
    if ((value & (1 << 15)) > 0) {
        value = (~value & 0xffff) + 1;
        value = -value;
    }
    cursor = cursor + 2;
    return value;
}
