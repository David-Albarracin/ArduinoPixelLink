#include <Arduino.h>

// Ampliamos el buffer RX de SoftwareSerial ANTES del include (64 por defecto
// se queda corto cuando llegan ráfagas de 96 bytes de imagen).
#ifndef _SS_MAX_RX_BUFF
#define _SS_MAX_RX_BUFF 128
#endif
#include <SoftwareSerial.h>

#include <MD_Parola.h>
#include <SPI.h>

// =====================================================================
//  Hardware (NO cambiar — drivers y pines del proyecto)
// =====================================================================
#define HARDWARE_TYPE  MD_MAX72XX::FC16_HW
#define MAX_DEVICES    12       // 4 cols x 3 filas de matrices 8x8
#define CLK_PIN        13
#define DATA_PIN       11
#define CS_PIN         10

MD_Parola display = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

#define BT_BAUD 38400
SoftwareSerial BT(3, 5);

// =====================================================================
//  Resolución física
// =====================================================================
#define PHYS_W   32             // 4 módulos * 8 cols
#define PHYS_H   24             // 3 módulos * 8 filas
#define MOD_COLS 4
#define MOD_ROWS 3

// =====================================================================
//  ORIENTACIÓN DE LOS MÓDULOS
//
//  Si al recibir una imagen (comando IMG) la ves girada o espejada,
//  ajusta estos flags y vuelve a flashear el Arduino.
//
//  Recomendado: primero ENVÍA "TEST" desde la app — pinta una "F"
//  grande y marcas en las 4 esquinas. Comparando con lo que sale en
//  la matriz se ve qué hace falta tocar.
//
//  Significado de cada flag (poner 0 ó 1):
//    INVERT_X         -> espejo horizontal de toda la matriz
//    INVERT_Y         -> espejo vertical de toda la matriz
//    MOD_X_REVERSE    -> invierte el orden de los módulos dentro de cada fila
//    MOD_Y_REVERSE    -> invierte el orden de las filas de módulos
//    MODULE_TRANSPOSE -> rota 90° cada módulo (intercambia ejes dentro)
//
//  Combinaciones más comunes:
//    Imagen espejada horizontalmente -> INVERT_X = 1
//    Imagen al revés (cabeza abajo)  -> INVERT_X = 1 y INVERT_Y = 1
//    Imagen girada 90° -> MODULE_TRANSPOSE = 1 (+ ajustar inverts si hace falta)
// =====================================================================
#define INVERT_X          0
#define INVERT_Y          0
#define MOD_X_REVERSE     0
#define MOD_Y_REVERSE     0
#define MODULE_TRANSPOSE  0

// =====================================================================
//  Imagen
// =====================================================================
#define IMG_BYTES (PHYS_W * PHYS_H / 8)   // 96

enum RxState { ST_TEXT, ST_IMG };
RxState rxState = ST_TEXT;
uint8_t  imgBuf[IMG_BYTES];
uint16_t imgPos = 0;

String mensaje = "";
unsigned long ultimoByteMs = 0;
char bufDisplay[64] = "";
bool showingImage = false;   // true mientras matriz muestra bitmap (no animar)

// =====================================================================
//  Corazón hardcodeado (24x20)
// =====================================================================
#define HEART_W 24
#define HEART_H 20
const uint32_t HEART[HEART_H] = {
  0x0F00F0, 0x0F00F0,
  0x3FC3FC, 0x3FC3FC,
  0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF,
  0x3FFFFC, 0x3FFFFC,
  0x0FFFF0, 0x0FFFF0,
  0x03FFC0, 0x03FFC0,
  0x00FF00, 0x00FF00,
  0x003C00, 0x003C00
};

// =====================================================================
//  Dibujo de píxel con la orientación configurada
// =====================================================================
inline void drawPx(MD_MAX72XX *mx, uint8_t x, uint8_t y, bool on) {
  if (x >= PHYS_W || y >= PHYS_H) return;

  // 1) Espejos globales
#if INVERT_X
  x = PHYS_W - 1 - x;
#endif
#if INVERT_Y
  y = PHYS_H - 1 - y;
#endif

  // 2) Descomposición en (módulo, píxel dentro del módulo)
  uint8_t mc = x / 8;
  uint8_t mr = y / 8;
  uint8_t dx = x % 8;
  uint8_t dy = y % 8;

  // 3) Orden de los módulos
#if MOD_X_REVERSE
  mc = MOD_COLS - 1 - mc;
#endif
#if MOD_Y_REVERSE
  mr = MOD_ROWS - 1 - mr;
#endif

  // 4) Rotación del píxel dentro del módulo
#if MODULE_TRANSPOSE
  { uint8_t tmp = dx; dx = dy; dy = tmp; }
#endif

  uint8_t modIdx = mr * MOD_COLS + mc;
  uint16_t logCol = (uint16_t)modIdx * 8 + dx;
  mx->setPoint(dy, logCol, on);
}

// =====================================================================
//  Helpers de pintado
// =====================================================================
void pintarCorazon() {
  showingImage = true;
  display.displayReset();
  display.displayClear();
  MD_MAX72XX *mx = display.getGraphicObject();
  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
  uint8_t xOff = (PHYS_W - HEART_W) / 2;
  uint8_t yOff = (PHYS_H - HEART_H) / 2;
  for (uint8_t r = 0; r < HEART_H; r++) {
    for (uint8_t c = 0; c < HEART_W; c++) {
      bool on = (HEART[r] & ((uint32_t)1 << (HEART_W - 1 - c))) != 0;
      drawPx(mx, xOff + c, yOff + r, on);
    }
  }
  mx->update();
  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}

