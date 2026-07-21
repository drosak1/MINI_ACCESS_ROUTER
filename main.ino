#include <WiFi.h>
#include "esp_wifi.h"
#include "SALGSMv1.h"

#include <WebServer.h>
#include <DNSServer.h>
#include <HardwareSerial.h>

#include "esp_task_wdt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

//http://192.168.50.1/status
//http://192.168.50.1/set.php?ID=1234&value=34&description=cisnienie&unit=stC&timezone=Europe%2FLondon
//nowe oznaczenia -> V-value, D-description, U-unit
//http://192.168.50.1/set.php?ID=1234&V=34&D=cisnienie&U=stC&timezone=Europe%2FLondon

//odpytanie czasu
//http://dlb.com.pl/api/v2/set.php?ID=TIME

/* =========================================================
   KONFIGURACJA ACCESS POINT
   ========================================================= */

const char *AP_SSID = "PROD";
const char *AP_PASSWORD = "PROD-2026";

/*
 * Serwer, który zostanie wpisany do linku wysyłanego przez UART.
 */
//const char *TARGET_SERVER = "http://dlb.com.pl";
const char *TARGET_SET_URL ="http://dlb.com.pl/api/tlm/v2/set.php";
const char *KEY = "999"; //klucz dostepu


/* =========================================================
   WATCHDOG
   ========================================================= */

/*
 * Maksymalny czas bez odświeżenia watchdoga.
 *
 * Task UART działa co 30 sekund, a inicjalizacja modemu może trwać
 * ponad minutę, dlatego ustawiamy 120 sekund.
 */
#define WATCHDOG_TIMEOUT_MS 120000


/*
 * Maksymalna liczba klientow podlaczonych do PROD.
 */
const uint8_t AP_MAX_CLIENTS = 15;

/*
 * Adres ESP32-C6 w sieci PROD.
 */
IPAddress AP_IP(192, 168, 50, 1);
IPAddress AP_GATEWAY(192, 168, 50, 1);
IPAddress AP_SUBNET(255, 255, 255, 0);

/* =========================================================
   KONFIGURACJA UART
   ========================================================= */

/*
 * Dopasuj piny do swojej plytki ESP32-C6.
 */
const int UART_RX_PIN = 4;
const int UART_TX_PIN = 5;

const uint32_t UART_BAUD = 9600;

const int GSM_RST = 22;

/*
 * Numer kontrolera UART.
 */
HardwareSerial TelemetryUART(1);

SALGSMv1 GSM_dev(TelemetryUART, "sensor.net", true);

/* =========================================================
   KONFIGURACJA KOLEJKI
   ========================================================= */

/*
 * Maksymalna liczba oczekujacych zapytan.
 */
#define TELEMETRY_QUEUE_SIZE 50

/*
 * Maksymalna dlugosc jednego zapytania HTTP.
 */
#define TELEMETRY_MESSAGE_SIZE 512

/*
 * Odstep pomiedzy kolejnymi transmisjami UART [ms].
 */
#define UART_SEND_INTERVAL_MS 30000

struct TelemetryMessage
{
    char request[TELEMETRY_MESSAGE_SIZE];
};

QueueHandle_t telemetryQueue = NULL;

/* =========================================================
   SERWER HTTP I DNS
   ========================================================= */

WebServer server(80);
DNSServer dnsServer;

/* =========================================================
   KODOWANIE URL
   ========================================================= */

/*
 * Koduje znaki specjalne do formatu URL.
 *
 * Przyklad:
 * Europe/London -> Europe%2FLondon
 */
String urlEncode(const String &value)
{
    const char hex[] = "0123456789ABCDEF";

    String encoded;
    encoded.reserve(value.length() * 3);

    for (size_t i = 0; i < value.length(); i++)
    {
        unsigned char c =
            static_cast<unsigned char>(value[i]);

        if (
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' ||
            c == '_' ||
            c == '.' ||
            c == '~')
        {
            encoded += static_cast<char>(c);
        }
        else
        {
            encoded += '%';
            encoded += hex[(c >> 4) & 0x0F];
            encoded += hex[c & 0x0F];
        }
    }

    return encoded;
}

bool routerTimeValid = false;
unsigned long routerEpochAtSync = 0;
unsigned long routerMillisAtSync = 0;

void setRouterTime(unsigned long epoch)
{
    routerEpochAtSync = epoch;
    routerMillisAtSync = millis();
    routerTimeValid = true;
}

