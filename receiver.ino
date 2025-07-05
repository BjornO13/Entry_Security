/**
 * @file receiver.ino
 * @brief ESP32-based employee tracking and door control system using FreeRTOS, ESP-NOW, and I2C LCD.
 * @author Bjorn Olsen
 * @date 3/17/2025
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <ESP32Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <esp_now.h>
#include <WiFi.h>

#define DOOR_PIN 4  ///< Servo pin for door control.
#define BUTTON_PIN 5  ///< Button pin for manual door control.

#define SDA 8  ///< I2C SDA pin.
#define SCL 9  ///< I2C SCL pin.

Servo Door_control;  ///< Servo object for door control.
QueueHandle_t DoorQueue;  ///< Queue handle for door control tasks.
QueueHandle_t RecvQueue;  ///< Queue handle for received access data.
LiquidCrystal_I2C lcd(0x27, 16, 2);  ///< LCD display object.

/**
 * @enum DoorState
 * @brief Enum representing door states.
 */
enum DoorState { 
  OPEN,  ///< Door is open.
  CLOSE  ///< Door is closed.
};

/**
 * @struct AccessData
 * @brief Structure to hold received access data.
 */
typedef struct {
  char uid[10];  ///< Employee UID.
  char time[20];  ///< Timestamp of access.
} AccessData;

/**
 * @struct Employee
 * @brief Linked list node for storing employee data.
 */
struct Employee {
  char uid[10];  ///< Employee UID.
  char entryTime[20];  ///< Entry timestamp.
  char exitTime[20];  ///< Exit timestamp.
  bool isEntering;  ///< Entry/exit status.
  struct Employee* next;  ///< Pointer to next employee node.
};

Employee* head = NULL;  ///< Head of the linked list for employee records.
bool Remote_Door = false;  ///< Flag to control door remotely.

/**
 * @brief ESP-NOW callback function to handle received data.
 * @param esp_now_info ESP-NOW metadata.
 * @param incomingData Pointer to received data.
 * @param len Length of received data.
 */
void IRAM_ATTR dataReceived(const esp_now_recv_info_t * esp_now_info, const uint8_t *incomingData, int len);

/**
 * @brief Adds an employee to the linked list.
 * @param head Pointer to the head of the list.
 * @param uid Employee UID.
 * @param time Timestamp of entry.
 */
void addEmployee(Employee** head, const char* uid, const char* time);

/**
 * @brief Calculates the total time spent inside.
 * @param entry Entry time string.
 * @param exit Exit time string.
 * @return Total time in seconds.
 */
int calculateTotalTime(const char* entry, const char* exit);

/**
 * @brief Task to check employee ID and manage entry/exit.
 * @param pvParameters Task parameters.
 */
void checkID(void *pvParameters);

/**
 * @brief FreeRTOS task to handle door control logic.
 * @param pvParameters Task parameters.
 */
void Task_DoorController(void *pvParameters);

/**
 * @brief FreeRTOS task to physically control the door servo.
 * @param pvParameters Task parameters.
 */
void Task_Door(void *pvParameters);


void IRAM_ATTR dataReceived(const esp_now_recv_info_t * esp_now_info, const uint8_t *incomingData, int len) {
  AccessData recvData; // Structure that receives ESP-NOW data
  memcpy(&recvData, incomingData, sizeof(recvData)); // Copy the revieved data into structure
  xQueueSendFromISR(RecvQueue, &recvData, NULL); // Send the structure to be preocessed with a queue
}

// Initializing fuction
void setup() {
  Serial.begin(115200); // Sets up Serial Communication
  WiFi.mode(WIFI_STA); // Wifi mode to match sender

  Wire.begin(SDA, SCL); // creates I2C wire for LCD
  lcd.init(); // initialize LCD
  lcd.backlight(); // keep backlight on
  lcd.clear(); // initializes screen to clear
  lcd.print("Ready"); // prints ready to communicate start

  Door_control.setPeriodHertz(50); // sets period to 50 MHz
  Door_control.attach(DOOR_PIN, 1000, 2000); // sets duty cycle min and max

  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Button pin initialization

  DoorQueue = xQueueCreate(1, sizeof(DoorState));  // Queue for Door State
  RecvQueue = xQueueCreate(1, sizeof(AccessData)); // Queue for Received Data

  xTaskCreate(Task_DoorController, "Door Controller", 2048, NULL, 1, NULL); // Task for Door Control
  xTaskCreate(Task_Door, "Door", 2048, NULL, 1, NULL); // Task for Door State
  xTaskCreate(checkID, "Check ID", 2048, NULL, 1, NULL); // Task for ID checking

  Serial.println("Initializing ESP-NOW..."); // Communication checking 
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed!"); // If failed, print failed and exit
    return;
  }
  esp_now_register_recv_cb(dataReceived); 
  Serial.println("ESP-NOW Initialized."); // ESP-NOW ready
}

void loop() {
  // Using FreeRTOS tasks
}

