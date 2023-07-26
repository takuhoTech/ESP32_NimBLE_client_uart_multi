//何故かCore Debug LevelをDebugにするとconnectを同時によんでも全部接続できる
//NimBLEClient.cppのline264を編集済み

#include "StringSplitter.h"
#include <NimBLEDevice.h>
#include <Ticker.h>
#include <HardwareSerial.h>

HardwareSerial SerialPICO(0);

#define DEBUG
#define DEBUG_MEM
//#define DEBUG_TEMP
//#define PICONOTIFY
#define ADV_DURATION 15
//define REQUIRE_TWO_PRPH

#ifdef REQUIRE_TWO_PRPH
#define OPERATOR ||
#else
#define OPERATOR &&
#endif

#ifdef DEBUG_TEMP
extern "C" {
  uint8_t temprature_sens_read();
}
#endif

#ifdef DEBUG
#define DEBUG_begin(x) Serial.begin(x)
#define DEBUG_print(x) Serial.print(x)
#define DEBUG_println(x) Serial.println(x)
#define DEBUG_wait while(!Serial)yield()
//#define DEBUG_wait
#else
#define DEBUG_begin(x)
#define DEBUG_print(x)
#define DEBUG_println(x)
#define DEBUG_wait
#endif

static String PRPHNAME[] = {"AirMeter", "PowerMeter", "Display"};
enum {AIRMETER, POWERMETER, ROUNDDISPLAY};

union PACKET
{
  struct {
    bool AirMeterIsOpen = false;  //1byte
    float AirSpeed;      //4byte
    float AirMeterBat;   //4byte
    bool PowerMeterIsOpen = false; //1byte
    uint16_t Cadence;    //2byte
    uint16_t PowerAvg;   //2byte
    uint16_t PowerMax;   //2byte
    float PowerMeterBat; //4byte
  };
  uint8_t bin[20];
};
static PACKET packet;
static String DisplayPacket;