unsigned long getRouterUnixTime()
{
    if (!routerTimeValid)
    {
        return 0;
    }

    return routerEpochAtSync + ((millis() - routerMillisAtSync) / 1000);
}

/* =========================================================
   BUDOWANIE PELNEGO LINKU
   ========================================================= */

String buildQueuedRequest()
{
    /*
     * Host, ktory zostal wpisany przez urzadzenie.
     *
     * Przyklad:
     * moj-serwer.pl
     */
    String request = TARGET_SET_URL;

    if (server.args() > 0)
    {
        request += '?';
    }

    for (int i = 0; i < server.args(); i++)
    {
        if (i > 0)
        {
            request += '&';
        }

        request += urlEncode(server.argName(i));
        request += '=';
        request += urlEncode(server.arg(i));
    }

    request += "&TS=";
    request += String(getRouterUnixTime());

    return request;
}

/* =========================================================
   ODPOWIEDZI JSON
   ========================================================= */

void sendJsonError(
    int statusCode,
    const String &message)
{
    String json;

    json.reserve(200);

    json += "{";
    json += "\"status\":\"error\",";
    json += "\"message\":\"";
    json += message;
    json += "\"";
    json += "}";

    server.send(
        statusCode,
        "application/json; charset=utf-8",
        json
    );
}

/* =========================================================
   WALIDACJA ZAPYTANIA
   ========================================================= */

bool validateTelemetryRequest()
{
    const char *requiredParameters[] =
    {
        "ID",
        "RSSI"
    };

    const size_t parameterCount =
        sizeof(requiredParameters) /
        sizeof(requiredParameters[0]);

    for (size_t i = 0; i < parameterCount; i++)
    {
        const char *parameter = requiredParameters[i];

        if (!server.hasArg(parameter))
        {
            sendJsonError(
                400,
                String("Brak parametru: ") + parameter
            );

            Serial.print("Brak parametru: ");
            Serial.println(parameter);

            return false;
        }

        if (server.arg(parameter).length() == 0)
        {
            sendJsonError(
                400,
                String("Pusty parametr: ") + parameter
            );
            Serial.print("Pusty parametr: ");
            Serial.println(parameter);

            return false;
        }
    }

    /*
     * Parametr timezone jest opcjonalny.
     */

    return true;
}

/* =========================================================
   DODANIE ZAPYTANIA DO KOLEJKI
   ========================================================= */

void handleSetRequest()
{
    if (!validateTelemetryRequest())
    {
        Serial.println("error -> validateTelemetryRequest()");
        //return;
    }

    if (telemetryQueue == NULL)
    {
        sendJsonError(
            500,
            "Kolejka nie zostala utworzona"
        );

        return;
    }

    String request = buildQueuedRequest();

    /*
     * Sprawdzenie, czy link zmiesci sie w strukturze.
     */
    if (request.length() >= TELEMETRY_MESSAGE_SIZE)
    {
        sendJsonError(
            413,
            "Zapytanie HTTP jest zbyt dlugie"
        );

        return;
    }

    TelemetryMessage message;

    memset(
        &message,
        0,
        sizeof(message)
    );

    request.toCharArray(
        message.request,
        sizeof(message.request)
    );

    /*
     * Dodanie wpisu na koniec kolejki.
     *
     * Czas oczekiwania 0:
     * obsluga HTTP nie jest blokowana, gdy kolejka jest pelna.
     */
    BaseType_t queueResult = xQueueSend(
        telemetryQueue,
        &message,
        0
    );

    if (queueResult != pdPASS)
    {
        sendJsonError(
            503,
            "Kolejka jest pelna"
        );

        return;
    }

    UBaseType_t queueCount =
        uxQueueMessagesWaiting(telemetryQueue);

    UBaseType_t queueFree =
        uxQueueSpacesAvailable(telemetryQueue);

    String response;

    response.reserve(800);

    response += "{";
    response += "\"status\":\"queued\",";
    response += "\"message\":\"Dane dodane do kolejki\",";
    response += "\"queue_count\":";
    response += String(queueCount);
    response += ",";
    response += "\"queue_free\":";
    response += String(queueFree);
    response += ",";
    response += "\"request\":\"";
    response += request;
    response += "\"";
    response += "}";

    /*
     * HTTP 202 = zapytanie przyjete do dalszej obslugi.
     */
    server.send(
        202,
        "application/json; charset=utf-8",
        response
    );

    Serial.println();
    Serial.println("Dodano do kolejki:");
    Serial.println(request);

    Serial.print("Liczba wpisow: ");
    Serial.println(queueCount);
}

