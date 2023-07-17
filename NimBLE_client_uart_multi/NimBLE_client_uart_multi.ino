/* Central Mode (client) BLE UART for ESP32

   This sketch is a central mode (client) Nordic UART Service (NUS) that connects automatically to a peripheral (server)
   Nordic UART Service. NUS is what most typical "blueart" servers emulate. This sketch will connect to your BLE uart
   device in the same manner the nRF Connect app does.

   Once Server[0].connected this sketch will switch notification on using BLE2902 for the charUUID_TX characteristic which is the
   characteristic that our server is making data available on via notification. The data received from the server
   characteristic charUUID_TX will be printed to Serial on this device. Every five seconds this device will send the
   string "Time since boot: #" to the server characteristic charUUID_RX, this will make that data available in the BLE
   uart and trigger a notifyCallback or similar depending on your BLE uart server setup.


   A brief explanation of BLE client/server actions and rolls:

   Central Mode (client) - Connects to a peripheral (server).
     -Scans for devices and reads service UUID.
     -Connects to a server's address with the desired service UUID.
     -Checks for and makes a reference to one or more characteristic UUID in the current service.
     -The client can send data to the server by writing to this RX Characteristic.
     -If the client has enabled notifications for the TX characteristic, the server can send data to the client as
     notifications to that characteristic. This will trigger the notifyCallback function.

   Peripheral (server) - Accepts connections from a central mode device (client).
     -Advertises a service UUID.
     -Creates one or more characteristic for the advertised service UUID
     -Accepts connections from a client.
     -The server can send data to the client by writing to this TX Characteristic.
     -If the server has enabled notifications for the RX characteristic, the client can send data to the server as
     notifications to that characteristic. This the default function on most "Nordic UART Service" BLE uart sketches.


   Copyright <2018> <Josh Campbell>
   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
   documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
   rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
   permit persons to whom the Software is furnished to do so, subject to the following conditions: The above copyright
   notice and this permission notice shall be included in all copies or substantial portions of the Software. THE
   SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
   WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
   COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


   Based on the "BLE_Client" example by Neil Kolban:
   https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLETests/Arduino/BLE_client/BLE_client.ino
   With help from an example by Andreas Spiess:
   https://github.com/SensorsIot/Bluetooth-BLE-on-Arduino-IDE/blob/master/Polar_Receiver/Polar_Receiver.ino
   Nordic UART Service info:
   https://infocenter.nordicsemi.com/index.jsp?topic=%2Fcom.nordic.infocenter.sdk5.v14.0.0%2Fble_sdk_app_nus_eval.html
*/
#include "StringSplitter.h"
#include <NimBLEDevice.h>
#include <Ticker.h>
#include <HardwareSerial.h>

HardwareSerial SerialPICO(0);

#define DEBUG

#ifdef DEBUG
#define DEBUG_begin(x) Serial.begin(x)
#define DEBUG_print(x) Serial.print(x)
#define DEBUG_println(x) Serial.println(x)
//#define DEBUG_wait while(!Serial)yield()
#define DEBUG_wait
#else
#define DEBUG_begin(x)
#define DEBUG_print(x)
#define DEBUG_println(x)
#define DEBUG_wait
#endif

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

