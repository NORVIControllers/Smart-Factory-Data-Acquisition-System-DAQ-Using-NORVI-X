/*  ===========================================================================
 *  NORVI X  -  Smart Factory Data Acquisition System (DAQ)   FW 1.1.0
 *  CPU: NORVI X-CPU-ESPS3-X1 (ESP32-S3-N16R2)
 *  AI4 (ADS1115) | DI16 (PCA9555) | RS-485 Modbus RTU | MQTT/JSON -> Grafana
 *
 *  Libraries: PubSubClient, Adafruit_ADS1X15, RTClib, ModbusMaster,
 *             clsPCA9555, PCA9539, PCA9536D, TFT_eSPI, CST816S
 *  Board: ESP32S3 Dev Module, 16MB Flash, OPI PSRAM
 *  Edit Config.h (settings) and IOList.h (I/O tags). This tab needs no edits.
 *  =========================================================================== */

#include "Config.h"
#include "IOList.h"
#include <Wire.h>
#include <SPI.h>
#include "SD.h"
#include "RTClib.h"
#include "PCA9539.h"
#include <PCA9536D.h>
#include <Adafruit_ADS1X15.h>
#include "clsPCA9555.h"
#include <ModbusMaster.h>
#include <PubSubClient.h>

#if USE_ETHERNET
  #include <Ethernet.h>
  #include <EthernetUdp.h>
  EthernetClient netClient;
  EthernetUDP    ntpUdp;
#else
  #include <WiFi.h>
  #include <time.h>
  WiFiClient netClient;
#endif
#if ENABLE_DISPLAY
  #include "TFT_eSPI.h"
  #include "Free_Fonts.h"
  TFT_eSPI tft = TFT_eSPI();
#endif

// ---- Peripherals ----
PCA9539 pcaOut(ADDR_PCA9539);
PCA9536 pcaIo;
RTC_DS3231 rtc;
Adafruit_ADS1115 ads[MAX_MODULES];
PCA9555 di0(0x27), di1(0x26);
PCA9555* diMod[MAX_MODULES] = { &di0, &di1 };
ModbusMaster mbNode[16];
PubSubClient mqtt(netClient);

// ---- Status flags ----
bool rtcOk=false, sdOk=false, pcaOk=false, netUp=false, mqttUp=false;
bool adsOk[MAX_MODULES]={false}, diOk[MAX_MODULES]={false};
uint32_t publishCount=0, bufferedCount=0;
uint16_t netRetries=0, mqttRetries=0;

// ---- Scheduler ----
uint32_t tDI=0,tAI=0,tPub=0,tHb=0,tDsp=0,tNet=0,tMqtt=0,tFlush=0,tNtp=0,tSd=0;
bool ledTick=false, ntpDone=false;

#define T_DATA   TOPIC_ROOT "/data"
#define T_STATUS TOPIC_ROOT "/status"
#define T_CMD    TOPIC_ROOT "/cmd"

void publishTelemetry();
bool mqttPublishData(const char* p);

// ===========================================================================
//  RS-485 DIRECTION CONTROL
// ===========================================================================
void rs485Tx(){ digitalWrite(RS485_DE,HIGH); }
void rs485Rx(){ digitalWrite(RS485_DE,LOW);  }

// ===========================================================================
//  TIME / RTC
// ===========================================================================
String isoTimestamp(){
  char b[24];
  if(rtcOk){ DateTime n=rtc.now();
    snprintf(b,sizeof(b),"%04d-%02d-%02dT%02d:%02d:%02dZ",
             n.year(),n.month(),n.day(),n.hour(),n.minute(),n.second());
  } else { uint32_t s=millis()/1000;
    snprintf(b,sizeof(b),"1970-01-01T%02lu:%02lu:%02luZ",(s/3600)%24,(s/60)%60,s%60);
  }
  return String(b);
}
void setRtcEpoch(uint32_t e){ if(rtcOk && e>1600000000UL){ rtc.adjust(DateTime(e));
  Serial.println(F("[TIME] RTC synced from NTP")); } }

// ===========================================================================
//  SD  (robust init + retry)
// ===========================================================================
bool sdInit(){
  // Free the shared SPI bus: deselect the W5500 (its floating CS is the usual
  // cause of SD mount / gpio errors on this board) before talking to the card.
  pinMode(ETH_CS,OUTPUT); digitalWrite(ETH_CS,HIGH);
  pinMode(SD_CS,OUTPUT);  digitalWrite(SD_CS,HIGH);
  delay(5);
  bool ok = SD.begin(SD_CS, SPI, SD_SPI_HZ);
  if(ok) Serial.printf("[SD ] mounted, %llu MB\n", SD.cardSize()/(1024ULL*1024ULL));
  else   Serial.println(F("[SD ] not mounted (check card seated + FAT32)"));
  return ok;
}

