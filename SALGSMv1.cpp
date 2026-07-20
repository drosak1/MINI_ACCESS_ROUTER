#include "SALGSMv1.h"

SALGSMv1::SALGSMv1(Stream & serial, const char* APN, bool debug){
  this->my_serial = &serial;
  strncpy(this->my_APN, APN, sizeof(this->my_APN)-1);
  this->my_APN[sizeof(this->my_APN)-1] = '\0';
}

bool SALGSMv1::init(void){

  //char response[100] = {0};
  //this->sendAT("AT+CSQ", response, sizeof(response), 1000);
  //Serial.println(response);
  //this->clear(response, sizeof(response));

  uint8_t pom=0;
  do{
    delay(1500);
    this->IMSI();
    Serial.print("IMSI: "); Serial.println(my_IMSI);
    pom++;
    if(pom>10) { Serial.println("RESET"); this->reset_(); }
  }while(strlen(this->my_IMSI)<10 and (this->my_IMSI[0]=='5'));

  if(this->con_to_internet()==false) { Serial.println("RESET"); this->reset_(); }
  
  return true;
}

void SALGSMv1::IMSI(void){
  char response[200] = {0};

  this->sendAT("AT+CIMI", response, sizeof(response), 2000);
  this->extractID(response);

  strncpy(this->my_IMSI, this->extractID(response), sizeof(this->my_IMSI)-1);
  response[sizeof(my_IMSI)-1] = '\0';

  // //const char * res = this->IMSI();
  // Serial.print("my_IMSI - ");
  // Serial.print(my_IMSI);
  // Serial.print(" -> size -> ");
  // Serial.print(sizeof(my_IMSI));
  // Serial.println(" /");
}