/**
 * Pinta una imagen monocroma de 32x24 empaquetada en 96 bytes
 * (8 px por byte, MSB primero, orden row-major).
 */
void pintarImagen(const uint8_t *buf) {
  showingImage = true;
  display.displayReset();
  display.displayClear();
  MD_MAX72XX *mx = display.getGraphicObject();
  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);

  for (uint8_t y = 0; y < PHYS_H; y++) {
    for (uint8_t x = 0; x < PHYS_W; x++) {
      uint16_t bitIdx = (uint16_t)y * PHYS_W + x;
      uint8_t  b      = buf[bitIdx >> 3];
      bool     on     = (b & (0x80 >> (bitIdx & 7))) != 0;
      drawPx(mx, x, y, on);
    }
  }

  mx->update();
  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}

/**
 * Patrón de calibración: una "F" grande en el centro + marcas
 * únicas en cada esquina.
 *
 *   - Esquina superior izq.:  1 píxel
 *   - Esquina superior der.:  línea horizontal de 3 px
 *   - Esquina inferior izq.:  línea vertical de 3 px
 *   - Esquina inferior der.:  cuadrado 3x3
 *
 * La "F" es asimétrica (no es la misma al rotar/espejar) por lo
 * que sirve para identificar cualquier transformación.
 */
void pintarTest() {
  showingImage = true;
  display.displayReset();
  display.displayClear();
  MD_MAX72XX *mx = display.getGraphicObject();
  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);

  // Esquinas
  drawPx(mx, 0, 0, true);                                // top-left = 1 px
  drawPx(mx, 29, 0, true); drawPx(mx, 30, 0, true); drawPx(mx, 31, 0, true); // top-right
  drawPx(mx, 0, 21, true); drawPx(mx, 0, 22, true); drawPx(mx, 0, 23, true); // bottom-left
  for (uint8_t y = 21; y <= 23; y++)
    for (uint8_t x = 29; x <= 31; x++)
      drawPx(mx, x, y, true);                            // bottom-right = 3x3

  // "F" en el centro (asimétrica)
  // Barra vertical
  for (uint8_t y = 4; y <= 19; y++) drawPx(mx, 11, y, true);
  // Barra horizontal superior
  for (uint8_t x = 11; x <= 23; x++) drawPx(mx, x, 4, true);
  // Barra horizontal media
  for (uint8_t x = 11; x <= 19; x++) drawPx(mx, x, 11, true);

  mx->update();
  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);

  Serial.println(F("Patron TEST pintado:"));
  Serial.println(F("  - Esquina sup. izq:  1 px"));
  Serial.println(F("  - Esquina sup. der:  3 px horizontal"));
  Serial.println(F("  - Esquina inf. izq:  3 px vertical"));
  Serial.println(F("  - Esquina inf. der:  cuadrado 3x3"));
  Serial.println(F("  - Centro:            letra F grande"));
}

// =====================================================================
//  Procesado de comandos de texto
// =====================================================================
void procesarComando() {
  if (mensaje.length() == 0) return;

  if (mensaje == "IMG") {
    rxState = ST_IMG;
    imgPos = 0;
    mensaje = "";
    Serial.println(F("Modo IMG: esperando 96 bytes..."));
    return;
  }

  if (mensaje == "TEST") {
    mensaje = "";
    pintarTest();
    return;
  }

  String low = mensaje;
  low.toLowerCase();
  Serial.print(F("Texto: "));
  Serial.println(mensaje);

  if (low == "corazon" || low == "heart") {
    mensaje = "";
    pintarCorazon();
    return;
  }

  mensaje.toCharArray(bufDisplay, sizeof(bufDisplay));
  mensaje = "";
  showingImage = false;
  display.displayReset();
  display.displayClear();
  display.displayScroll(bufDisplay, PA_CENTER, PA_SCROLL_LEFT, 60);
}

// =====================================================================
//  setup / loop
// =====================================================================
void setup() {
  Serial.begin(9600);
  BT.begin(BT_BAUD);

  display.begin();
  display.setIntensity(5);

  pintarCorazon();

  Serial.println(F("PixelLink listo (32x24)."));
  Serial.println(F("Comandos: TEST | IMG | corazon | <texto>"));
}

void loop() {
  while (BT.available()) {
    uint8_t c = BT.read();
    ultimoByteMs = millis();

    if (rxState == ST_IMG) {
      imgBuf[imgPos++] = c;
      if (imgPos >= IMG_BYTES) {
        rxState = ST_TEXT;
        Serial.println(F("Imagen completa. Pintando..."));
        pintarImagen(imgBuf);
      }
      continue;
    }

    if (c == '\n') {
      if (mensaje.length() > 0) procesarComando();
    } else if (c != '\r' && c >= 32 && c < 127) {
      mensaje += (char)c;
    }
  }

  if (rxState == ST_TEXT && mensaje.length() > 0 && millis() - ultimoByteMs > 300) {
    procesarComando();
  }

  if (!showingImage && display.displayAnimate()) {
    display.displayReset();
  }
}