/* =========================================================
   STATUS URZADZENIA
   ========================================================= */

void handleStatus()
{
    UBaseType_t queueCount = 0;
    UBaseType_t queueFree = 0;

    if (telemetryQueue != NULL)
    {
        queueCount =
            uxQueueMessagesWaiting(telemetryQueue);

        queueFree =
            uxQueueSpacesAvailable(telemetryQueue);
    }

    String json;

    json.reserve(400);

    json += "{";

    json += "\"gateway\":\"PROD\",";

    json += "\"status\":\"ok\",";

    json += "\"ap_ip\":\"";
    json += WiFi.softAPIP().toString();
    json += "\",";

    json += "\"connected_clients\":";
    json += String(WiFi.softAPgetStationNum());
    json += ",";

    json += "\"queue_count\":";
    json += String(queueCount);
    json += ",";

    json += "\"queue_free\":";
    json += String(queueFree);
    json += ",";

    json += "\"queue_capacity\":";
    json += String(TELEMETRY_QUEUE_SIZE);
    json += ",";

    json += "\"uart_baud\":";
    json += String(UART_BAUD);
    json += ",";

    json += "\"send_interval_ms\":";
    json += String(UART_SEND_INTERVAL_MS);

    json += "}";

    server.send(
        200,
        "application/json; charset=utf-8",
        json
    );
}

/* =========================================================
   NIEZNANY ADRES
   ========================================================= */

void handleNotFound()
{
    /*
     * Przechwytujemy kazda sciezke konczaca sie na set.php.
     *
     * Przyklady:
     * /set.php
     * /api/set.php
     * /telemetry/set.php
     */
    if (server.uri().endsWith("/set.php"))
    {
        handleSetRequest();
        return;
    }

    sendJsonError(
        404,
        String("Nieznany adres: ") + server.uri()
    );
}



/* =========================================================
   TASK WYSYLAJACY UART
   ========================================================= */
void uartSenderTask(void *parameter)
{
    TelemetryMessage message;
    TelemetryMessage removedMessage;

    /*
     * Dodanie tego tasku do watchdoga.
     * NULL oznacza aktualnie wykonywany task UART_sender.
     */
    esp_err_t watchdogResult =
        esp_task_wdt_add(NULL);

    if (watchdogResult == ESP_OK)
    {
        Serial.println(
            "UART_sender dodany do watchdoga"
        );
    }
    else
    {
        Serial.print(
            "Blad dodawania UART_sender do watchdoga: "
        );

        Serial.println(
            esp_err_to_name(watchdogResult)
        );
    }

    const TickType_t sendInterval =
        pdMS_TO_TICKS(UART_SEND_INTERVAL_MS);

    TickType_t lastWakeTime =
        xTaskGetTickCount();

    while (true)
    {
        /*
         * Potwierdzenie, że task nadal działa.
         */
        esp_task_wdt_reset();

        /*
         * Oczekiwanie do kolejnego cyklu.
         */
        vTaskDelayUntil(
            &lastWakeTime,
            sendInterval
        );

        /*
         * Task obudził się prawidłowo.
         */
        esp_task_wdt_reset();

        /*
         * Podgląd pierwszego wpisu bez usuwania.
         */
        BaseType_t peekResult = xQueuePeek(
            telemetryQueue,
            &message,
            0
        );

        if (peekResult != pdTRUE)
        {
            /*
             * Kolejka jest pusta.
             * Watchdog został już odświeżony.
             */
            continue;
        }

        size_t messageLength =
            strlen(message.request);

        Serial.println();
        Serial.println("UART -> NBiOT TX:");
        Serial.println(message.request);

        char url[200];
        snprintf(url, sizeof(url), "AT+HTTPPARA=\"URL\",\"%s&IMSI=%s&KEY=%s&IP=%s\"",message.request,GSM_dev.my_IMSI, KEY, GSM_dev.IP);
        Serial.println("URL -> NBiOT :");
        Serial.println(url);

        if(GSM_dev.http_get_(url))  {
            Serial.println("htt_get_() OK                     ;-) !");
        }
        // else
        // {
        //     if(GSM_dev.http_get_(url))  {
        //         Serial.println("htt_get_()!");
        //     }
        //     else
        //     {
        //         GSM_dev.reset_(); // restart systemu, podwójna proba wyslania danych na serwer
        //     }
        // }

        /*
         * Odświeżenie przed rozpoczęciem transmisji.
         */
        esp_task_wdt_reset();

        size_t writtenBytes = TelemetryUART.write(
            reinterpret_cast<const uint8_t *>(
                message.request
            ),
            messageLength
        );

        size_t newlineWritten =
            TelemetryUART.write('\n');

        /*
         * Oczekiwanie na opróżnienie bufora UART.
         */
        TelemetryUART.flush();

        /*
         * Transmisja zakończona.
         */
        esp_task_wdt_reset();

        if (
            writtenBytes == messageLength &&
            newlineWritten == 1)
        {
            /*
             * Usuwamy pierwszy wpis dopiero po zakończeniu
             * transmisji UART.
             */
            BaseType_t receiveResult = xQueueReceive(
                telemetryQueue,
                &removedMessage,
                0
            );

            if (receiveResult == pdTRUE)
            {
                Serial.println(
                    "Wpis wyslany i usuniety z kolejki"
                );

                Serial.print("Pozostalo wpisow: ");

                Serial.println(
                    uxQueueMessagesWaiting(
                        telemetryQueue
                    )
                );
            }
            else
            {
                Serial.println(
                    "Blad usuwania wpisu z kolejki"
                );
            }
        }
        else
        {
            /*
             * Wiadomość pozostaje w kolejce.
             */
            Serial.println(
                "Nie wyslano calej wiadomosci"
            );

            Serial.println(
                "Wpis pozostaje w kolejce"
            );
        }

        esp_task_wdt_reset();
    }
}