void checkID(void *pvParameters) {
  AccessData recvData; // Instance of structure
  while (1) {
    if (xQueueReceive(RecvQueue, &recvData, portMAX_DELAY) == pdPASS) {  // receives structure data from queue
      Employee* temp = head; // Head of the linked list
      
      while ((temp != NULL) && (strcmp(temp->uid, recvData.uid) != 0)) {
        temp = temp->next; // if the current node of the linked list is not null and the RFID tags are not the same, next node
      }

      if (temp == NULL) { // if the RFID tag is not in the linked list
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Set Employee?");
        lcd.setCursor(0,1);
        lcd.print("y/n"); // Ask manager if this is a new employee

        Serial.flush();  // Clear previous Serial input

        String input = "";  // initialize empty string
        while (input.length() == 0) {  // Wait for valid input
          if (Serial.available()) {
            input = Serial.readStringUntil('\n'); // once there is a new line character, input is ready
            input.trim();  
          }
          vTaskDelay(pdMS_TO_TICKS(100));  // Give time for input
        }

        Serial.print("Received Input: ");
        Serial.println(input);  // shows recieved input

        if (input.equalsIgnoreCase("y")) {  // if the input is y, employee is added to the linked list
          addEmployee(&head, recvData.uid, recvData.time);
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Employee Added");
          Remote_Door = true; // open the door
        } else {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Denied"); // Employee is not added and prints Denied
        }
      } else { // If the employee is in the linked list
        temp->isEntering = !temp->isEntering;  // update the entering boolean at that node
        if (temp->isEntering) { // if the employee is entering
          strcpy(temp->entryTime, recvData.time);
          Remote_Door = true; // open the door and set the entrance time to the received time
        } else { // if the employee is leaving
          strcpy(temp->exitTime, recvData.time); // set the leaving time to the received time
          int total_time = calculateTotalTime(temp->entryTime, temp->exitTime); // calculate total time
          Serial.print("Total Time Inside: ");
          Serial.print(total_time);
          Serial.println(" seconds"); // prints total time in seconds

          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Remove Employee?");
          lcd.setCursor(0, 1);
          lcd.print("y/n"); // ask if the employee should be removed

          Serial.flush();  // Clear previous Serial input

          String input = "";  
          while (input.length() == 0) {  // Wait for valid input
            if (Serial.available()) {
              input = Serial.readStringUntil('\n');
              input.trim();  
            } 
            vTaskDelay(pdMS_TO_TICKS(100));  // Give time for input
          }

          Serial.print("Received Input: ");
          Serial.println(input); // prints the received input

          if (input.equalsIgnoreCase("y")) { // if the employee should be removed
            removeEmployee(&head, recvData.uid); // remove the employee
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Employee Removed"); // print confirmation
          } else { // if the employee should not be removed
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Maintained"); // print confirmation
          }
        }
      }
      vTaskDelay(pdMS_TO_TICKS(1000)); // long delay if the CPU has to work
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // short delay 
  }
}

void addEmployee(Employee** head, const char* uid, const char* time) {
    Employee* newEmployee = (Employee*)malloc(sizeof(Employee)); // memory allocation of data for a single node
    if (!newEmployee) {
      Serial.println("Memory allocation failed!");
      return; // check if memory allocation failed
    }
    strcpy(newEmployee->uid, uid); // enter uid tag
    strcpy(newEmployee->entryTime, time); // enter time
    strcpy(newEmployee->exitTime, "\0"); // enter nothing for exit time
    newEmployee->isEntering = true; // set enter to true
    newEmployee->next = *head; // set the last head to the next link
    *head = newEmployee; // set the new head to the new node
    Serial.println("Employee added successfully."); // print confirmation
}

// Function to remove an employee by RFID
void removeEmployee(Employee** head, const char* rfid) {
    Employee* temp = *head; // set a pointer to head
    Employee* prev = NULL; // set a pointer to one before head

    while (temp != NULL && strcmp(temp->uid, rfid) != 0) { // find the node for removal
        prev = temp;
        temp = temp->next; 
    }

    if (temp == NULL) return;  // Employee not found

    if (prev == NULL) {
        *head = temp->next;  // Removing head node
    } else {
        prev->next = temp->next; // link the previous and the next node after the removed node
    }

    free(temp);  // Free memory
}

int calculateTotalTime(const char* entry, const char* exit) {
  int h1, m1, s1, h2, m2, s2; // ints of each component of entry and exit times
  sscanf(entry, "%d:%d:%d", &h1, &m1, &s1); // turn the strings into ints
  sscanf(exit, "%d:%d:%d", &h2, &m2, &s2);
  return ((h2 * 3600 + m2 * 60 + s2) - (h1 * 3600 + m1 * 60 + s1)); // calculate time in seconds
}

void Task_DoorController(void *pvParameters) {
  DoorState currentState = CLOSE; // set initial state to closed

  while (1) {
    int buttonState = digitalRead(BUTTON_PIN); // read the button press

    if (currentState == CLOSE) { // logic for closed state
      
      if ((buttonState == LOW) || (Remote_Door == true)) {  // Button pressed
        currentState = OPEN; // set the state to open
        Serial0.println("Door: Open"); // print confirmation
      }
    } else if (currentState == OPEN) { // logic for open state
      
      if (buttonState == LOW) {  // Button pressed
        currentState = CLOSE; // set state to closed
        Serial0.println("Door: Closed"); // print confirmation
        Remote_Door = false; // turn off remote door access 
        lcd.clear(); // clear lcd display
      }
    } else {
      currentState = CLOSE; // default statement
    }

    // Send the updated state to the queue
    xQueueSend(DoorQueue, &currentState, portMAX_DELAY);
    Remote_Door = false; 
    vTaskDelay(pdMS_TO_TICKS(300));  // Debounce delay
  }
}

void Task_Door(void *pvParameters) {
  DoorState receivedState;

  while (1) {
    // Wait for state from the queue
    if (xQueueReceive(DoorQueue, &receivedState, portMAX_DELAY) == pdPASS) { // if the queue is not empty
      if (receivedState == OPEN) { // if the current state is open
        Door_control.write(180); // servo control to open door
      } else {
        Door_control.write(0); // serco control to close door
      } 
    }

    vTaskDelay(pdMS_TO_TICKS(100));  // Prevents task from using too much CPU
  }
}