static BLEUUID serviceUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
// The characteristics of the above service we are interested in.
// The client can send data to the server by writing to this characteristic.
static BLEUUID charUUID_RX("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
// If the client has enabled notifications for this characteristic, the server can send data to the client as notifications.
static BLEUUID charUUID_TX("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");

#define MAX_SERVER 3
typedef struct
{
  BLEAddress *pServerAddress;
  boolean doConnect = false;
  boolean connected = false;
  BLERemoteCharacteristic* pTXCharacteristic;
  BLERemoteCharacteristic* pRXCharacteristic;
  bool NotifyState = true;
  int index = 0;
  String name;
  int rssi;
} server;
static server Server[MAX_SERVER];

const uint8_t notificationOff[] = {0x0, 0x0};
const uint8_t notificationOn[] = {0x1, 0x0};

static void BuildPacket(String PrphName, String str, int index)
{
  //DEBUG_print(str.c_str());
  if (index == AIRMETER)
  {
    //packet.AirMeterIsOpen = true;
    StringSplitter *AirData = new StringSplitter(str, ' ', 2);
    packet.AirSpeed = AirData->getItemAtIndex(0).toFloat();
    packet.AirMeterBat = AirData->getItemAtIndex(1).toFloat();
    delete AirData;
  }
  else if (index == POWERMETER)
  {
    //packet.PowerMeterIsOpen = true;
    StringSplitter *PowerData = new StringSplitter(str, ' ', 4);
    packet.Cadence = PowerData->getItemAtIndex(0).toInt();
    packet.PowerAvg = PowerData->getItemAtIndex(1).toInt();
    packet.PowerMax = PowerData->getItemAtIndex(2).toInt();
    packet.PowerMeterBat = PowerData->getItemAtIndex(3).toFloat();
    delete PowerData;
  }
  /*else if (index == ROUNDDISPLAY)//Readyメッセージが届いたら、送り返す
    {
    String tmp = String(packet.Cadence);
    tmp += " ";
    tmp += String(packet.PowerAvg);
    tmp += " ";
    tmp += String(int(packet.PowerMeterBat * 100.0));
    tmp += " ";
    tmp += String(int(packet.AirSpeed * 100.0));
    tmp += " ";
    tmp += String(int(packet.AirMeterBat * 100.0));
    tmp += ",";
    DisplayPacket = tmp;
    Server[index].pRXCharacteristic->writeValue(DisplayPacket.c_str(), DisplayPacket.length());
    DEBUG_println("Send to Display");
    }*/
  else
  {
    DEBUG_println("Unknown device detected");
  }
}

static void notifyCallback_0(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  String ServerName = Server[0].name;
  DEBUG_print((ServerName + ":").c_str());
  String tmp = "";
  for (int i = 0; i < length; i++) {
    DEBUG_print((char)pData[i]);     // Print character to uart
    //DEBUG_print(pData[i]);           // print raw data to uart
    //DEBUG_print(" ");
    tmp += (char)pData[i];
  }
  BuildPacket(ServerName, tmp, 0);
  DEBUG_println();
}
static void notifyCallback_1(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  String ServerName = Server[1].name;
  DEBUG_print((ServerName + ":").c_str());
  String tmp = "";
  for (int i = 0; i < length; i++) {
    DEBUG_print((char)pData[i]);     // Print character to uart
    //DEBUG_print(pData[i]);           // print raw data to uart
    //DEBUG_print(" ");
    tmp += (char)pData[i];
  }
  BuildPacket(ServerName, tmp, 1);
  DEBUG_println();
}
static void notifyCallback_2(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  String ServerName = Server[2].name;
  DEBUG_print((ServerName + ":").c_str());
  String tmp = "";
  for (int i = 0; i < length; i++) {
    DEBUG_print((char)pData[i]);     // Print character to uart
    //DEBUG_print(pData[i]);           // print raw data to uart
    //DEBUG_print(" ");
    tmp += (char)pData[i];
  }
  BuildPacket(ServerName, tmp, 2);
  DEBUG_println();
}
void (* const notifyCallbackArray[MAX_SERVER])(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) = {
  notifyCallback_0,
  notifyCallback_1,
  notifyCallback_2,
};

class MyClientCallback : public BLEClientCallbacks
{
    void onConnect(BLEClient *pclient)
    {
      //DEBUG_print(Server[pclient->index].name.c_str());
      //DEBUG_println(" Connected Callback");
      DEBUG_print(" - Callback ");
      if (pclient->index == AIRMETER)
      {
        packet.AirMeterIsOpen = true;
        DEBUG_println("AirMeter Connected.");
      }
      else if (pclient->index == POWERMETER)
      {
        packet.PowerMeterIsOpen = true;
        DEBUG_println("PowerMeter Connected.");
      }
      else if (pclient->index == ROUNDDISPLAY)
      {
        DEBUG_println("Display Connected.");
      }
    }
    void onDisconnect(BLEClient *pclient)
    {
      DEBUG_print(" - Callback ");
      if (pclient->index == AIRMETER)
      {
        packet.AirMeterIsOpen = false;
        Server[pclient->index].connected = false;
        Server[pclient->index].doConnect = true;
        DEBUG_println("AirMeter Disconnected. Try reConnect.");
      }
      else if (pclient->index == POWERMETER)
      {
        packet.PowerMeterIsOpen = false;
        Server[pclient->index].connected = false;
        Server[pclient->index].doConnect = true;
        DEBUG_println("PowerMeter Disconnected. Try reConnect.");
      }
      else if (pclient->index == ROUNDDISPLAY)
      {
        Server[pclient->index].connected = false;
        Server[pclient->index].doConnect = true;
        DEBUG_println("Display Disconnected. Try reConnect.");
      }
      //DEBUG_println("onDisconnect");
      BLEDevice::deleteClient(pclient);
    }
};

//bool isConnecting = false;

bool connectToServer(server *peripheral) {
  DEBUG_print("Establishing a connection to device address: ");
  DEBUG_println((*peripheral->pServerAddress).toString().c_str());

  BLEClient*  pClient  = BLEDevice::createClient();
  DEBUG_println(" - Created client");

  pClient->index = peripheral->index;

  pClient->setClientCallbacks(new MyClientCallback());

  // Connect to the remove BLE Server.
  pClient->setConnectTimeout(5); //seconds
  if (!(pClient->connect(*peripheral->pServerAddress)))
  {
    DEBUG_println(" - Connect func returned false");
    BLEDevice::deleteClient(pClient);
    return false;
  }
  DEBUG_println(" - Connected to server");

  // Obtain a reference to the Nordic UART service on the remote BLE server.
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    DEBUG_print("Failed to find Nordic UART service UUID: ");
    DEBUG_println(serviceUUID.toString().c_str());
    BLEDevice::deleteClient(pClient);
    return false;
  }
  DEBUG_println(" - Remote BLE service reference established");

  // Obtain a reference to the TX characteristic of the Nordic UART service on the remote BLE server.
  peripheral->pTXCharacteristic = pRemoteService->getCharacteristic(charUUID_TX);
  if (peripheral->pTXCharacteristic == nullptr) {
    DEBUG_print("Failed to find TX characteristic UUID: ");
    DEBUG_println(charUUID_TX.toString().c_str());
    BLEDevice::deleteClient(pClient);
    return false;
  }
  DEBUG_println(" - Remote BLE TX characteristic reference established");

  /*// Read the value of the TX characteristic.
    std::string value = peripheral->pTXCharacteristic->readValue();
    DEBUG_print("The characteristic value is currently: ");
    DEBUG_println(value.c_str());*/

  peripheral->pTXCharacteristic->registerForNotify(notifyCallbackArray[peripheral->index]);//DEBUG default valid

  //RXchar
  // Obtain a reference to the RX characteristic of the Nordic UART service on the remote BLE server.
  peripheral->pRXCharacteristic = pRemoteService->getCharacteristic(charUUID_RX);
  if (peripheral->pRXCharacteristic == nullptr) {
    DEBUG_print("Failed to find RX characteristic UUID: ");
    DEBUG_println(charUUID_RX.toString().c_str());
    BLEDevice::deleteClient(pClient);
    return false;
  }
  DEBUG_println(" - Remote BLE RX characteristic reference established");

  // Write to the the RX characteristic.
  //String helloValue = "Hello Remote Server";
  //peripheral->pRXCharacteristic->writeValue(helloValue.c_str(), helloValue.length());
  return true;
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    //Called for each advertising BLE server.
    void onResult(BLEAdvertisedDevice* advertisedDevice) {
      //DEBUG_print("BLE Advertised Device found - ");
      //DEBUG_println(advertisedDevice->toString().c_str());

      // We have found a device, check to see if it contains the Nordic UART service.
      if (advertisedDevice->haveServiceUUID() && advertisedDevice->getServiceUUID().equals(serviceUUID))
      {
        DEBUG_println("Nordic UART device found");
        for (int i = 0; i < MAX_SERVER; i++)
        {
          if ((PRPHNAME[i] == advertisedDevice->getName().c_str()) && (Server[i].doConnect == false) && (Server[i].connected == false))
          {
            DEBUG_print("Device "); DEBUG_print(i); DEBUG_println(" found");
            //advertisedDevice->getScan()->stop();
            Server[i].pServerAddress = new BLEAddress(advertisedDevice->getAddress());
            Server[i].doConnect = true;
            Server[i].index = i;
            Server[i].name = advertisedDevice->getName().c_str();
            Server[i].rssi = advertisedDevice->getRSSI();
            return;
          }
        }
      }
    }
}; // MyAdvertisedDeviceCallbacks

