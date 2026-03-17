#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SD.h>
#include <SPI.h>
#include "gesture_data.h"  // Your exported gesture data

// Pin Definitions
#define FLEX_THUMB A0
#define FLEX_INDEX A1
#define FLEX_MIDDLE A2
#define FLEX_RING A3
#define FLEX_PINKY A6
#define SPEAK_BUTTON 8      // Button to trigger speech (pressed = LOW)
#define SPEAKER_PIN 9        // Speaker for beeps and speech

// SD Card pins
#define SD_CS 10
#define SD_MOSI 11
#define SD_MISO 12
#define SD_SCK 13

// LCD I2C address
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Smoothing
const int SMOOTH_WINDOW = 5;
int readings[5][SMOOTH_WINDOW];
int readIndex = 0;
int smoothed[5];

// Detection settings
const int TOLERANCE = 20;
unsigned long lastDetectionTime = 0;
const unsigned long DETECTION_COOLDOWN = 300;

// Current detected letter and word
char currentLetter = '?';
int currentMatches = 0;
String currentWord = "";     // For storing multi-letter words

// Speech variables
bool buttonPressed = false;
unsigned long lastButtonPress = 0;
const unsigned long BUTTON_DEBOUNCE = 50;  // Debounce time

void setup() {
  Serial.begin(9600);
  Serial.println("=========================");
  Serial.println("SIGN LANGUAGE DETECTOR v2");
  Serial.println("WITH SPEECH OUTPUT");
  Serial.println("=========================");
  
  // Initialize button with pull-up
  pinMode(SPEAK_BUTTON, INPUT_PULLUP);
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Sign Language");
  lcd.setCursor(0, 1);
  lcd.print("Push to Speak");
  Serial.println("LCD initialized");
  
  // Initialize SD card
  Serial.print("Initializing SD card... ");
  if (!SD.begin(SD_CS)) {
    lcd.setCursor(0, 1);
    lcd.print("SD Card Error!");
    Serial.println("FAILED");
  } else {
    lcd.setCursor(0, 1);
    lcd.print("SD Card OK     ");
    Serial.println("OK");
    
    // Create header in CSV file
    File dataFile = SD.open("gestures.csv", FILE_WRITE);
    if (dataFile) {
      dataFile.println("Time,Detected,Thumb,Index,Middle,Ring,Pinky");
      dataFile.close();
      Serial.println("CSV file created");
    }
  }
  
  // Initialize smoothing arrays
  for (int f = 0; f < 5; f++) {
    for (int i = 0; i < SMOOTH_WINDOW; i++) {
      readings[f][i] = 0;
    }
  }
  
  // Show gesture count
  Serial.print("Loaded ");
  Serial.print(GESTURE_COUNT);
  Serial.println(" gestures");
  
  // Beep to show ready
  playBeep(2);
  delay(2000);
  lcd.clear();
  
  Serial.println("System ready! Press button to speak detected letter.");
  Serial.println("-------------------------");
}

void loop() {
  // Read all 5 flex sensors
  int rawValues[5] = {
    analogRead(FLEX_THUMB),
    analogRead(FLEX_INDEX),
    analogRead(FLEX_MIDDLE),
    analogRead(FLEX_RING),
    analogRead(FLEX_PINKY)
  };
  
  // Smooth the readings
  smoothReadings(rawValues);
  
  // Detect gesture
  detectGesture();
  
  // Check button for speech
  checkSpeechButton();
  
  // Update LCD
  updateLCD();
  
  // Log to SD card (every 20 readings)
  static int counter = 0;
  if (++counter >= 20) {
    logToSD();
    counter = 0;
  }
  
  // Print to Serial
  printToSerial();
  
  delay(50);
}

void smoothReadings(int raw[5]) {
  for (int f = 0; f < 5; f++) {
    readings[f][readIndex] = raw[f];
  }
  readIndex = (readIndex + 1) % SMOOTH_WINDOW;
  
  for (int f = 0; f < 5; f++) {
    long sum = 0;
    for (int i = 0; i < SMOOTH_WINDOW; i++) {
      sum += readings[f][i];
    }
    smoothed[f] = sum / SMOOTH_WINDOW;
  }
}

void detectGesture() {
  int bestMatch = -1;
  int bestMatches = 0;
  
  for (int g = 0; g < GESTURE_COUNT; g++) {
    int fingersMatching = 0;
    
    for (int f = 0; f < 5; f++) {
      int target = GESTURE_VALUES[g][f];
      int current = smoothed[f];
      
      if (abs(current - target) <= TOLERANCE) {
        fingersMatching++;
      }
    }
    
    if (fingersMatching > bestMatches) {
      bestMatches = fingersMatching;
      bestMatch = g;
    }
  }
  
  if (bestMatch >= 0 && bestMatches >= 4) {
    char newLetter = GESTURE_NAMES[bestMatch];
    
    if (newLetter != currentLetter && millis() - lastDetectionTime > DETECTION_COOLDOWN) {
      currentLetter = newLetter;
      currentMatches = bestMatches;
      lastDetectionTime = millis();
      
      // Add to word if building a word
      if (currentWord.length() < 10) {  // Limit word length
        currentWord += currentLetter;
      }
      
      Serial.print("✅ DETECTED: ");
      Serial.print(currentLetter);
      Serial.print(" (");
      Serial.print(currentMatches);
      Serial.println("/5)");
      
      if (bestMatches == 5) {
        playBeep(2);
      } else {
        playBeep(1);
      }
    }
  } else if (bestMatches < 4) {
    if (millis() - lastDetectionTime > 1000) {
      currentLetter = '?';
      currentMatches = 0;
    }
  }
}

