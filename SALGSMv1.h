#ifndef SALGSMv1_H
#define SALGSMv1_H

#include <Arduino.h>

/*
  URUCHOMIENIE MODULU sygnalizowane nast komendami
  +CPIN: READY
  Call Ready
  SMS Ready
*/

class SALGSMv1 {
public:
  SALGSMv1(Stream & serial, const char* APN, bool debug);
  void sendAT(const char* cmd, char* out, size_t outSize, unsigned long timeout = 2000);
  void clear(char* buf, size_t size);
  bool init(void);

  void IMSI(void);
  bool con_to_internet(void);

  bool http_get_(const char* cmd);

  const char* extractID(const char* resp);
  //const char* IMSI(void);
  bool reset_(void);
  bool isModuleAlive(void);
  void setDEBUG(bool);

  //void extractHttpData(String raw);

  char my_IMSI [20] = {0};
  char my_APN [100] = {0};
  char IP [20] = {0};

private:
  bool DEBUG = true;
  bool STATUS = false;

  Stream* my_serial;

};

#endif