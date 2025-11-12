/**
 * Test simple del relé en GPIO 32 (INCLINE_DIRECTION_PIN)
 * Alterna el relé cada 2 segundos entre NC y NO
 */

#define RELE_PIN_32 32

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Configurar GPIO 32 como salida
  pinMode(RELE_PIN_32, OUTPUT);

  Serial.println("========================================");
  Serial.println("  Test Relé GPIO 32 - Dirección Motor");
  Serial.println("========================================");
  Serial.println("");
  Serial.println("Alternando cada 2 segundos:");
  Serial.println("  HIGH (1) = NC");
  Serial.println("  LOW  (0) = NO");
  Serial.println("========================================");
  Serial.println("");
}

void loop() {
  // Activar NC (HIGH)
  digitalWrite(RELE_PIN_32, HIGH);
  Serial.println("GPIO 32: HIGH (1) -> Relé en posición NC");
  delay(2000);

  // Activar NO (LOW)
  digitalWrite(RELE_PIN_32, LOW);
  Serial.println("GPIO 32: LOW  (0) -> Relé en posición NO");
  delay(2000);
}