// ===========================================================================
//  BOARD BRING-UP
// ===========================================================================
void i2cScan(){
  Serial.println(F("[I2C] scanning..."));
  for(uint8_t a=1;a<127;a++){ Wire.beginTransmission(a);
    if(Wire.endTransmission()==0) Serial.printf("[I2C]  device @0x%02X\n",a); }
}
void boardBegin(){
  Wire.begin(I2C_SDA,I2C_SCL); delay(50); i2cScan();
  pinMode(PCA9539_RST,OUTPUT); digitalWrite(PCA9539_RST,HIGH); delay(20);

  pcaOk = pcaIo.begin();
  if(pcaOk){ pcaIo.pinMode(IO_PB1,INPUT); pcaIo.pinMode(IO_PB3,INPUT);
    pcaIo.pinMode(LED_RUN,OUTPUT); pcaIo.pinMode(LED_NET,OUTPUT);
    pcaIo.digitalWrite(LED_RUN,HIGH); pcaIo.digitalWrite(LED_NET,HIGH);
    Serial.println(F("[BRD] PCA9536 OK")); }
  else Serial.println(F("[BRD] PCA9536 not found"));

  SPI.begin(SPI_SCLK,SPI_MISO,SPI_MOSI); delay(50);

  rtcOk = rtc.begin();
  if(rtcOk){ if(rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__),F(__TIME__)));
    Serial.println(F("[BRD] RTC DS3231 OK")); }
  else Serial.println(F("[BRD] RTC not found"));

  sdOk = sdInit();          // initialise SD at setup
  tSd  = millis();
}
void setLed(uint8_t led,bool on){ if(pcaOk) pcaIo.digitalWrite(led,on?HIGH:LOW); }

// ===========================================================================
//  NETWORK + NTP
// ===========================================================================
void netBegin(){
#if USE_ETHERNET
  Serial.println(F("[NET] Ethernet (W5500) starting..."));
  Ethernet.init(ETH_CS); byte mac[6]; memcpy(mac,ETH_MAC,6);
 #if USE_DHCP
  if(Ethernet.begin(mac)==0) Serial.println(F("[NET] DHCP failed"));
 #endif
#else
  Serial.printf("[NET] Wi-Fi connecting to %s ...\n",WIFI_SSID);
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID,WIFI_PASS);
#endif
}
bool linkUp(){
#if USE_ETHERNET
  return Ethernet.linkStatus()!=LinkOFF;
#else
  return WiFi.status()==WL_CONNECTED;
#endif
}
void syncNtp(){
#if !USE_ETHERNET
  configTime(0,0,NTP_SERVER); struct tm tm;
  if(getLocalTime(&tm,3000)){ setRtcEpoch((uint32_t)mktime(&tm)); ntpDone=true; tNtp=millis(); }
#else
  static byte buf[48]; ntpUdp.begin(8888); memset(buf,0,48);
  buf[0]=0b11100011; buf[1]=0; buf[2]=6; buf[3]=0xEC;
  buf[12]=49; buf[13]=0x4E; buf[14]=49; buf[15]=52;
  ntpUdp.beginPacket(NTP_SERVER,123); ntpUdp.write(buf,48); ntpUdp.endPacket();
  delay(1000);
  if(ntpUdp.parsePacket()){ ntpUdp.read(buf,48);
    unsigned long hi=word(buf[40],buf[41]),lo=word(buf[42],buf[43]);
    setRtcEpoch((hi<<16|lo)-2208988800UL); ntpDone=true; tNtp=millis(); }
#endif
}
void netUpdate(){
  bool up=linkUp();

  if(up && !netUp){                       // <<< link just came up: announce it
#if !USE_ETHERNET
    Serial.print(F("[NET] Wi-Fi connected, IP: ")); Serial.println(WiFi.localIP());
    Serial.print(F("[NET] RSSI: ")); Serial.print(WiFi.RSSI()); Serial.println(F(" dBm"));
#else
    Serial.print(F("[NET] Ethernet link up, IP: ")); Serial.println(Ethernet.localIP());
#endif
    netRetries=0;
  }
  if(!up && netUp){
    Serial.println(F("[NET] link DOWN -> entering reconnect cycle"));
  }
  netUp=up;

  // ---- retry cycle while offline ----
  if(!netUp && millis()-tNet>WIFI_RETRY_MS){
    tNet=millis(); netRetries++;
    Serial.printf("[NET] reconnect attempt #%u\n",netRetries);
#if !USE_ETHERNET
    WiFi.disconnect(); WiFi.begin(WIFI_SSID,WIFI_PASS);
#else
    Ethernet.maintain();
#endif
    return;
  }

  if(netUp && (!ntpDone || millis()-tNtp>NTP_RESYNC_MS)) syncNtp();
}

