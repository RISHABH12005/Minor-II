#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN 10
#define RST_PIN 9

MFRC522 rfid(SS_PIN, RST_PIN);

void setup() {
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();

  Serial.println("Place any RFID card/tag...");
}

void loop() {
  // Detect new card/tag
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  Serial.println("\nCard/Tag Detected!");

  // Print UID
  Serial.print("UID: ");
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
    Serial.print(rfid.uid.uidByte[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  // Print card type
  Serial.print("Type: ");
  MFRC522::PICC_Type type = rfid.PICC_GetType(rfid.uid.sak);

  switch (type) {
    case MFRC522::PICC_TYPE_MIFARE_MINI:
      Serial.println("MIFARE Mini");
      break;
    case MFRC522::PICC_TYPE_MIFARE_1K:
      Serial.println("MIFARE 1K");
      break;
    case MFRC522::PICC_TYPE_MIFARE_4K:
      Serial.println("MIFARE 4K");
      break;
    case MFRC522::PICC_TYPE_MIFARE_UL:
      Serial.println("MIFARE Ultralight (Tag)");
      break;
    default:
      Serial.println("Unknown");
      break;
  }

  Serial.println("-------------------");

  rfid.PICC_HaltA();
}