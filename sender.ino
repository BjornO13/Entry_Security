/**
 * @file sender.ino
 * @brief ESP32-S3 RFID Timestamp Sender using ESP-NOW and RTC
 * 
 * This program reads an RFID tag using MFRC522, retrieves the UID and timestamp 
 * from an RTC module, displays it on an LCD, and sends the data wirelessly via ESP-NOW.
 * 
 * @author Soo bin Lee
 * @date 2025-03-17
 */

// ======== Includes ===================
#include <WiFi.h>
#include <esp_now.h>
#include <SPI.h>
#include <Wire.h>
#include "RTClib.h" 
#include <LiquidCrystal_I2C.h>

#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>

// Define SPI pins for ESP32-S3
#define SS_PIN  8   ///< RFID SDA (SS) pin
#define SCK  12     ///< SPI Clock pin
#define MOSI 11     ///< SPI MOSI pin
#define MISO 13     ///< SPI MISO pin

// Define I2C pins for RTC & LCD
#define SDA_R 21    ///< I2C SDA pin for RTC & LCD
#define SLA_R 19    ///< I2C SCL pin for RTC & LCD

// ======== Global Variables ===================

// Initialize the MFRC522 RFID module using SPI
MFRC522DriverPinSimple ss_pin(SS_PIN);
MFRC522DriverSPI driver{ss_pin};
MFRC522 rfid{driver};

// Initialize RTC and LCD
RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);

/**
 * @struct AccessData
 * @brief Stores the RFID UID and timestamp of the scan.
 */
typedef struct {
  char uid[10];   ///< RFID UID stored as a string
  char time[20];  ///< Timestamp of scan (HH:MM:SS)
} AccessData;

AccessData accessData;  ///< Stores scanned UID and timestamp

/// MAC address of the receiving ESP-NOW device
uint8_t accessMAC[] = {0x24, 0xEC, 0x4A, 0x0E, 0xC0, 0xCC};

/**
 * @brief Callback function for ESP-NOW message send status.
 * 
 * This function is triggered after attempting to send data via ESP-NOW. 
 * It displays whether the transmission was successful or failed.
 * 
 * @param macAddr MAC address of the recipient
 * @param status ESP-NOW transmission status
 */
void sendStatus(const uint8_t *macAddr, esp_now_send_status_t status) {
  Serial.print("ESP-NOW Send Status: ");
  lcd.clear();
  lcd.setCursor(0, 0);
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("Success");
    lcd.print("Data Sent");
  } else {
    Serial.println("Failed");
    lcd.print("Failed to Send");
  }
}

/**
 * @brief Initializes peripherals, communication protocols, and ESP-NOW.
 * 
 * This function sets up the RFID reader, RTC, LCD, and ESP-NOW communication.
 */
void setup() {
  Serial.begin(115200);
  Serial.println("Starting setup...");

  // Initialize SPI for RFID
  SPI.begin(SCK, MISO, MOSI, SS_PIN);
  rfid.PCD_Init();  // Initialize RFID module

  // Initialize I2C for RTC and LCD
  Wire.begin(SDA_R, SLA_R);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("Please Scan ID");
  Serial.println("LCD Initialized");
  delay(2000);

  // Initialize RTC module, use as debugging tool to make sure RTC is properly connected
  Serial.println("Initializing RTC...");
  if (!rtc.begin()) {
    Serial.println("RTC Error!");
    lcd.print("RTC Error!");
    while (1);
  }
  Serial.println("RTC Initialized.");

  // Set ESP32 and initialize ESP-NOW
  WiFi.mode(WIFI_STA);
  Serial.println("Initializing ESP-NOW...");
  // Debug statement
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Error!");
    lcd.print("ESP-NOW Error!");
    return;
  }
  Serial.println("ESP-NOW Initialized");

  // Register ESP-NOW callback function
  esp_now_register_send_cb(sendStatus);

  // Add peer device for ESP-NOW communication
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, accessMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  // Debug statement
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Peer Error!");
    lcd.print("Peer Error!");
    return;
  }
  Serial.println("ESP-NOW Peer Added");
}

/**
 * @brief Main loop that continuously checks for RFID scans and sends data.
 * 
 * If a valid RFID tag is detected, the UID and timestamp are displayed on 
 * the LCD and transmitted via ESP-NOW.
 */
void loop() {
  // Check if an RFID tag is detected
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  // Select the card and read the UID
  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("RFID Scanned!");
  delay(2000);

  // Read UID and format as a string
  String uidStr = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    uidStr += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) uidStr += ":"; // Separate bytes with colons
  }
  uidStr.toUpperCase(); // Convert to all uppercase to ensure consistency in formatting
  uidStr.toCharArray(accessData.uid, sizeof(accessData.uid)); // Converts string to C-style char array and stores result into accessData 

  // Get Timestamp from RTC
  DateTime now = rtc.now();
  sprintf(accessData.time, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());

  // Display UID and Timestamp on LCD/Users/saralee/Desktop/ee474/lab/Lee_final_project/Lee_final_project.ino
  lcd.clear();
  lcd.print("UID: ");
  lcd.print(accessData.uid);
  lcd.setCursor(0, 1);
  lcd.print(accessData.time);
  delay(5000);

  // Send Data to ESP-NOW (UID + Timestamp)
  esp_now_send(accessMAC, (uint8_t *)&accessData, sizeof(accessData));

  delay(2000);
}
