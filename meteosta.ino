/*
  Namisto obvykleho vypisu do seriove linky pomoci Serial.print()
  pouziji knihovnu Streaming a format Serial << promenna << promenna atd.
  Streaming stahnete z webu http://arduiniana.org/libraries/streaming/
*/
#include <Streaming.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <ArduinoJson.h>
#include <Time.h>
#include <HTTPSRedirect.h>

#define LED_BLUE 2

/*
  Objekty tlakomeru, teplomeru a weboveho serveru,
  ktery se spusti na standardnim portu 80
*/
Adafruit_SHT31 teplomer = Adafruit_SHT31();
ESP8266WebServer server(80);

// Promenne meteorologickych udaju
//float teplota0 = 0.0f, tlak = 0.0f;
float temperature = 0.0f;
uint8_t humidity = 0;

// Prihlasovaci udaje k Wi-Fi
//const char ssid[] = "Bondy free wifi";
//const char heslo[] = "iloveeman";

const char ssid[] = "Bondy paid wifi";
const char heslo[] = "iadmireeman";

//const char ssid[] = "iPhone";
//const char heslo[] = "SkodaSuperb";

const char* host = "script.google.com";
const int httpsPort = 443;
const char *GScriptId = "AKfycbxG0DEB0R-i-5nlqDvbTBSxLG1G9SJhYUvJipXCMdsF880Ak44";

// Write to Google Spreadsheet
String url = String("/macros/s/") + GScriptId + "/exec?";

HTTPSRedirect* client = nullptr;
String payload = "";

/*
  Nemenny HTML kod stranky
  ulozeny ve flashove pameti cipu
*/
PROGMEM const char html_hlavicka[] = "<!DOCTYPE html><html><head><title>Meteostanice</title><meta http-equiv=\"refresh\" content=\"70\"><style>html,body{font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;margin:0;padding:0;display:flex;justify-content:center;align-items:center;width:100%;height:100%;overflow:hidden;background-color:black;}div{font-size:10vw;color:grey;resize:none;overflow:auto;}.value{color:white;font-weight:bolder;}</style></head><body><div>";
PROGMEM const char html_paticka[] = "</div></body>";

/*
  Pomocna promenna casovace, ktery kazdou
  minutu aktualizuje udaje ze senzoru
*/
uint64_t posledniObnova = 0;
uint64_t ledBlinkInterval = 0;
bool flag = false;
bool flagWifi = false;

// Funkce setup se zpracuje hned po spusteni
void setup() {

  pinMode(LED_BLUE, OUTPUT);
  
  // Nastartuj seriovoou linku na rychlosti 115 200 bps
  Serial.begin(115200);
  // Dvakrat odradkuj seriovou linku a napis uvitani
  Serial << endl << endl;
  Serial << "===============================" << endl;
  Serial << "=== M E T E O S T A N I C E ===" << endl;
  Serial << "===============================" << endl << endl;

  /*
    Pokud se nepodarilo nastartovat tlakomer
    nebo teplomer (I2C adresa 0x45 je napsana na jeho desticce),
    vypis do seriove linky chybove hlaseni a zastav dalsi zpracovavani
  */
  if (!teplomer.begin(0x45)) {
    Serial << "Teplomer neodpovida. Zkontroluj zapojeni!" << endl;
    while (1) {}
  }

  /*
    Precti hodnoty ze sensoru a vypis
    je do seriove linky
  */
  ziskejHodnoty();
  connectToWifi();
  

  client = new HTTPSRedirect(httpsPort);
  client->setPrintResponseBody(true);
  client->setContentTypeHeader("application/json");

  Serial.print("Connecting to ");
  Serial.println(host);
  digitalWrite(LED_BLUE, LOW);
  
  /*
  Try to connect 5x, then return
  */
  
  for (int i = 0; i < 5; i++) {
    int retval = client->connect(host, httpsPort);
    if (retval == 1) {
      flag = true;
      Serial.println("Connected to host!");
      blueLedTurnOff();
      blueLedBlink(250,4);
      break;
    }
    else
      Serial.println("Connection failed. Retrying...");
      Serial << "Error: " << retval << endl; 
  }


  if (!flag) {
    Serial.print("Could not connect to server: ");
    Serial.println(host);
    Serial.println("Exiting...");
    return;
  }


  /*
    Nastaveni weboveho serveru. Zareaguje, pokud zachyti pozadavek GET /,
    tedy pozadavek na korenovy adresar. Stejne tak bych mohl nastavit
    reakce na fiktivny stranky, treba server.on("/index.html" a tak dale.
  */
  server.on("/", []() {
    // Ziskej parametr URL jmenem api, tedy /?api=...
    String api = server.arg("api");
    // Preved jej na mala pismena
    api.toLowerCase();
    /*
      Pokud URL parametr obsahuje text 'json',
      posli klientovi HTTP kod 200 a JSON s hodnotami ze senzoru
      a vypis do seriove linky IP adresu klienta
    */
    if (api == "json") {
      server.send(200, "application/json", "{\"teplota\":" + String(temperature, 2) + ",\"humidity\":" + String(humidity) + "}");
      Serial << "HTTP GET: Klient si stahl JSON data" << endl << endl;
    }
    /*
      Pokud URL parametr obsahuje neco jineho, nebo
      neexistuje, posli klientovi HTTP kod 200 a HTML stranku
    */
    else {
      server.send(200, "text/html", String(html_hlavicka) + "Teplota: <span class=\"value\">" + String(temperature, 2) + " &#x00B0;C</span><br/>humidity: <span class=\"value\">" + String(humidity) + " %</span>" + "<br> cas: " + millis() + String(html_paticka));
      Serial << "HTTP GET: Klient si stahl HTML stranku" << endl << endl;
    }
  });

  // Nastavili jsme webovy server a ted ho uz muzeme spustit
  server.begin();
  Serial << "Webovy sever je spusteny a ceka!" << endl;
  sendMeasuredData(); //for the firts time
}

