#include <SoftwareSerial.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

SoftwareSerial sim800(10, 11);   // RX, TX
LiquidCrystal_I2C lcd(0x27, 16, 2);

#define RELAY 7
#define CURRENT_SENSOR A0
#define VOLTAGE_SENSOR A1

// ==========================================
// 🚨 PRESENTATION WHITELIST NUMBERS
// ==========================================
const String allowedNumber1 = "+919611245387"; 
const String allowedNumber2 = "+917483725854"; 

bool motorState = false;
bool callActive = false;
String gsmData = "";
float voltage = 0.0;
float current = 0.0;

unsigned long lastSensorRead = 0; 

// CENTRALIZED DISPLAY FUNCTION
void updateDisplay()
{
  lcd.clear();
  if (motorState)
  {
    lcd.setCursor(0, 0);
    lcd.print("MOTOR RUNNING");
    lcd.setCursor(0, 1);
    lcd.print("V:"); lcd.print(voltage, 1);
    lcd.print(" I:"); lcd.print(current, 1);
  }
  else
  {
    lcd.setCursor(0, 0);
    lcd.print("AGRI PUMP CTRL");
    lcd.setCursor(0, 1);
    lcd.print("SYSTEM READY");
  }
}

void motorON()
{
  digitalWrite(RELAY, LOW);
  motorState = true;
  readSensors();
  updateDisplay(); 
  Serial.println("MOTOR ON");
}

void motorOFF()
{
  digitalWrite(RELAY, HIGH);  
  motorState = false;
  updateDisplay();
  Serial.println("MOTOR OFF");
}

void clearGSM()
{
  gsmData = "";
  while (sim800.available())
  {
    sim800.read();
  }
}

void deleteSMS()
{
  sim800.println("AT+CMGD=1,4");
}

void sendConfirmationSMS(String message)
{
  Serial.println("Sending SMS...");
  sim800.println("AT+CMGF=1");          
  delay(200); 
  sim800.print("AT+CMGS=\"");
  sim800.print(allowedNumber1); 
  sim800.println("\"");
  delay(500);                           
  sim800.print(message);                
  delay(200);
  sim800.write(26); // CTRL+Z
  delay(1000); // Give network time to process                          
  Serial.println("SMS Sent.");
}

void readSensors()
{
  int currentRaw = analogRead(CURRENT_SENSOR);
  int voltageRaw = analogRead(VOLTAGE_SENSOR);
  voltage = ((voltageRaw * 5.0) / 1023.0) * 5.0;
  current = ((currentRaw * 5.0) / 1023.0) - 2.5;
  current = current / 0.185;
  if(current < 0) current = -current;
}

bool waitForNetwork()
{
  unsigned long startTime = millis();
  while (millis() - startTime < 20000) 
  {
    sim800.println("AT+CREG?");
    unsigned long t = millis();
    String response = "";
    while (millis() - t < 1000)
    {
      while (sim800.available()) response += (char)sim800.read();
    }
    if (response.indexOf("+CREG: 0,1") >= 0 || response.indexOf("+CREG: 0,5") >= 0) return true;
  }
  return false;
}

void setup()
{
  pinMode(RELAY, OUTPUT);
  digitalWrite(RELAY, HIGH);

  Serial.begin(9600);
  sim800.begin(9600);

  lcd.init();
  lcd.backlight();
  
  // WELCOME SCREEN
  lcd.clear();
  lcd.setCursor(4, 0);
  lcd.print("WELCOME");
  lcd.setCursor(1, 1);
  lcd.print("AGRI PUMP CTRL");
  delay(2500); 
  
  lcd.clear();
  lcd.print("CHECKING");
  lcd.setCursor(0, 1);
  lcd.print("NETWORK...");

  // INITIALIZATION COMMANDS
  sim800.println("AT"); delay(100);
  sim800.println("AT+CSCS=\"GSM\""); delay(100); // Fixes SMS text issues
  sim800.println("AT+GSMBUSY=0"); delay(100);
  sim800.println("AT+CLIP=1"); delay(100);
  sim800.println("AT+DDET=1"); delay(100);
  sim800.println("AT+CMGF=1"); delay(100);
  sim800.println("AT+CNMI=1,2,0,0,0"); delay(100);

  if (waitForNetwork())
  {
    updateDisplay();
    Serial.println("NETWORK REGISTERED");
  }
  else
  {
    lcd.clear();
    lcd.print("NO NETWORK");
    Serial.println("NETWORK FAILED");
  }
  clearGSM();
}

void loop()
{
  while (sim800.available())
  {
    char c = sim800.read();
    gsmData += c;
    if (gsmData.length() > 150) gsmData = "";

    if (gsmData.indexOf("+DTMF: 1") >= 0)
    {
      sim800.println("ATH"); callActive = false; motorON(); 
      sendConfirmationSMS("Motor is RUNNING"); clearGSM(); return;
    }
    
    if (gsmData.indexOf("+DTMF: 2") >= 0)
    {
      sim800.println("ATH"); callActive = false; motorOFF(); 
      sendConfirmationSMS("Motor is OFF. System Ready"); clearGSM(); return;
    }

    if (gsmData.indexOf("+DTMF: 3") >= 0)
    {
      sim800.println("ATH"); callActive = false; readSensors();
      String statusMsg = motorState ? "Motor: ON | " : "Motor: OFF | ";
      statusMsg += "V: " + String(voltage, 1) + "V | I: " + String(current, 1) + "A";
      sendConfirmationSMS(statusMsg); clearGSM(); return;
    }
  }

  if (!callActive && (millis() - lastSensorRead > 3000))
  {
    readSensors();
    if(motorState) updateDisplay(); 
    lastSensorRead = millis();
  }

  if (gsmData.indexOf("RING") >= 0 && !callActive)
  {
    delay(300);
    while(sim800.available()) gsmData += (char)sim800.read();
    
    if (gsmData.indexOf(allowedNumber1) >= 0 || gsmData.indexOf(allowedNumber2) >= 0)
    {
      sim800.println("ATA"); callActive = true; lcd.clear(); lcd.print("CALL ACTIVE");
    }
    else
    {
      sim800.println("ATH"); lcd.clear(); lcd.print("CALL BLOCKED"); delay(2000); updateDisplay();
    }
    gsmData = "";
  }

  if (gsmData.indexOf("+CMT:") >= 0)
  {
    delay(200); 
    while (sim800.available()) gsmData += (char)sim800.read();

    if (gsmData.indexOf(allowedNumber1) >= 0 || gsmData.indexOf(allowedNumber2) >= 0)
    {
      if (gsmData.indexOf("STATUS") >= 0)
      {
        readSensors(); sendConfirmationSMS("V:" + String(voltage,1) + " I:" + String(current,1));
        deleteSMS(); clearGSM();
      }
      else if (gsmData.indexOf("OFF") >= 0)
      {
        motorOFF(); deleteSMS(); clearGSM();
      }
      else if (gsmData.indexOf("ON") >= 0)
      {
        motorON(); deleteSMS(); clearGSM();
      }
    }
    else { deleteSMS(); clearGSM(); }
  }

  if (gsmData.indexOf("NO CARRIER") >= 0 || gsmData.indexOf("BUSY") >= 0)
  {
    callActive = false; updateDisplay(); clearGSM();
  }
}