// ===========================================================================
//  LAYER 1 - ANALOGUE INPUTS (AI4 / ADS1115)
// ===========================================================================
void aiBegin(){
  uint8_t maxMod=0;
  for(uint8_t i=0;i<AI_COUNT;i++) if(aiList[i].enabled && aiList[i].module+1>maxMod) maxMod=aiList[i].module+1;
  if(maxMod>MAX_MODULES) maxMod=MAX_MODULES;
  for(uint8_t m=0;m<maxMod;m++){ adsOk[m]=ads[m].begin(ADS_ADDR[m]);
    if(adsOk[m]){ ads[m].setGain(GAIN_ONE);
      Serial.printf("[AI ] ADS1115 #%u @0x%02X OK\n",m,ADS_ADDR[m]); }
    else Serial.printf("[AI ] ADS1115 #%u @0x%02X NOT FOUND\n",m,ADS_ADDR[m]); }
}
float scale(float x,float a,float b,float c,float d){ return (b==a)?c:c+(x-a)*(d-c)/(b-a); }
void aiPoll(){
  for(uint8_t i=0;i<AI_COUNT;i++){ AiChannel&c=aiList[i];
    if(!c.enabled || c.module>=MAX_MODULES || !adsOk[c.module]){ c.fault=true; continue; }
    int16_t raw=ads[c.module].readADC_SingleEnded(c.ch);
    if(c.mode==AI_MODE_420MA){ float mA=raw*AI_MA_FACTOR; c.raw=mA;
      c.fault=(mA<AI_WIREBREAK_MA); c.eng=scale(mA,4.0f,20.0f,c.engMin,c.engMax); }
    else { float v=ads[c.module].computeVolts(raw)*AI_V_DIVIDER; c.raw=v;
      c.fault=false; c.eng=scale(v,0.0f,10.0f,c.engMin,c.engMax); }
  }
}

// ===========================================================================
//  LAYER 2 - DIGITAL INPUTS (DI16 / PCA9555)  debounce + edge counter
// ===========================================================================
void diBegin(){
  uint8_t maxMod=0;
  for(uint8_t i=0;i<DI_COUNT;i++) if(diList[i].enabled && diList[i].module+1>maxMod) maxMod=diList[i].module+1;
  if(maxMod>MAX_MODULES) maxMod=MAX_MODULES;
  for(uint8_t m=0;m<maxMod;m++){ Wire.beginTransmission(PCA_ADDR[m]);
    diOk[m]=(Wire.endTransmission()==0);
    if(diOk[m]){ for(uint8_t p=0;p<16;p++) diMod[m]->pinMode(p,INPUT);
      Serial.printf("[DI ] PCA9555 #%u @0x%02X OK\n",m,PCA_ADDR[m]); }
    else Serial.printf("[DI ] PCA9555 #%u @0x%02X NOT FOUND\n",m,PCA_ADDR[m]); }
}
void diPoll(){
  uint32_t now=millis();
  for(uint8_t i=0;i<DI_COUNT;i++){ DiChannel&c=diList[i];
    if(!c.enabled || c.module>=MAX_MODULES || !diOk[c.module]) continue;
    bool raw=(diMod[c.module]->digitalRead(c.pin)!=0); if(c.invert) raw=!raw;
    if(raw!=c.candidate){ c.candidate=raw; c.tCandidate=now; }
    else if(raw!=c.state && now-c.tCandidate>=DI_DEBOUNCE_MS){
      if(!c.state && raw) c.count++; c.state=raw; }
  }
}