// void uartSenderTask(void *parameter)
// {
//     TelemetryMessage message;
//     TelemetryMessage removedMessage;

//     const TickType_t sendInterval =
//         pdMS_TO_TICKS(UART_SEND_INTERVAL_MS);

//     TickType_t lastWakeTime =
//         xTaskGetTickCount();

//     while (true)
//     {
//         /*
//          * Task wykonuje sie co 5 sekund.
//          */
//         vTaskDelayUntil(
//             &lastWakeTime,
//             sendInterval
//         );

//         /*
//          * xQueuePeek odczytuje pierwszy wpis,
//          * ale jeszcze go nie usuwa.
//          */
//         BaseType_t peekResult = xQueuePeek(
//             telemetryQueue,
//             &message,
//             0
//         );

//         if (peekResult != pdTRUE)
//         {
//             /*
//              * Kolejka jest pusta.
//              */
//             continue;
//         }

//         size_t messageLength =
//             strlen(message.request);

//         Serial.println();
//         Serial.println("UART -> NBiOT TX:");
//         Serial.println(message.request);

//         char url[200];
//         snprintf(url, sizeof(url), "AT+HTTPPARA=\"URL\",\"%s&IMSI=%s&KEY=%s&IP=%s\"",message.request,GSM_dev.my_IMSI, KEY, GSM_dev.IP);
//         Serial.println("URL -> NBiOT :");
//         Serial.println(url);

//         if(GSM_dev.http_get_(url))  {
//             Serial.println("htt_get_()!");
//         }
//         else
//         {
//             if(GSM_dev.http_get_(url))  {
//                 Serial.println("htt_get_()!");
//             }
//             else
//             {
//                 GSM_dev.reset_(); // restart systemu, podwójna proba wyslania danych na serwer
//             }
//         }

//         /*
//          * Wyslanie linku przez UART.
//          */
//         size_t writtenBytes = TelemetryUART.write(
//             reinterpret_cast<const uint8_t *>(
//                 message.request
//             ),
//             messageLength
//         );

//         /*
//          * Kazdy link zakonczony jest znakiem nowej linii.
//          */
//         size_t newlineWritten =
//             TelemetryUART.write('\n');

//         /*
//          * Czekamy, az bufor nadajnika UART zostanie oprozniony.
//          */
//         TelemetryUART.flush();

//         /*
//          * Usuwamy wpis tylko wtedy, gdy caly komunikat
//          * zostal przekazany do UART.
//          */
//         if (
//             writtenBytes == messageLength &&
//             newlineWritten == 1)
//         {
//             BaseType_t receiveResult = xQueueReceive(
//                 telemetryQueue,
//                 &removedMessage,
//                 0
//             );