bool SALGSMv1::con_to_internet(void){
  char response[200] = {0};
  uint8_t count = 0;

  this->STATUS = true;

  count = 0;
  do{
    this->clear(response, sizeof(response));
    this->sendAT("AT+CGATT=1", response, sizeof(response), 3000);
    Serial.print("[con()] AT+CGATT=1 -> "); Serial.println(response);
    count++;
  } while((strstr(response, "OK") == NULL) and (count<2));
  if (count>=2){
    this->STATUS = false;
  }

  count = 0;
  do{
    this->clear(response, sizeof(response));
    this->sendAT("AT+CSTT=\"internet\"", response, sizeof(response), 1500);
    Serial.print("[con()] AT+CSTT=\"internet\" -> "); Serial.println(response);
    count++;
  } while((strstr(response, "OK") == NULL) and (count<3));
  if (count>=3){
    this->STATUS = false;
  }

  count = 0;
  do{
    this->clear(response, sizeof(response));
    this->sendAT("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"", response, sizeof(response), 1000);
    Serial.print("[con()] AT+CSTT=\"internet\" -> "); Serial.println(response);
    count++;
  } while((strstr(response, "OK") == NULL) and (count<3));
  if (count>=3){
    this->STATUS = false;
  }

  count = 0;
  do{
    this->clear(response, sizeof(response));
    char c_payload[100];
    snprintf(c_payload, sizeof(c_payload),"AT+SAPBR=3,1,\"APN\",\"%s\"", this->my_APN);
    this->sendAT(c_payload, response, sizeof(response), 1000);
    Serial.print("[con()] AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\" -> "); Serial.println(response);
    count++;
  } while((strstr(response, "OK") == NULL) and (count<3));
  if (count>=3){
    this->STATUS = false;
  }

  count = 0;
  do{
    this->clear(response, sizeof(response));
    this->sendAT("AT+SAPBR=1,1", response, sizeof(response), 2000);
    Serial.print("[con()] AT+SAPBR=1,1 -> "); Serial.println(response);
    count++;
  } while((strstr(response, "OK") == NULL) and (count<3));
  if (count>=3){
    this->STATUS = false;
  }
 
  count = 0;
  do{
    this->clear(response, sizeof(response));
    this->sendAT("AT+SAPBR=2,1", response, sizeof(response), 2000);
    Serial.print("[con()] AT+SAPBR=1,1 -> "); Serial.println(response);
    count++;
  } while((strstr(response, "OK") == NULL) and (count<3));
  if (count>=3){
    this->STATUS = false;
  }

  //response posiada na ten moment numer IP
 char* ptr = strstr(response, "+SAPBR:");
  if (ptr) {
    if (sscanf(ptr, "+SAPBR: %*d,%*d,\"%19[^\"]\"", this->IP) == 1) {
      Serial.print("IP: ");
      Serial.println(this->IP);
      this->STATUS = true;
    } else {
      Serial.println("Blad IP");
    }
  } else {
    Serial.println("Brak +SAPBR");
  }
  
  return this->STATUS;
}


bool SALGSMv1::http_get_(const char* cmd){
  char response[60] = {0};
  bool status = false;
  //url += "&lacDec="+String(this->lacDec)+"&cellDec="+String(this->cellDec)+"&netop="+this->network_operator;

  this->STATUS == true;

  this->clear(response, sizeof(response));
  this->sendAT("AT+HTTPINIT", response, sizeof(response), 2000);
  if(strstr(response, "OK") == NULL) this->STATUS == false;
  delay(2000);

  this->clear(response, sizeof(response));
  this->sendAT("AT+HTTPPARA=\"CID\",1", response, sizeof(response), 2000);
  if(strstr(response, "OK") == NULL) this->STATUS == false;
  delay(2000);

  this->clear(response, sizeof(response));

  this->sendAT(cmd, response, sizeof(response), 2000);
  if(strstr(response, "OK") == NULL) this->STATUS == false;
  delay(2000);

  this->clear(response, sizeof(response));
  this->sendAT("AT+HTTPACTION=0", response, sizeof(response), 2000);
  if(strstr(response, "OK") == NULL) this->STATUS == false;
  delay(4000);
  
  this->clear(response, sizeof(response));
  this->sendAT("AT+HTTPREAD=0,100", response, sizeof(response), 5000);
  if(strstr(response, "OK") == NULL) this->STATUS == false;  
  delay(2000);
  if(strstr(response, "SETOK") != NULL) status = true; //<-tutaj odpowiedz z serwera
  if(status==true){
    if(czas_unix==false){
    
      if(extractTS(response)){
        czas_unix=true;
        Serial.println("[TS] czas_unix = true");
      }
      else{
        czas_unix=false;
        Serial.println("[TS] czas_unix = false");
      }
    }
  }else{
    Serial.println("[TS] error status == false !");
  }

  this->clear(response, sizeof(response));
  this->sendAT("AT+HTTPTERM", response, sizeof(response), 2000);
  if(strstr(response, "OK") == NULL) this->STATUS == false;

  if(this->STATUS == false) {
    //zapamietaj licznik w EEPROM
    //odp. zapemietany przed wysłaniem http
    this->reset_();
  }

  return status;
}

void SALGSMv1::clear(char* buf, size_t size) {
  memset(buf, 0, size);
}

void SALGSMv1::sendAT(const char* cmd, char* out, size_t outSize, unsigned long timeout) {
  size_t idx = 0;
  //wdt_reset();
  this->my_serial->println(cmd);
  if (DEBUG) Serial.write("[DEBUG] ");
  unsigned long start = millis();
  while (millis() - start < timeout) {
    while (this->my_serial->available()) {
      char c = this->my_serial->read();
      if (DEBUG) Serial.write(c);  // wypisuj bezpośrednio

      if (idx < outSize - 1) {
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') out[idx++] = c;
      }
    }
    //wdt_reset();
  }
  if (DEBUG) Serial.write("[DEBUG END] ");
  out[idx] = '\0'; // ✅ zakończenie stringa

  // if (strstr(input, "+HTTPREAD") != NULL) {
  // extractHttpData(out);
  // }
}

const char* SALGSMv1::extractID(const char* resp)
{
    static char imsi[32];   // miejsce na wynik
    int j = 0;

    // przejdź przez cały tekst i wyciągnij tylko cyfry
    for (int i = 0; resp[i] != '\0'; i++) {
        if (resp[i] >= '0' && resp[i] <= '9') {
            if (j < sizeof(imsi) - 1) {
                imsi[j++] = resp[i];
            }
        }
    }
    imsi[j] = '\0'; // zakończ string
    return imsi;
}

bool SALGSMv1::isModuleAlive(void) {
  char response[200] = {0};
  this->sendAT("AT", response, sizeof(response), 10000);
  return (strstr(response, "OK") != NULL);
}

bool SALGSMv1::reset_(void){
  if (DEBUG) Serial.println("RESET");
  //wdt_enable(WDTO_1S);
  //delay(15000);
}

void SALGSMv1::setDEBUG(bool state){
  this->DEBUG = state;
}

bool SALGSMv1::extractTS(const char* resp)
{
  if (resp == NULL) {
    return false;
  }

  const char* key = strstr(resp, "\"TS\"");

  if (key == NULL) {
    key = strstr(resp, "TS");
  }

  if (key == NULL) {
    return false;
  }

  const char* colon = strchr(key, ':');

  if (colon == NULL) {
    return false;
  }

  colon++;

  while (*colon == ' ' || *colon == '\t') {
    colon++;
  }

  if (*colon < '0' || *colon > '9') {
    return false;
  }

  char* endPtr = NULL;
  unsigned long value = strtoull(colon, &endPtr, 10);

  if (endPtr == colon) {
    return false;
  }

  this->serverEpoch = value;
  if (DEBUG) Serial.print("TS serverEpoch -> ");
  if (DEBUG) Serial.println(this->serverEpoch);
  //ts = static_cast<unsigned long>(value);
  return true;
}

// void SALGSMv1::extractHttpData(String raw) {
//   int start = raw.indexOf("+HTTPREAD:");
//   if (start == -1) return; // brak +HTTPREAD

//   // wyciągamy liczbę znaków po ":"
//   int colon = raw.indexOf(":", start);
//   int newline = raw.indexOf("\n", colon);
//   if (colon == -1 || newline == -1) return "";

//   String lenStr = raw.substring(colon + 1, newline);
//   lenStr.trim();
//   int lengthToRead = lenStr.toInt(); // np. 41

//   // wyciągamy dokładnie tyle znaków po znaku nowej linii
//   int dataStart = newline + 1;
//   if (dataStart + lengthToRead > raw.length()) {
//     lengthToRead = raw.length() - dataStart; // jeśli mniej danych niż zadeklarowane
//   }

//   String data = raw.substring(dataStart, dataStart + lengthToRead);

//   // opcjonalnie usuń BOM
//   if (data.startsWith("\xEF\xBB\xBF")) {
//     data = data.substring(3);
//   }

//   Serial.println(data);

// }
