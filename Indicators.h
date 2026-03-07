// Indicators.h
#pragma once
#include <Arduino.h>

namespace Indicators {

struct Config {
  int16_t btnX  = 300;
  int16_t btnY  = 18;

  int16_t keyX  = 300;
  int16_t keyY  = 46;

  int16_t moboX = 300;
  int16_t moboY = 74;

  int16_t r     = 10;

  uint16_t colorOn   = 0x07E0; // green
  uint16_t colorOff  = 0x39E7; // gray
  uint16_t colorRing = 0xFFFF; // white
  uint16_t colorText = 0xFFFF; // white
};

struct StateCache {
  bool inited = false;
  bool lastBtn = false;
  bool lastKey = false;
  bool lastMobo = false;
};

template <typename DisplayT>
inline void drawCircleIndicator(DisplayT &lcd,
                                int16_t x, int16_t y, int16_t r,
                                char letter,
                                bool on,
                                const Config &cfg) {
  lcd.fillCircle(x, y, r, on ? cfg.colorOn : cfg.colorOff);
  lcd.drawCircle(x, y, r, cfg.colorRing);

  lcd.setTextColor(cfg.colorText);
  lcd.setTextSize(1);
  lcd.setCursor(x - 3, y - 4);
  lcd.print(letter);
}

template <typename DisplayT>
inline void redrawAll(DisplayT &lcd,
                      bool btnOn, bool keyOn, bool moboOn,
                      StateCache &cache,
                      const Config &cfg) {
  cache.inited = true;
  cache.lastBtn = btnOn;
  cache.lastKey = keyOn;
  cache.lastMobo = moboOn;

  drawCircleIndicator(lcd, cfg.btnX,  cfg.btnY,  cfg.r, 'B', btnOn,  cfg);
  drawCircleIndicator(lcd, cfg.keyX,  cfg.keyY,  cfg.r, 'K', keyOn,  cfg);
  drawCircleIndicator(lcd, cfg.moboX, cfg.moboY, cfg.r, 'M', moboOn, cfg);
}

template <typename DisplayT>
inline void update(DisplayT &lcd,
                   bool btnOn, bool keyOn, bool moboOn,
                   StateCache &cache,
                   const Config &cfg) {
  if (!cache.inited) {
    redrawAll(lcd, btnOn, keyOn, moboOn, cache, cfg);
    return;
  }

  if (btnOn != cache.lastBtn) {
    cache.lastBtn = btnOn;
    drawCircleIndicator(lcd, cfg.btnX, cfg.btnY, cfg.r, 'B', btnOn, cfg);
  }
  if (keyOn != cache.lastKey) {
    cache.lastKey = keyOn;
    drawCircleIndicator(lcd, cfg.keyX, cfg.keyY, cfg.r, 'K', keyOn, cfg);
  }
  if (moboOn != cache.lastMobo) {
    cache.lastMobo = moboOn;
    drawCircleIndicator(lcd, cfg.moboX, cfg.moboY, cfg.r, 'M', moboOn, cfg);
  }
}

} // namespace Indicators
