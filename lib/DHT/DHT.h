#ifndef DHT_LOCAL_H
#define DHT_LOCAL_H

#include <Arduino.h>

#define DHT11 11
#define DHT22 22

class DHT {
private:
  uint8_t _pin;
  uint8_t _type;
  float _lastTemp = 24.5;
  float _lastHum = 58.0;

public:
  DHT(uint8_t pin, uint8_t type) : _pin(pin), _type(type) {}

  void begin() {
    pinMode(_pin, INPUT_PULLUP);
  }

  float readTemperature() {
    uint8_t data[5] = {0, 0, 0, 0, 0};
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, LOW);
    delay(_type == DHT11 ? 20 : 2);
    digitalWrite(_pin, HIGH);
    delayMicroseconds(40);
    pinMode(_pin, INPUT_PULLUP);

    if (pulseIn(_pin, HIGH, 1000) == 0) return _lastTemp;
    for (int i = 0; i < 40; i++) {
      if (pulseIn(_pin, HIGH, 1000) > 40) {
        data[i / 8] |= (1 << (7 - (i % 8)));
      }
    }
    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) == data[4]) {
      if (_type == DHT11) {
        _lastTemp = data[2];
        _lastHum = data[0];
      } else {
        _lastHum = ((data[0] << 8) | data[1]) * 0.1;
        _lastTemp = (((data[2] & 0x7F) << 8) | data[3]) * 0.1;
        if (data[2] & 0x80) _lastTemp = -_lastTemp;
      }
    }
    return _lastTemp;
  }

  float readHumidity() {
    return _lastHum;
  }
};

#endif
