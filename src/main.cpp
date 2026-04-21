// ============================================================================
//  PixelLink - Firmware
//  Recibe frames RGB 96x24 desde la app Ionic via Bluetooth (HC-05/06)
//  y los muestra en una matriz WS2812 (NeoPixel) de 96x24 = 2304 LEDs.
//
//  HARDWARE (recomendado):
//    - Arduino MEGA 2560 (necesitamos 8 KB de RAM; el UNO no alcanza).
//    - Matriz WS2812 96x24 conectada a pin D6 (5V + GND).
//    - HC-05 / HC-06 conectado a Serial1:
//        HC-05 TX  -> Arduino RX1 (pin 19)
//        HC-05 RX  -> Arduino TX1 (pin 18)  (usar divisor 1k/2k)
//        HC-05 VCC -> 5V, GND -> GND.
//      Configurar el HC-05 a 115200 bps (AT+UART=115200,0,0).
//
//  PROTOCOLO (coincide con PixelLink/home.page.ts):
//    [0xFF 0xFE 0xFD]                cabecera (3 bytes)
//    R G B  x (96 columnas * 24 filas) = 6912 bytes
//    [0xFD 0xFE 0xFF]                fin (3 bytes)
//
//  La app envia las celdas en orden fila-por-fila (row-major):
//    fila 0 col 0, fila 0 col 1, ..., fila 0 col 95, fila 1 col 0, ...
//
//  Por defecto asumimos un cableado en ZIG-ZAG (serpentine), que es el mas
//  comun al encadenar tiras. Cambia SERPENTINE a 0 si todas tus filas van
//  en el mismo sentido.
// ============================================================================

#include <Arduino.h>
#include <FastLED.h>

// --------- Configuracion de matriz --------------------------------------------
#define LED_PIN        6
#define COLS           96
#define ROWS           24
#define NUM_LEDS       (COLS * ROWS)
#define LED_TYPE       WS2812B
#define COLOR_ORDER    GRB
#define BRIGHTNESS     40     // 0..255  (ojo con el consumo)
#define SERPENTINE     1      // 1 si las filas van en zig-zag; 0 si todas iguales

// --------- Configuracion de Bluetooth / protocolo -----------------------------
#define BT_BAUD        115200
#define FRAME_BYTES    (NUM_LEDS * 3)    // 6912

static const uint8_t HEADER_0 = 0xFF;
static const uint8_t HEADER_1 = 0xFE;
static const uint8_t HEADER_2 = 0xFD;
static const uint8_t FOOTER_0 = 0xFD;
static const uint8_t FOOTER_1 = 0xFE;
static const uint8_t FOOTER_2 = 0xFF;

// --------- Estado --------------------------------------------------------------
CRGB leds[NUM_LEDS];

enum ParseState : uint8_t {
  WAIT_H0, WAIT_H1, WAIT_H2,
  READ_PAYLOAD,
  WAIT_F0, WAIT_F1, WAIT_F2
};
ParseState gState = WAIT_H0;

uint16_t payloadIndex = 0;
uint8_t  rgbTmp[3];
uint8_t  rgbStep = 0;

unsigned long lastByteMs = 0;
const unsigned long FRAME_TIMEOUT_MS = 2000;

static inline uint16_t xyToIndex(uint16_t col, uint16_t row) {
#if SERPENTINE
  if (row & 0x01) {
    return row * COLS + (COLS - 1 - col);
  } else {
    return row * COLS + col;
  }
#else
  return row * COLS + col;
#endif
}

static inline void storePixel(uint16_t pixelNumber, uint8_t r, uint8_t g, uint8_t b) {
  uint16_t row = pixelNumber / COLS;
  uint16_t col = pixelNumber % COLS;
  uint16_t idx = xyToIndex(col, row);
  if (idx < NUM_LEDS) {
    leds[idx] = CRGB(r, g, b);
  }
}

static inline void resetParser() {
  gState = WAIT_H0;
  payloadIndex = 0;
  rgbStep = 0;
}

void bootAnimation() {
  for (uint16_t col = 0; col < COLS; col++) {
    for (uint16_t row = 0; row < ROWS; row++) {
      uint8_t hue = (col * 255UL) / COLS;
      leds[xyToIndex(col, row)] = CHSV(hue, 255, 255);
    }
    FastLED.show();
    delay(8);
  }
  delay(300);
  FastLED.clear();
  FastLED.show();
}

void onByte(uint8_t b) {
  lastByteMs = millis();

  switch (gState) {
    case WAIT_H0:
      if (b == HEADER_0) gState = WAIT_H1;
      break;

    case WAIT_H1:
      if (b == HEADER_1) gState = WAIT_H2;
      else if (b == HEADER_0) gState = WAIT_H1;
      else gState = WAIT_H0;
      break;

    case WAIT_H2:
      if (b == HEADER_2) {
        gState = READ_PAYLOAD;
        payloadIndex = 0;
        rgbStep = 0;
      } else if (b == HEADER_0) {
        gState = WAIT_H1;
      } else {
        gState = WAIT_H0;
      }
      break;

    case READ_PAYLOAD: {
      rgbTmp[rgbStep++] = b;
      if (rgbStep == 3) {
        uint16_t pixelNumber = payloadIndex / 3;
        storePixel(pixelNumber, rgbTmp[0], rgbTmp[1], rgbTmp[2]);
        rgbStep = 0;
      }
      payloadIndex++;
      if (payloadIndex >= FRAME_BYTES) {
        gState = WAIT_F0;
      }
      break;
    }

    case WAIT_F0:
      if (b == FOOTER_0) gState = WAIT_F1;
      else { resetParser(); }
      break;

    case WAIT_F1:
      if (b == FOOTER_1) gState = WAIT_F2;
      else { resetParser(); }
      break;

    case WAIT_F2:
      if (b == FOOTER_2) {
        FastLED.show();
      }
      resetParser();
      break;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("PixelLink firmware iniciando..."));
  Serial.print(F("NUM_LEDS = ")); Serial.println(NUM_LEDS);
  Serial.print(F("FRAME_BYTES = ")); Serial.println(FRAME_BYTES);

  Serial1.begin(BT_BAUD);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS)
         .setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();

  bootAnimation();
  Serial.println(F("Listo. Esperando frames por BT..."));
}

void loop() {
  uint16_t n = 0;
  while (Serial1.available() && n < 256) {
    onByte((uint8_t)Serial1.read());
    n++;
  }

  if (gState != WAIT_H0 && (millis() - lastByteMs) > FRAME_TIMEOUT_MS) {
    Serial.println(F("[WARN] frame timeout - reset parser"));
    resetParser();
  }
}