void SendDisplay()
{
  if (Server[ROUNDDISPLAY].connected)
  {
    String tmp = String(packet.Cadence);
    tmp += " ";
    tmp += String(packet.PowerAvg);
    tmp += " ";
    tmp += String(int(packet.PowerMeterBat * 100.0));
    tmp += " ";
    tmp += String(int(packet.AirSpeed * 100.0));
    tmp += " ";
    tmp += String(int(packet.AirMeterBat * 100.0));
    tmp += ",";
    DisplayPacket = tmp;
    Server[ROUNDDISPLAY].pRXCharacteristic->writeValue(DisplayPacket.c_str(), DisplayPacket.length());
    DEBUG_println("Send to Display");
  }
}
void SendDisplayTask(void *pvParameters)
{
  while (true)
  {
    SendDisplay();
    delay(1000);
  }
  vTaskDelete(NULL);
}

void SerialPico()
{
  //while (SerialPICO.available() > 1);
#ifdef PICONOTIFY
  DEBUG_print("Check Pico request");
#endif
  if (SerialPICO.read() != -1)
  {
    SerialPICO.write(packet.bin, sizeof(PACKET));
#ifdef PICONOTIFY
    DEBUG_print(" - Send to Pico");
#endif
  }
  while (SerialPICO.available() > 0)
  {
    SerialPICO.read();
  }
#ifdef PICONOTIFY
  DEBUG_println("");
#endif
}
void SerialPicoTask(void *pvParameters)
{
  while (true)
  {
    SerialPico();
    delay(100);
  }
  vTaskDelete(NULL);
}