void checkSpeechButton() {
  // Read button (LOW when pressed due to pull-up)
  bool buttonState = (digitalRead(SPEAK_BUTTON) == LOW);
  
  // Debounce
  if (buttonState && !buttonPressed && (millis() - lastButtonPress > BUTTON_DEBOUNCE)) {
    buttonPressed = true;
    lastButtonPress = millis();
    
    // Button pressed - speak the current letter or word
    speakCurrent();
    
  } else if (!buttonState && buttonPressed) {
    buttonPressed = false;
  }
}

void speakCurrent() {
  // First beep to indicate speaking
  tone(SPEAKER_PIN, 1500, 50);
  delay(100);
  
  // Determine what to speak
  String textToSpeak = "";
  
  if (currentWord.length() > 0) {
    // We have a word built
    textToSpeak = currentWord;
    Serial.print("🔊 SPEAKING WORD: ");
    Serial.println(textToSpeak);
    
    // Clear the word after speaking
    currentWord = "";
    
  } else if (currentLetter != '?') {
    // Speak single letter
    textToSpeak = String(currentLetter);
    Serial.print("🔊 SPEAKING LETTER: ");
    Serial.println(textToSpeak);
  } else {
    // Nothing to speak
    Serial.println("🔇 Nothing to speak");
    playBeep(1);  // Single beep for "no speech"
    return;
  }
  
  // Generate speech patterns for each character
  for (int i = 0; i < textToSpeak.length(); i++) {
    char c = textToSpeak[i];
    speakCharacter(c);
    delay(200);  // Pause between characters
  }
  
  // Ending beep
  delay(100);
  tone(SPEAKER_PIN, 2000, 100);
}

void speakCharacter(char c) {
  // Simple tone patterns for each letter
  // Each letter gets a unique pattern of beeps
  int baseFreq = 400 + (c - 'A') * 30;  // Different pitch per letter
  
  switch(c) {
    case 'A': tone(SPEAKER_PIN, 400, 150); break;
    case 'B': tone(SPEAKER_PIN, 430, 150); break;
    case 'C': tone(SPEAKER_PIN, 460, 150); break;
    case 'D': tone(SPEAKER_PIN, 490, 150); break;
    case 'E': tone(SPEAKER_PIN, 520, 150); break;
    case 'F': tone(SPEAKER_PIN, 550, 150); break;
    case 'G': tone(SPEAKER_PIN, 580, 150); break;
    case 'H': tone(SPEAKER_PIN, 610, 150); break;
    case 'I': tone(SPEAKER_PIN, 640, 150); break;
    case 'J': tone(SPEAKER_PIN, 670, 150); break;
    case 'K': tone(SPEAKER_PIN, 700, 150); break;
    case 'L': tone(SPEAKER_PIN, 730, 150); break;
    case 'M': tone(SPEAKER_PIN, 760, 150); break;
    case 'N': tone(SPEAKER_PIN, 790, 150); break;
    case 'O': tone(SPEAKER_PIN, 820, 150); break;
    case 'P': tone(SPEAKER_PIN, 850, 150); break;
    case 'Q': tone(SPEAKER_PIN, 880, 150); break;
    case 'R': tone(SPEAKER_PIN, 910, 150); break;
    case 'S': tone(SPEAKER_PIN, 940, 150); break;
    case 'T': tone(SPEAKER_PIN, 970, 150); break;
    case 'U': tone(SPEAKER_PIN, 1000, 150); break;
    case 'V': tone(SPEAKER_PIN, 1030, 150); break;
    case 'W': tone(SPEAKER_PIN, 1060, 150); break;
    case 'X': tone(SPEAKER_PIN, 1090, 150); break;
    case 'Y': tone(SPEAKER_PIN, 1120, 150); break;
    case 'Z': tone(SPEAKER_PIN, 1150, 150); break;
    default: tone(SPEAKER_PIN, 500, 100); break;
  }
  delay(200);
  noTone(SPEAKER_PIN);
}

void updateLCD() {
  lcd.setCursor(0, 0);
  lcd.print("Sign: ");
  lcd.print(currentLetter);
  lcd.print("   ");
  
  lcd.setCursor(0, 1);
  if (currentWord.length() > 0) {
    // Show the word being built
    lcd.print("Word:");
    lcd.print(currentWord);
    // Pad with spaces to clear rest of line
    for (int i = 5 + currentWord.length(); i < 16; i++) {
      lcd.print(" ");
    }
  } else if (currentMatches == 5) {
    lcd.print("PERFECT!      ");
  } else if (currentMatches == 4) {
    lcd.print("Good match    ");
  } else {
    lcd.print("Press to speak ");
  }
}

void logToSD() {
  File dataFile = SD.open("gestures.csv", FILE_WRITE);
  if (dataFile) {
    dataFile.print(millis());
    dataFile.print(",");
    dataFile.print(currentLetter);
    dataFile.print(",");
    for (int i = 0; i < 5; i++) {
      dataFile.print(smoothed[i]);
      if (i < 4) dataFile.print(",");
    }
    dataFile.println();
    dataFile.close();
  }
}

void playBeep(int count) {
  for (int i = 0; i < count; i++) {
    tone(SPEAKER_PIN, 1000, 100);
    delay(150);
    noTone(SPEAKER_PIN);
    delay(50);
  }
}

void printToSerial() {
  static unsigned long lastPrint = 0;
  
  if (millis() - lastPrint > 500) {
    lastPrint = millis();
    
    Serial.print("Values: ");
    for (int i = 0; i < 5; i++) {
      Serial.print(smoothed[i]);
      if (i < 4) Serial.print(",");
    }
    Serial.print(" -> ");
    Serial.print(currentLetter);
    Serial.print(" (");
    Serial.print(currentMatches);
    Serial.print("/5) | Word: ");
    Serial.println(currentWord);
  }
}