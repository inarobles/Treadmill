/**
 * Test simple del fin de carrera de inclinación
 * Pin: GPIO 21 con pull-up interno
 * Conexión: Fin de carrera entre GPIO 21 y GND
 */

#define INCLINE_LIMIT_SWITCH_PIN 21

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Configurar GPIO 21 como entrada con pull-up interno
  pinMode(INCLINE_LIMIT_SWITCH_PIN, INPUT_PULLUP);

  Serial.println("========================================");
  Serial.println("  Test Fin de Carrera - GPIO 21");
  Serial.println("========================================");
  Serial.println("Pull-up interno: HABILITADO");
  Serial.println("Conexión: GPIO 21 <-> GND");
  Serial.println("");
  Serial.println("Estado esperado:");
  Serial.println("  - NO ACTIVADO (abierto)  -> PIN=HIGH (1)");
  Serial.println("  - ACTIVADO (cerrado GND) -> PIN=LOW (0)");
  Serial.println("========================================");
  Serial.println("");
}

void loop() {
  int pin_state = digitalRead(INCLINE_LIMIT_SWITCH_PIN);

  Serial.print("GPIO 21: ");
  Serial.print(pin_state);
  Serial.print(" -> Fin de carrera: ");

  if (pin_state == LOW) {
    Serial.println("✓ ACTIVADO (cinta a 0%)");
  } else {
    Serial.println("- NO activado (cinta > 0%)");
  }

  delay(500);  // Leer cada 500ms
}