void ConnectPrph(int index)
{
  if (Server[index].doConnect == true)
  {
    if (connectToServer(&Server[index])) {
      DEBUG_println("Connection success");
      Server[index].connected = true;
      Server[index].doConnect = false; //false only when connect success
      //Server[index].pTXCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)notificationOn, 2, true);
    }
    else
    {
      DEBUG_println("Connect Failed. Try again later.");
    }
  }
}
void ConnectPrphTask_0(void *pvParameters)
{
  while (true)
  {
    //DEBUG_println("Connect Task 0");
    ConnectPrph(0);
    delay(500);
  }
  vTaskDelete(NULL);
}
void ConnectPrphTask_1(void *pvParameters)
{
  while (true)
  {
    //DEBUG_println("Connect Task 1");
    ConnectPrph(1);
    delay(500);
  }
  vTaskDelete(NULL);
}
void ConnectPrphTask_2(void *pvParameters)
{
  while (true)
  {
    //DEBUG_println("Connect Task 2");
    ConnectPrph(2);
    delay(500);
  }
  vTaskDelete(NULL);
}
void (* const ConnectPrphTaskArray[MAX_SERVER])(void *pvParameters) = {
  ConnectPrphTask_0,
  ConnectPrphTask_1,
  ConnectPrphTask_2,
};

void setup() {
  SerialPICO.begin(115200, SERIAL_8N1, -1, -1);
  DEBUG_begin(115200);
  DEBUG_wait;
  DEBUG_println("Starting Nordic UART central");

  xTaskCreateUniversal(
    SerialPicoTask,
    "SerialPicoTask",
    8192,
    NULL,
    configMAX_PRIORITIES - 4,
    NULL,
    APP_CPU_NUM
  );

  BLEDevice::init("");
  BLEDevice::setPower(ESP_PWR_LVL_P9);
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  do
  {
    pBLEScan->start(ADV_DURATION);//ConnectPrphTask生成の前でないといけない
    delay(500);
  }
  while ((!(Server[AIRMETER].doConnect)) OPERATOR (!(Server[POWERMETER].doConnect)));
  DEBUG_println("Scan complete");

  for (int i = 0; i < MAX_SERVER; i++)
  {
    DEBUG_println("Create connect task");
    xTaskCreateUniversal(
      ConnectPrphTaskArray[i],
      "ConnectPrphTask",
      8192,
      NULL,
      configMAX_PRIORITIES - 1 - i,
      NULL,
      APP_CPU_NUM
    );
  }

  xTaskCreateUniversal(
    SendDisplayTask,
    "SendDisplayTask",
    8192,
    NULL,
    configMAX_PRIORITIES - 5,
    NULL,
    APP_CPU_NUM
  );
}

void loop() {
#ifdef DEBUG_MEM
  DEBUG_print("MEMORY:");
  DEBUG_println(ESP.getFreeHeap());
#endif
#ifdef DEBUG_TEMP
  DEBUG_print("TEMP:");
  DEBUG_println(temperatureRead());
#endif
  delay(1000);
}