// The remote Nordic UART service service we wish to connect to.
// This service exposes two characteristics: one for transmitting and one for receiving (as seen from the client).
static BLEUUID serviceUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
// The characteristics of the above service we are interested in.
// The client can send data to the server by writing to this characteristic.
static BLEUUID charUUID_RX("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");   // RX Characteristic
// If the client has enabled notifications for this characteristic,
// the server can send data to the client as notifications.
static BLEUUID charUUID_TX("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");   // TX Characteristic

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
  String AIR = "AirMeter";
  String PWR = "PowerMeter";
  //String DPY = "Display";
  if (PrphName == AIR)
  {
    //packet.AirMeterIsOpen = true;
    StringSplitter *AirData = new StringSplitter(str, ' ', 2);
    packet.AirSpeed = AirData->getItemAtIndex(0).toFloat();
    packet.AirMeterBat = AirData->getItemAtIndex(1).toFloat();

  }
  else if (PrphName == PWR)
  {
    //packet.PowerMeterIsOpen = true;
    StringSplitter *PowerData = new StringSplitter(str, ' ', 4);
    packet.Cadence = PowerData->getItemAtIndex(0).toInt();
    packet.PowerAvg = PowerData->getItemAtIndex(1).toInt();
    packet.PowerMax = PowerData->getItemAtIndex(2).toInt();
    packet.PowerMeterBat = PowerData->getItemAtIndex(3).toFloat();
  }
  /*else if (PrphName == DPY)//Readyメッセージが届いたら、送り返す
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
      String AIR = "AirMeter";
      String PWR = "PowerMeter";
      String DPY = "Display";
      DEBUG_print(" - Callback ");
      if (Server[pclient->index].name == AIR)
      {
        packet.AirMeterIsOpen = true;
        DEBUG_println("AirMeter Connected.");
      }
      else if (Server[pclient->index].name == PWR)
      {
        packet.PowerMeterIsOpen = true;
        DEBUG_println("PowerMeter Connected.");
      }
      else if (Server[pclient->index].name == DPY)
      {
        DEBUG_println("Display Connected.");
      }
    }
    void onDisconnect(BLEClient *pclient)
    {
      String AIR = "AirMeter";
      String PWR = "PowerMeter";
      String DPY = "Display";
      DEBUG_print(" - Callback ");
      if (Server[pclient->index].name == AIR)
      {
        packet.AirMeterIsOpen = false;
        Server[pclient->index].connected = false;
        Server[pclient->index].doConnect = true;
        DEBUG_println("AirMeter Disconnected. Try reConnect.");
      }
      else if (Server[pclient->index].name == PWR)
      {
        packet.PowerMeterIsOpen = false;
        Server[pclient->index].connected = false;
        Server[pclient->index].doConnect = true;
        DEBUG_println("PowerMeter Disconnected. Try reConnect.");
      }
      else if (Server[pclient->index].name == DPY)
      {
        Server[pclient->index].connected = false;
        Server[pclient->index].doConnect = true;
        DEBUG_println("Display Disconnected. Do nothing.");
      }
      //DEBUG_println("onDisconnect");
    }
};

bool connectToServer(server *peripheral) {
  DEBUG_print("Establishing a connection to device address: ");
  DEBUG_println((*peripheral->pServerAddress).toString().c_str());

  BLEClient*  pClient  = BLEDevice::createClient();
  DEBUG_println(" - Created client");

  pClient->index = peripheral->index;

  pClient->setClientCallbacks(new MyClientCallback());

  // Connect to the remove BLE Server.
  pClient->setConnectTimeout(3); //3 seconds
  pClient->connect(*peripheral->pServerAddress);
  DEBUG_println(" - Connected to server");

  // Obtain a reference to the Nordic UART service on the remote BLE server.
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    DEBUG_print("Failed to find Nordic UART service UUID: ");
    DEBUG_println(serviceUUID.toString().c_str());
    return false;
  }
  DEBUG_println(" - Remote BLE service reference established");

  // Obtain a reference to the TX characteristic of the Nordic UART service on the remote BLE server.
  peripheral->pTXCharacteristic = pRemoteService->getCharacteristic(charUUID_TX);
  if (peripheral->pTXCharacteristic == nullptr) {
    DEBUG_print("Failed to find TX characteristic UUID: ");
    DEBUG_println(charUUID_TX.toString().c_str());
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
    DEBUG_print("Failed to find our characteristic UUID: ");
    DEBUG_println(charUUID_RX.toString().c_str());
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
      DEBUG_print("BLE Advertised Device found - ");
      DEBUG_println(advertisedDevice->toString().c_str());

      // We have found a device, check to see if it contains the Nordic UART service.
      if (advertisedDevice->haveServiceUUID() && advertisedDevice->getServiceUUID().equals(serviceUUID))
      {
        DEBUG_println("Found a device with the desired ServiceUUID!");
        for (int i = 0; i < MAX_SERVER; i++)
        {
          if ((Server[i].doConnect == false) && (Server[i].connected == false))
          {
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

Ticker SendDisplayTimer;
Ticker SerialPicoTimer;

void SendDisplay()
{
  String DPY = "Display";
  for (int i = 0; i < MAX_SERVER; i++)
  {
    if (Server[i].connected && (Server[i].name == DPY))
      //if (Server[i].connected)
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
      Server[i].pRXCharacteristic->writeValue(DisplayPacket.c_str(), DisplayPacket.length());
      DEBUG_println("Send to Display");
    }

  }
}
void SerialPico()
{
  //while (SerialPICO.available() > 1);
  if (SerialPICO.read() != -1)
  {
    SerialPICO.write(packet.bin, sizeof(PACKET));
    DEBUG_println("Send to Pico");
  }
  while (SerialPICO.available() > 0)
  {
    SerialPICO.read();
  }
}

void setup() {
  SerialPICO.begin(115200, SERIAL_8N1, -1, -1);
  DEBUG_begin(115200);
  DEBUG_wait;

  DEBUG_println("Starting Arduino BLE Central Mode (Client) Nordic UART Service");

  BLEDevice::init("");

  SerialPicoTimer.attach(0.1, SerialPico);

} // End of setup.

void loop() {
  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device. Specify that we want active scanning and start the
  // scan to run for 30 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->start(15);

  SendDisplayTimer.attach(0.5, SendDisplay);
  //SerialPicoTimer.attach(0.1, SerialPico);

  while (true) {
    // If the flag "Server[0].doConnect" is true then we have scanned for and found the desired
    // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
    // Server[0].connected we set the Server[0].connected flag to be true.
    for (int i = 0; i < MAX_SERVER; i++)
    {
      if (Server[i].doConnect == true) {
        if (connectToServer(&Server[i])) {
          DEBUG_println("We are now connected to the BLE Server.");
          Server[i].connected = true;
          //Server[i].pTXCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)notificationOn, 2, true);
          Server[i].doConnect = false; //false only when connect success
        }
        else
        {
          DEBUG_println("Failed to connect to the server. Try again later.");
        }

      }
    }

    /*String DPY = "Display";
      for (int i = 0; i < MAX_SERVER; i++)
      {
      if (Server[i].connected && (Server[i].name == DPY))
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
        Server[i].pRXCharacteristic->writeValue(DisplayPacket.c_str(), DisplayPacket.length());
        DEBUG_println("Send to Display");
      }
      }
      while (SerialPICO.available() > 1);
      if (SerialPICO.read() != -1)
      {
      SerialPICO.write(packet.bin, sizeof(PACKET));
      DEBUG_println("Send to Pico");
      }*/

    delay(100);

    DEBUG_print("MEMORY:");

    DEBUG_println(ESP.getFreeHeap());
    //auto tmpArray = new long[1000];
  }
} // End of loop