void blueLedBlink(int interval, int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(LED_BLUE, LOW); // Turn the LED on (Note that LOW is the voltage level)
    delay(interval); // Wait for a second
    digitalWrite(LED_BLUE, HIGH); // Turn the LED off by making the voltage HIGH
    delay(interval); // Wait for two seconds
  }
}

void blueLedTurnOn() {
    digitalWrite(LED_BLUE, LOW); // Turn the LED on (Note that LOW is the voltage level) 
    Serial << "Blue LED turned on." << endl; 
}

void blueLedTurnOff() {
    digitalWrite(LED_BLUE, HIGH); // Turn the LED on (Note that LOW is the voltage level)  
    Serial << "Blue LED turned off." << endl; 
}

// Funkce loop se po spusteni opakuje stale dokola
void loop() {

  if (!flagWifi) {
    Serial << "Sleep for 60 s" << endl;
    delay(60000);
    Serial << "Waking up a trying to reconnect wifi" << endl;
    connectToWifi();
    Serial << "Connected" << endl;
    sendMeasuredData();
  }
  
  // Zpracuj pozadavky weboveho serveru
  server.handleClient();

  if ((millis() - ledBlinkInterval) > 20000) {
    Serial << "Alive!" << endl;
    ledBlinkInterval = millis();
    blueLedBlink(500,1);
  }
  
  /*
    Kazdych x sekund precti hodnoty ze senzoru.
  */
  if ((millis() - posledniObnova) > 300000) {
    ziskejHodnoty();
    posledniObnova = millis();
    sendMeasuredData();
    blueLedBlink(500,2);
  }
}

void sendMeasuredData() {
  Serial << "Sending data to Google Sheets" << endl;
  String temperatureStr = "temperature=";
  String humidityStr = "&humidity=";  
  
  String finalUrl;
  finalUrl = url + temperatureStr + temperature + humidityStr + humidity;
  
  StaticJsonBuffer<250> JSONBuffer;
  JsonObject& JSONencoder = JSONBuffer.createObject();
  JSONencoder["Temperature"] = temperature;
  JSONencoder["Humidity"] = humidity;
  
  char JSONmessageBuffer[250];
  JSONencoder.prettyPrintTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
  Serial << JSONmessageBuffer << endl;

  if (client != nullptr) {
    if (!client->connected()) {   
      Serial << "Reconnecting client." << endl;   
      int retval = client->connect(host, httpsPort);
        if (retval != 1) {
          flag = false;
          flagWifi = false;
          Serial.println("Connection could not be established.");
          blueLedTurnOn();
          return;
        }
      }
      payload = "";
      Serial.println("Send Data to Sheet");
      Serial.println(finalUrl);
      client->GET(finalUrl, host, payload);
      Serial << payload << endl; 
    
  }
  else {
    Serial.println(" >> Failed to POST data");
  }
}

void connectToWifi() {
  // Jmeno zarizeni v siti
  WiFi.hostname("meteostanice");
  // Rezim Wi-Fi (sta = station)
  WiFi.mode(WIFI_STA);
  // Zahajeni pripojovani
  WiFi.begin(ssid, heslo);
  Serial << endl << "Pripojuji se k Wi-Fi siti " << ssid << " ";
  // Dokud se nepripojim, vypisuj do seriove linky tecky
  while (WiFi.status() != WL_CONNECTED) {
    Serial << ".";
    delay(500);
  }

  // Vypis do seriove linky pridelenou IP adresu
  Serial << endl << "Meteostanice ma IP adresu " << WiFi.localIP() << endl;
  flagWifi = true;
}


// Funkce pro precteni hodnot ze senzoru do promennych
void ziskejHodnoty() {
  // Teplota z teplomeru
  temperature = teplomer.readTemperature();
  // humidity vzduchu
  humidity = teplomer.readHumidity();

  // Vypsani cerstvych hodnot do seriove linky
  Serial << "System bezi: " << millis() << " ms" << endl;
  Serial << "Volna pamet heap: " << ESP.getFreeHeap() << " B" << endl << endl;

  Serial << "Udaje z teplomeru a vlhkomeru SHT30" << endl;
  Serial << "Teplota vzduchu: " << temperature << " C" << endl;
  Serial << "Relativni humidity vzduchu: " << humidity << " %" << endl << endl << endl;
}
