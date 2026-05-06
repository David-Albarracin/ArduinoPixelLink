#include <Arduino.h>
#include <SoftwareSerial.h>
#include <MD_Parola.h>
#include <SPI.h>

#define HARDWARE_TYPE  MD_MAX72XX::FC16_HW
#define MAX_DEVICES    4
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

void setup() {
  Serial.begin(9600);
  BT.begin(BT_BAUD);

  display.begin();
  display.setIntensity(5);
  display.displayClear();
  display.displayScroll("Listo...", PA_CENTER, PA_SCROLL_LEFT, 60);

  Serial.println("Listo...");
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

  // Si hay texto acumulado y no llega nada en 300ms, mostrar igual
  if (mensaje.length() > 0 && !mensajeNuevo && millis() - ultimoByteMs > 300) {
    mensajeNuevo = true;
  }

  if (mensajeNuevo) {
    mensajeNuevo = false;
    Serial.print("Mostrando: ");
    Serial.println(mensaje);
    mensaje.toCharArray(bufDisplay, sizeof(bufDisplay));
    mensaje = "";
    display.displayClear();
    display.displayScroll(bufDisplay, PA_CENTER, PA_SCROLL_LEFT, 60);
  }

  if (display.displayAnimate()) {
    display.displayReset();
  }
}
