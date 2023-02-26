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

Lightweight 20x4 Character LCD driver for ESP8266

Note: The LCD data lines must be connected to the MCU as follows:
    LCD DB7..........GPIO15 (NodeMCU D8) *1-10kOhm pull-down resistor is required
    LCD DB6..........GPIO14 (NodeMCU D5)
    LCD DB5..........GPIO13 (NodeMCU D7)
    LCD DB4..........GPIO12 (NodeMCU D6)
    LCD RW...........GND
    LCD EN...........GPIO16 (NodeMCU D0)
*/

#ifndef LCD2004_H
#define LCD2004_H

#include <Arduino.h>
#include <inttypes.h>
#include <Print.h>

// Adjust according to LCD speed
#define DELAY_US_SETUP_HOLD     1
#define DELAY_US_INIT           5000
#define DELAY_US_DATA_COMMAND   75
#define DELAY_US_LONG_COMMAND   2700
#define DELAY_MS_POR            100

#define LCD_COLS                20
#define LCD_LINES               4

class LCD2004 : public Print
{
    public:
        LCD2004(uint8_t rs);
        void begin();
        void clear();
        void cmd(uint8_t val);
        void setCursor(uint8_t col, uint8_t row);
        size_t write(uint8_t val) override; // Print::write(uint8_t)
    private:
        uint8_t rs_pin;
};

#endif