// ===========================================================================
//  LAYER 3 - RS-485 MODBUS RTU  (one transaction per pass + dead-slave backoff)
// ===========================================================================
void mbBegin(){
#if ENABLE_MODBUS
  pinMode(RS485_DE,OUTPUT); digitalWrite(RS485_DE,LOW);
  Serial2.begin(RS485_BAUD,SERIAL_8N1,RS485_RX,RS485_TX);
  uint32_t now=millis();
  for(uint8_t i=0;i<MB_COUNT && i<16;i++){
    mbNode[i].begin(mbList[i].slaveId,Serial2);
    mbNode[i].preTransmission(rs485Tx); mbNode[i].postTransmission(rs485Rx);
    mbList[i].lastPoll=now;          // stagger first poll so boot isn't a stall burst
  }
  Serial.printf("[MB ] Modbus RTU ready: %u regs @%d baud\n",MB_COUNT,RS485_BAUD);
#else
  Serial.println(F("[MB ] Modbus disabled (ENABLE_MODBUS 0)"));
#endif
}
void mbPoll(uint8_t i){
  ModbusReg&r=mbList[i];
  uint8_t qty=(r.dtype==MB_U32||r.dtype==MB_FLOAT32)?2:1;
  uint8_t res=(r.fc==MB_FC_INPUT)?mbNode[i].readInputRegisters(r.addr,qty)
                                 :mbNode[i].readHoldingRegisters(r.addr,qty);
  if(res!=mbNode[i].ku8MBSuccess){ r.valid=false; return; }
  uint16_t w0=mbNode[i].getResponseBuffer(0); float v;
  if(qty==2){ uint16_t w1=mbNode[i].getResponseBuffer(1);
    uint32_t raw=r.wordSwap?((uint32_t)w1<<16)|w0:((uint32_t)w0<<16)|w1;
    if(r.dtype==MB_FLOAT32){ float f; memcpy(&f,&raw,4); v=f; } else v=(float)raw; }
  else v=(r.dtype==MB_S16)?(float)(int16_t)w0:(float)w0;
  r.value=v*r.scale+r.offset; r.valid=true;
}
void mbUpdate(){
#if ENABLE_MODBUS
  if(MB_COUNT==0) return;
  uint32_t now=millis();
  static uint8_t idx=0;
  for(uint8_t scanned=0; scanned<MB_COUNT && scanned<16; scanned++){
    uint8_t i=idx; idx=(idx+1)%MB_COUNT;
    ModbusReg&r=mbList[i];
    if(!r.enabled) continue;
    if((int32_t)(now-r.lastPoll) < (int32_t)r.pollMs) continue;       // not due
    if(r.downUntil && (int32_t)(now-r.downUntil) < 0) continue;       // parked
    r.lastPoll=now;
    mbPoll(i);                                                        // 1 txn / pass
    if(r.valid){
      if(r.errCount) Serial.printf("[MB ] %s recovered\n", r.key);
      r.errCount=0; r.downUntil=0;
    } else {
      if(r.errCount<60000) r.errCount++;
      if(r.errCount>=MB_MAX_ERRORS){
        r.downUntil = now + MB_BACKOFF_MS;
        Serial.printf("[MB ] %s (slave %u) no response -> parked %us\n",
                      r.key, r.slaveId, (unsigned)(MB_BACKOFF_MS/1000));
      }
    }
    return;  // only one Modbus transaction per loop pass -> loop stays responsive
  }
#endif
}