//             if (receiveResult == pdTRUE)
//             {
//                 Serial.println(
//                     "Wpis wyslany i usuniety z kolejki"
//                 );

//                 Serial.print("Pozostalo wpisow: ");
//                 Serial.println(
//                     uxQueueMessagesWaiting(
//                         telemetryQueue
//                     )
//                 );
//             }
//             else
//             {
//                 Serial.println(
//                     "Blad usuwania wpisu z kolejki"
//                 );
//             }
//         }
//         else
//         {
//             /*
//              * Wpis pozostaje na poczatku kolejki.
//              * Kolejna proba nastapi po 5 sekundach.
//              */
//             Serial.println(
//                 "Nie wyslano calej wiadomosci"
//             );

//             Serial.println(
//                 "Wpis pozostaje w kolejce"
//             );
//         }
//     }
// }

void initializeWatchdog()
{
    esp_task_wdt_config_t watchdogConfig = {};

    watchdogConfig.timeout_ms = WATCHDOG_TIMEOUT_MS;

    /*
     * Nie dodajemy automatycznie tasku IDLE.
     * Będziemy kontrolować wyłącznie:
     * - Arduino loopTask,
     * - UART_sender.
     */
    watchdogConfig.idle_core_mask = 0;

    /*
     * Po przekroczeniu czasu uruchom panic handler,
     * co w normalnej konfiguracji produkcyjnej powoduje reset.
     */
    watchdogConfig.trigger_panic = true;

    /*
     * Arduino może mieć już uruchomiony Task Watchdog.
     */
    esp_err_t result =
        esp_task_wdt_init(&watchdogConfig);

    if (result == ESP_ERR_INVALID_STATE)
    {
        /*
         * Watchdog był już uruchomiony.
         * Zmieniamy jego konfigurację.
         */
        result =
            esp_task_wdt_reconfigure(&watchdogConfig);
    }

    if (result == ESP_OK)
    {
        Serial.println("Watchdog skonfigurowany");
    }
    else
    {
        Serial.print("Blad konfiguracji watchdoga: ");
        Serial.println(esp_err_to_name(result));
        return;
    }

    /*
     * setup() i loop() działają w tym samym tasku Arduino.
     * NULL oznacza aktualnie wykonywany task.
     */
    if (esp_task_wdt_status(NULL) != ESP_OK)
    {
        result = esp_task_wdt_add(NULL);

        if (result == ESP_OK)
        {
            Serial.println("loopTask dodany do watchdoga");
        }
        else
        {
            Serial.print("Nie mozna dodac loopTask: ");
            Serial.println(esp_err_to_name(result));
        }
    }
    else
    {
        Serial.println("loopTask byl juz kontrolowany");
    }
}


