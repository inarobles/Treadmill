/**
 * Test simple del relé en GPIO 32 (INCLINE_DIRECTION_PIN)
 * Mantiene el relé activado permanentemente en LOW (NO)
 */

#define RELE_PIN_32 32

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Configurar GPIO 32 como salida
  pinMode(RELE_PIN_32, OUTPUT);

  // Activar relé en LOW (NO)
  digitalWrite(RELE_PIN_32, HIGH);

  Serial.println("========================================");
  Serial.println("  Test Relé GPIO 32 - Dirección Motor");
  Serial.println("========================================");
  Serial.println("");
  Serial.println("GPIO 32: LOW (0) -> Relé ACTIVADO (NO)");
  Serial.println("Manteniendo activado permanentemente...");
  Serial.println("========================================");
  Serial.println("");
}

void loop() {
  // No hacer nada - mantener el estado
  delay(1000);
}