// ===========================================================================
//  LAYER 4 - JSON PAYLOAD (dependency-free)
// ===========================================================================
static size_t aS(char*o,size_t cap,size_t n,const char*s){ while(*s&&n<cap-1)o[n++]=*s++; return n; }
static size_t aN(char*o,size_t cap,size_t n,float v,uint8_t dp){ char t[24]; dtostrf(v,0,dp,t); return aS(o,cap,n,t); }
void buildPayload(char*o,size_t cap,const char*iso){
  size_t n=0;
  n=aS(o,cap,n,"{\"device\":\"" DEVICE_ID "\",\"site\":\"" SITE_ID "\",\"line\":\"" LINE_ID "\",\"ts\":\"");
  n=aS(o,cap,n,iso); n=aS(o,cap,n,"\"");
  bool f;
  n=aS(o,cap,n,",\"ai\":{"); f=true;
  for(uint8_t i=0;i<AI_COUNT;i++){ AiChannel&c=aiList[i]; if(!c.enabled)continue;
    if(!f)n=aS(o,cap,n,","); f=false; n=aS(o,cap,n,"\""); n=aS(o,cap,n,c.key); n=aS(o,cap,n,"\":");
    n=c.fault?aS(o,cap,n,"null"):aN(o,cap,n,c.eng,2); }
  n=aS(o,cap,n,"},\"di\":{"); f=true;
  for(uint8_t i=0;i<DI_COUNT;i++){ DiChannel&c=diList[i]; if(!c.enabled)continue;
    if(!f)n=aS(o,cap,n,","); f=false; n=aS(o,cap,n,"\""); n=aS(o,cap,n,c.key); n=aS(o,cap,n,"\":");
    n=aS(o,cap,n,c.state?"1":"0"); }
  n=aS(o,cap,n,"},\"cnt\":{"); f=true;
  for(uint8_t i=0;i<DI_COUNT;i++){ DiChannel&c=diList[i]; if(!c.enabled)continue;
    if(!f)n=aS(o,cap,n,","); f=false; char t[16]; ultoa(c.count,t,10);
    n=aS(o,cap,n,"\""); n=aS(o,cap,n,c.key); n=aS(o,cap,n,"\":"); n=aS(o,cap,n,t); }
  n=aS(o,cap,n,"},\"mb\":{"); f=true;
  for(uint8_t i=0;i<MB_COUNT;i++){ ModbusReg&r=mbList[i]; if(!r.enabled)continue;
    if(!f)n=aS(o,cap,n,","); f=false; n=aS(o,cap,n,"\""); n=aS(o,cap,n,r.key); n=aS(o,cap,n,"\":");
    n=r.valid?aN(o,cap,n,r.value,3):aS(o,cap,n,"null"); }
  n=aS(o,cap,n,"}}"); o[n<cap?n:cap-1]='\0';
}

// ===========================================================================
//  STORE-AND-FORWARD (SD)
// ===========================================================================
void storeAppend(const char*p){
#if ENABLE_STORE_FWD
  if(!sdOk) return;
  if(SD.exists(BUFFER_FILE)){ File c=SD.open(BUFFER_FILE,FILE_READ); size_t s=c.size(); c.close();
    if(s>BUFFER_MAX_BYTES){ SD.remove(BUFFER_FILE); bufferedCount=0;
      Serial.println(F("[SF ] buffer cap -> reset")); } }
  File fp=SD.open(BUFFER_FILE,FILE_APPEND);
  if(fp){ fp.println(p); fp.close(); bufferedCount++; }
#endif
}
void storeFlush(){
#if ENABLE_STORE_FWD
  if(!sdOk||!SD.exists(BUFFER_FILE)) return;
  if(millis()-tFlush<1000) return; tFlush=millis();
  File in=SD.open(BUFFER_FILE,FILE_READ); if(!in) return;
  const char* TMP="/daq_tmp.jsonl"; if(SD.exists(TMP)) SD.remove(TMP);
  File out=SD.open(TMP,FILE_WRITE);
  uint16_t sent=0; bool stop=false; String line;
  while(in.available()){ line=in.readStringUntil('\n'); line.trim(); if(!line.length())continue;
    if(!stop && sent<FLUSH_MAX_LINES && mqttPublishData(line.c_str())){
      sent++; if(bufferedCount) bufferedCount--; }
    else { stop=true; if(out) out.println(line); } }
  in.close(); if(out) out.close();
  SD.remove(BUFFER_FILE); if(SD.exists(TMP)) SD.rename(TMP,BUFFER_FILE);
  if(sent) Serial.printf("[SF ] replayed %u buffered records\n",sent);
#endif
}

// ===========================================================================
//  MQTT
// ===========================================================================
void onMqtt(char* topic,byte* payload,unsigned int len){
  String cmd; for(unsigned int i=0;i<len;i++) cmd+=(char)payload[i];
  Serial.printf("[MQTT] cmd: %s\n",cmd.c_str());
  if(cmd=="publish_now") publishTelemetry();
}
void mqttBegin(){
  mqtt.setServer(MQTT_HOST,MQTT_PORT); mqtt.setKeepAlive(MQTT_KEEPALIVE);
  mqtt.setBufferSize(PAYLOAD_BUF); mqtt.setCallback(onMqtt);
}
void mqttReconnect(){
  mqttRetries++;
  Serial.printf("[MQTT] connect attempt #%u ... ",mqttRetries); bool ok;
  if(strlen(MQTT_USER)>0) ok=mqtt.connect(DEVICE_ID,MQTT_USER,MQTT_PASS,T_STATUS,1,true,"offline");
  else                    ok=mqtt.connect(DEVICE_ID,T_STATUS,1,true,"offline");
  if(ok){ Serial.println(F("connected")); mqttRetries=0;
    mqtt.publish(T_STATUS,"online",true); mqtt.subscribe(T_CMD);
    storeFlush();                          // push buffered data right after reconnect
  } else Serial.printf("failed rc=%d (retry in %ums)\n",mqtt.state(),MQTT_RETRY_MS);
}
void mqttUpdate(){
  if(!netUp){ mqttUp=false; return; }
  if(!mqtt.connected()){ if(millis()-tMqtt>MQTT_RETRY_MS){ tMqtt=millis(); mqttReconnect(); } }
  else mqtt.loop();
  mqttUp=mqtt.connected();
}
bool mqttPublishData(const char* p){ if(!mqtt.connected()) return false;
  bool ok=mqtt.publish(T_DATA,p); if(ok) publishCount++; return ok; }

