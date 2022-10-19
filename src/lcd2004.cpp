/*
MIT License

Copyright (c) 2022 Dolen Le

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "lcd2004.h"

#include <inttypes.h>

LCD2004::LCD2004(uint8_t rs, uint8_t en) {
    rs_pin = rs;
    en_pin = en;
}

void LCD2004::begin() {
    delay(DELAY_MS_POR);
    pinMode(rs_pin, OUTPUT);
    pinMode(en_pin, OUTPUT);

    pinMode(15, OUTPUT);
    pinMode(14, OUTPUT);
    pinMode(13, OUTPUT);
    pinMode(12, OUTPUT);

    // Initialization sequence
    GPOC = 0xF000;
    GPOS = 0x3000;
    for(uint8_t i=0; i<3; i++) {
        delayMicroseconds(DELAY_US_SETUP_HOLD);
        GPOC = (1 << en_pin);
        delayMicroseconds(DELAY_US_SETUP_HOLD);
        GPOS = (1 << en_pin);
        delayMicroseconds(DELAY_US_INIT);
    }
    
    // Set 4-bit mode
    GPOC = 0x1000;
    delayMicroseconds(DELAY_US_SETUP_HOLD);
    GPOC = (1 << en_pin);
    delayMicroseconds(DELAY_US_SETUP_HOLD);
    GPOS = (1 << en_pin);
    delayMicroseconds(DELAY_US_DATA_COMMAND);

    // Function Set
    cmd(0x28); // 4-bit mode, 2 lines, 5x8 font
    delayMicroseconds(DELAY_US_DATA_COMMAND);

    cmd(0x0C); // Display on
    delayMicroseconds(DELAY_US_DATA_COMMAND);

    cmd(0x06); // Entry mode auto-increment
    delayMicroseconds(DELAY_US_DATA_COMMAND);
}

void LCD2004::clear() {
    cmd(0x01);
    delayMicroseconds(DELAY_US_LONG_COMMAND);
}

void LCD2004::cmd(uint8_t val) {
    GPOC = (1 << rs_pin);
    delayMicroseconds(DELAY_US_SETUP_HOLD);
    write(val);
    GPOS = (1 << rs_pin);
    delayMicroseconds(DELAY_US_DATA_COMMAND);
}

size_t IRAM_ATTR LCD2004::write(uint8_t val) {
    GPOC = 0xF000;
    GPOS = ((uint32_t)(val & 0xF0)) << 8;
    delayMicroseconds(DELAY_US_SETUP_HOLD);
    GPOC = (1 << en_pin);
    GPOS = (1 << en_pin);
    GPOC = 0xF000;
    GPOS = ((uint32_t)(val & 0x0F)) << 12;
    delayMicroseconds(DELAY_US_SETUP_HOLD);
    GPOC = (1 << en_pin);
    GPOS = (1 << en_pin);
    delayMicroseconds(DELAY_US_DATA_COMMAND);
    return 1;
}

void LCD2004::setCursor(uint8_t col, uint8_t row) {
    const uint8_t row_addrs[] = { 0x00, 0x40, 0x14, 0x54 }; // For regular LCDs
    cmd(0x80 | (col + row_addrs[row]));
}