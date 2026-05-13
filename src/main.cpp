#include <Arduino.h>
#include <SoftwareSerial.h>
#include <MD_Parola.h>
#include <SPI.h>

#define HARDWARE_TYPE  MD_MAX72XX::FC16_HW
#define MAX_DEVICES    12
#define CLK_PIN        13
#define DATA_PIN       11
#define CS_PIN         10

MD_Parola display = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

#define BT_BAUD 38400

SoftwareSerial BT(3, 5);
String mensaje = "";
bool mensajeNuevo = false;
unsigned long ultimoByteMs = 0;
char bufDisplay[64] = "";

#define PHYS_W   32
#define PHYS_H   24
#define MOD_COLS 4
#define HEART_W  24
#define HEART_H  20

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

inline void drawPx(MD_MAX72XX *mx, uint8_t x, uint8_t y, bool on) {
  if (x >= PHYS_W || y >= PHYS_H) return;
  uint8_t mc = x / 8;
  uint8_t mr = y / 8;
  uint8_t dx = x % 8;
  uint8_t dy = y % 8;
  uint8_t modIdx = mr * MOD_COLS + mc;
  uint16_t logCol = (uint16_t)modIdx * 8 + dx;
  mx->setPoint(dy, logCol, on);
}

void pintarCorazon() {
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

void setup() {
  Serial.begin(9600);
  BT.begin(BT_BAUD);

  display.begin();
  display.setIntensity(5);
  pintarCorazon();

  Serial.println("");
}

void loop() {
  if (BT.available()) {
    uint8_t c = BT.read();
    ultimoByteMs = millis();

    Serial.print("0x");
    if (c < 16) Serial.print("0");
    Serial.print(c, HEX);
    if (c >= 32 && c < 127) { Serial.print(" = '"); Serial.print((char)c); Serial.println("'"); }
    else Serial.println(" (ctrl)");

    if (c == '\n') {
      if (mensaje.length() > 0) mensajeNuevo = true;
    } else if (c != '\r' && c >= 32 && c < 127) {
      mensaje += (char)c;
    }
  }

  if (mensaje.length() > 0 && !mensajeNuevo && millis() - ultimoByteMs > 300) {
    mensajeNuevo = true;
  }

  if (mensajeNuevo) {
    mensajeNuevo = false;
    String low = mensaje;
    low.toLowerCase();
    Serial.print("Recibido: ");
    Serial.println(mensaje);

    if (low == "corazon" || low == "heart") {
      mensaje = "";
      pintarCorazon();
      return;
    }

    mensaje.toCharArray(bufDisplay, sizeof(bufDisplay));
    mensaje = "";
    display.displayReset();
    display.displayClear();
    display.displayScroll(bufDisplay, PA_CENTER, PA_SCROLL_LEFT, 60);
  }

  if (display.displayAnimate()) {
    display.displayReset();
  }
}