void setup()
{
    pinMode(GSM_RST, OUTPUT);
    digitalWrite(GSM_RST,HIGH);

    Serial.begin(115200);

    initializeWatchdog();

    delay(1000);

    Serial.println();
    Serial.println("==============================");
    Serial.println("ESP32-C6 TELEMETRY GATEWAY");
    Serial.println("==============================");

    /*
     * Uruchomienie dodatkowego UART.
     */
    TelemetryUART.begin(
        UART_BAUD,
        SERIAL_8N1,
        UART_RX_PIN,
        UART_TX_PIN
    );

    Serial.print("UART RX: GPIO");
    Serial.println(UART_RX_PIN);

    Serial.print("UART TX: GPIO");
    Serial.println(UART_TX_PIN);

    Serial.print("UART baud: ");
    Serial.println(UART_BAUD);

    /*
     * Utworzenie kolejki.
     */
    telemetryQueue = xQueueCreate(
        TELEMETRY_QUEUE_SIZE,
        sizeof(TelemetryMessage)
    );

    if (telemetryQueue == NULL)
    {
        Serial.println(
            "BLAD: nie mozna utworzyc kolejki"
        );

        while (true)
        {
            delay(1000);
        }
    }

    Serial.print("Pojemnosc kolejki: ");
    Serial.println(TELEMETRY_QUEUE_SIZE);

    /*
     * ESP32-C6 pracuje jako Access Point.
     */
    WiFi.mode(WIFI_AP);

    /*
     * Konfiguracja stalego adresu IP.
     */
    if (!WiFi.softAPConfig(
            AP_IP,
            AP_GATEWAY,
            AP_SUBNET))
    {
        Serial.println(
            "Blad konfiguracji IP Access Point"
        );
    }

    /*
     * Uruchomienie sieci PROD.
     */
    bool accessPointStarted = WiFi.softAP(
        AP_SSID,
        AP_PASSWORD,
        1,                 // kanal Wi-Fi
        0,                 // SSID widoczne
        AP_MAX_CLIENTS
    );

    if (!accessPointStarted)
    {
        Serial.println(
            "BLAD: nie mozna uruchomic sieci PROD"
        );

        while (true)
        {
            delay(1000);
        }
    }

    /*
    * Ustawienie maksymalnej mocy nadajnika Wi-Fi.
    */
    esp_err_t txResult = esp_wifi_set_max_tx_power(80);

    if (txResult == ESP_OK)
    {
        Serial.println("Maksymalna moc Wi-Fi ustawiona na 20 dBm");
    }
    else
    {
        Serial.print("Blad ustawiania mocy: ");
        Serial.println(txResult);
    }

    int8_t currentTxPower = 0;

    if (esp_wifi_get_max_tx_power(&currentTxPower) == ESP_OK)
    {
        Serial.print("Moc TX: ");
        Serial.print(currentTxPower / 4.0);
        Serial.println(" dBm");
    }


    Serial.println();
    Serial.println("Siec PROD uruchomiona");

    Serial.print("SSID: ");
    Serial.println(AP_SSID);

    Serial.print("Adres IP: ");
    Serial.println(WiFi.softAPIP());

    /*
     * Lokalny DNS kieruje nazwy serwerow na ESP32-C6.
     *
     * Dzieki temu zapytanie:
     * http://moj-serwer/set.php
     *
     * moze trafic do 192.168.50.1.
     */
    bool dnsStarted = dnsServer.start(
        53,
        "*",
        AP_IP
    );

    if (dnsStarted)
    {
        Serial.println(
            "Serwer DNS uruchomiony"
        );
    }
    else
    {
        Serial.println(
            "Blad uruchamiania DNS"
        );
    }

    /*
     * Obslugiwane endpointy.
     */
    server.on(
        "/set.php",
        HTTP_GET,
        handleSetRequest
    );

    server.on(
        "/api/set.php",
        HTTP_GET,
        handleSetRequest
    );

    server.on(
        "/status",
        HTTP_GET,
        handleStatus
    );

    server.onNotFound(handleNotFound);

    server.begin();

    Serial.println(
        "Serwer HTTP uruchomiony"
    );

    Serial.println(
        "Status: http://192.168.50.1/status"
    );

    Serial.println("Inicjalizacja modulu NBiOT moze potrwac 15 sekund ...");
        digitalWrite(GSM_RST, LOW);
        delay(100);
        digitalWrite(GSM_RST, HIGH);
        delay(15000);
        GSM_dev.init();
    Serial.println("Zakonczona inicjalizacja modulu NBiOT");    

    //synchronizuj zegar
    //http://dlb.com.pl/api/v2/set.php?ID=TIME
    char url[200];
    snprintf(url, sizeof(url), "AT+HTTPPARA=\"URL\",\"dlb.com.pl/api/tlm/v2/set.php?ID=TIME&IMSI=%s&KEY=%s&IP=%s\"",GSM_dev.my_IMSI, KEY, GSM_dev.IP);
    Serial.println("synchronizuj zegar :");
    Serial.println(url);
    if(GSM_dev.http_get_(url))  {
            Serial.println("htt_get_(TIME) OK                     ;-) !");
            setRouterTime(GSM_dev.serverEpoch);
    }
    //setRouterTime(GSM_dev.serverEpoch);
    //setRouterTime(unsigned long epoch);

    /*
     * Utworzenie osobnego tasku UART.
     */
    BaseType_t taskResult = xTaskCreate(
        uartSenderTask,
        "UART_sender",
        4096,
        NULL,
        1,
        NULL
    );

    if (taskResult != pdPASS)
    {
        Serial.println(
            "BLAD: nie mozna utworzyc tasku UART"
        );

        while (true)
        {
            delay(1000);
        }
    }

    Serial.println(
        "Task UART uruchomiony"
    );

    Serial.println(
        "Gateway gotowy do pracy"
    );

}

/* =========================================================
   LOOP
   ========================================================= */

void loop()
{

    esp_task_wdt_reset();


    /*
     * Obsluga zapytan DNS.
     */
    dnsServer.processNextRequest();

    /*
     * Obsluga zapytan HTTP.
     */
    server.handleClient();

    delay(2);
}