void publishTelemetry(){
  static char buf[PAYLOAD_BUF]; String ts=isoTimestamp();
  buildPayload(buf,sizeof(buf),ts.c_str());
  if(mqtt.connected() && mqttPublishData(buf)) return;
  storeAppend(buf);
  Serial.println(F("[PUB] broker offline -> buffered to SD"));
}

// ===========================================================================
//  DISPLAY (optional)
// ===========================================================================
void dspBegin(){
#if ENABLE_DISPLAY
  tft.init(); tft.begin(); tft.setRotation(0); tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(FSB12); tft.setTextColor(TFT_YELLOW);
  tft.setCursor(40,24); tft.print("NORVI X"); tft.setCursor(28,46); tft.print("DAQ NODE");
#endif
}
void dspUpdate(){
#if ENABLE_DISPLAY
  tft.fillScreen(TFT_BLACK); tft.setFreeFont(FSB12); tft.setTextColor(TFT_CYAN);
  tft.setCursor(28,24); tft.print("NORVI X DAQ"); tft.setFreeFont(FSB9);
  tft.setCursor(8,70); tft.setTextColor(netUp?TFT_GREEN:TFT_RED);  tft.print(netUp?"NET  UP":"NET DOWN");
  tft.setCursor(8,92); tft.setTextColor(mqttUp?TFT_GREEN:TFT_RED); tft.print(mqttUp?"MQTT OK":"MQTT --");
  tft.setTextColor(sdOk?TFT_GREEN:TFT_RED); tft.setCursor(8,114); tft.print(sdOk?"SD  OK":"SD --");
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(8,140); tft.printf("Pub:%lu",publishCount);
  tft.setCursor(8,162); tft.printf("Buf:%lu",bufferedCount);
  uint8_t row=190;
  for(uint8_t i=0;i<AI_COUNT && i<3;i++){ AiChannel&c=aiList[i]; if(!c.enabled)continue;
    tft.setTextColor(c.fault?TFT_RED:TFT_YELLOW); tft.setCursor(8,row);
    tft.printf("%s:%.1f%s",c.key,c.eng,c.unit); row+=22; }
#endif
}

// ===========================================================================
//  SETUP / LOOP
// ===========================================================================
void setup(){
  Serial.begin(115200); delay(500);
  Serial.println(F("\n=== NORVI X Smart Factory DAQ ==="));
  Serial.print(F("FW ")); Serial.print(FW_VERSION);
  Serial.print(F("  Device ")); Serial.println(DEVICE_ID);

  boardBegin(); aiBegin(); diBegin(); mbBegin(); dspBegin();
  netBegin(); mqttBegin();
  Serial.println(F("[BOOT] ready"));
}
void loop(){
  uint32_t now=millis();
  netUpdate(); mqttUpdate();

  // SD retry while not mounted (so store-and-forward recovers without reboot)
  if(!sdOk && now-tSd>=SD_RETRY_MS){ tSd=now;
    Serial.println(F("[SD ] re-init attempt..."));
    sdOk=sdInit(); }

  if(now-tDI>=DI_POLL_MS){ tDI=now; diPoll(); }
  if(now-tAI>=AI_POLL_MS){ tAI=now; aiPoll(); }
  mbUpdate();
  if(now-tPub>=PUBLISH_MS){ tPub=now; publishTelemetry(); }
  if(mqtt.connected()) storeFlush();
  if(now-tHb>=1000){ tHb=now; ledTick=!ledTick; setLed(LED_RUN,ledTick); setLed(LED_NET,mqttUp); }
  if(now-tDsp>=DISPLAY_MS){ tDsp=now; dspUpdate(); }
}
