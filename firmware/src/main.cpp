/**
 * ESP32 CO2 Drift Compensator - 气泵式闭环监测系统
 * 
 * 功能：自动启动监测、气泵PWM控制、整点对齐、断电自恢复、串口控制
 * 
 * 硬件:
 * - ESP32-S3-DevKitC-1
 * - ZG09SR CO2传感器 (Modbus RTU)
 * - 5V微型气泵 (PWM控制)
 * 
 * 配置文件: config.h (从 config.h.example 复制)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ModbusMaster.h>
#include <time.h>
#include "config.h"

// ==================== 硬件引脚配置 ====================
#define PUMP_PIN 18           // 气泵PWM引脚
#define RX_PIN 16             // CO2传感器RX
#define TX_PIN 17             // CO2传感器TX
#define PWM_CHANNEL 0
#define PWM_FREQ 5000
#define PWM_RES 8
#define SENSOR_ID 254         // Modbus从机地址
#define CO2_REG_ADDR 0x000B   // CO2数据寄存器

// ==================== 全局变量 ====================
ModbusMaster node;
WiFiMulti wifiMulti;
bool autoMode = true;         // 默认自动运行
time_t nextScheduleTime = 0;
unsigned long lastStatusPrint = 0;

// ==================== 函数声明 ====================
void setupWiFiList();
bool checkNetwork();
void handleSerialCommand();
void executeMeasurementLogic(time_t plannedTimestamp, bool allowUpload);
void runVentilation();
void printSystemCheck();
uint16_t readZG09SR();
void controlPump(bool on, int duty);
time_t getNextAlignedEpoch();
bool isTimeSynced();
void syncNTPTime();

// ==================== 初始化 ====================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n========================================");
    Serial.println("   ESP32 CO2 Drift Compensator");
    Serial.println("========================================");

    // 1. 硬件初始化
    ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
    ledcAttachPin(PUMP_PIN, PWM_CHANNEL);
    controlPump(false, 0);

    Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
    node.begin(SENSOR_ID, Serial2);

    // 2. 联网
    setupWiFiList();
    Serial.println(">>> 系统启动中，正在搜索网络...");

    // 3. 同步时间
    if (checkNetwork()) {
        configTime(GMT_OFFSET_SEC, 0, NTP_SERVER1, NTP_SERVER2);
        struct tm t;
        if (getLocalTime(&t, 3000)) {
            nextScheduleTime = getNextAlignedEpoch();
        }
    }

    // 4. 打印状态
    printSystemCheck();

    Serial.println("\n命令提示:");
    Serial.println("  stop   - 暂停自动，进入手动模式");
    Serial.println("  auto   - 恢复自动监测");
    Serial.println("  single - 单次测量 (手动模式下不上传)");
    Serial.println("  vent   - 开启通风");
    Serial.println("  status - 查看当前状态");
    Serial.println("========================================\n");
}

// ==================== 主循环 ====================
void loop() {
    handleSerialCommand();

    if (autoMode) {
        wifiMulti.run();

        if (!isTimeSynced()) {
            if (millis() - lastStatusPrint > 2000) {
                Serial.print(".");
                lastStatusPrint = millis();
            }
            return;
        }

        time_t now = time(NULL);
        if (nextScheduleTime == 0) {
            nextScheduleTime = getNextAlignedEpoch();
            printSystemCheck();
        }

        if (now >= nextScheduleTime) {
            Serial.println("\n>>> 自动任务触发 <<<");
            executeMeasurementLogic(nextScheduleTime, true);
            
            // 每次测量完成后重新同步NTP时间，纠正RTC累积漂移
            syncNTPTime();
            
            nextScheduleTime = getNextAlignedEpoch();
            printSystemCheck();
        }
    }

    delay(50);
}

// ==================== 核心测量逻辑 ====================
void executeMeasurementLogic(time_t plannedTimestamp, bool allowUpload) {
    Serial.println("------------------------------");

    // 1. 气泵采样
    Serial.printf("1. [气泵] 采样循环 (%d秒)...\n", PUMP_TIME_MEASURE);
    controlPump(true, PUMP_DUTY_MEASURE);
    delay(PUMP_TIME_MEASURE * 1000);
    controlPump(false, 0);

    // 2. 气压平衡
    Serial.printf("2. [系统] 气压平衡 (%d秒)...\n", SENSOR_STABILIZE_TIME);
    delay(SENSOR_STABILIZE_TIME * 1000);

    // 3. 读取传感器
    Serial.print("3. [传感] 读取数据... ");
    uint16_t co2 = readZG09SR();

    if (co2 > 0) {
        Serial.printf("CO2: %d ppm\n", co2);

        // 4. 上传判断
        if (allowUpload) {
            if (checkNetwork()) {
                Serial.print("4. [云端] 正在上传... ");
                HTTPClient http;
                http.begin(SERVER_URL);
                http.addHeader("Content-Type", "application/json");

                struct tm pTm;
                localtime_r(&plannedTimestamp, &pTm);
                char timeStr[25];
                strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &pTm);

                JsonDocument doc;
                doc["timestamp"] = timeStr;
                doc["co2"] = co2;
                String jsonPayload;
                serializeJson(doc, jsonPayload);

                int code = http.POST(jsonPayload);
                if (code == 200) Serial.println("成功");
                else Serial.printf("失败 (Code:%d)\n", code);
                http.end();
            } else {
                Serial.println("4. [云端] 网络断开，跳过");
            }
        } else {
            Serial.println("4. [云端] 手动模式，不上传");
        }
    } else {
        Serial.println("读取失败");
    }
    Serial.println("------------------------------");
}

// ==================== 通风功能 ====================
void runVentilation() {
    Serial.println("\n------------------------------");
    Serial.println("[通风模式] 已启动");
    Serial.printf("强度: %d, 时长: %d秒\n", PUMP_DUTY_VENT, PUMP_TIME_VENT);

    controlPump(true, PUMP_DUTY_VENT);

    for (int i = PUMP_TIME_VENT; i > 0; i -= 5) {
        delay(5000);
        Serial.printf("... %d秒\n", i - 5);
    }

    controlPump(false, 0);
    Serial.println("[通风模式] 完成");
    Serial.println("------------------------------");
}

// ==================== 系统状态面板 ====================
void printSystemCheck() {
    bool net = checkNetwork();
    bool timeSync = isTimeSynced();

    Serial.println("\n+--- [ 系统状态面板 ] ---");

    Serial.printf("| WiFi: %s ", net ? "OK" : "NO");
    if (net) Serial.printf("(%s)", WiFi.SSID().c_str());
    Serial.println();

    Serial.printf("| 时间: %s ", timeSync ? "OK" : "等待同步");
    if (timeSync) {
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);
        Serial.printf("%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    }
    Serial.println();

    Serial.printf("| 模式: %s\n", autoMode ? "自动运行中" : "手动/停止");

    if (autoMode && timeSync && nextScheduleTime > 0) {
        struct tm t;
        localtime_r(&nextScheduleTime, &t);
        Serial.printf("| 计划: 下次测量 @ %02d:%02d:%02d\n", t.tm_hour, t.tm_min, t.tm_sec);
    }

    Serial.println("+------------------------");
}

// ==================== 串口指令处理 ====================
void handleSerialCommand() {
    if (Serial.available()) {
        String s = Serial.readStringUntil('\n');
        s.trim();
        s.toLowerCase();
        if (s.length() == 0) return;

        Serial.printf("\n[指令] 收到: %s\n", s.c_str());

        if (s == "stop") {
            autoMode = false;
            controlPump(false, 0);
            printSystemCheck();
        }
        else if (s == "auto") {
            autoMode = true;
            Serial.println("已恢复自动模式");
            if (isTimeSynced()) {
                nextScheduleTime = getNextAlignedEpoch();
            } else {
                nextScheduleTime = 0;
            }
            printSystemCheck();
        }
        else if (s == "single") {
            Serial.printf(">>> 执行单次测量 (上传: %s) <<<\n", autoMode ? "开" : "关");
            executeMeasurementLogic(time(NULL), autoMode);
        }
        else if (s == "vent") {
            runVentilation();
        }
        else if (s == "status") {
            printSystemCheck();
        }
        else {
            Serial.println("未知指令");
        }
    }
}

// ==================== WiFi配置 ====================
void setupWiFiList() {
#ifdef WIFI_SSID_1
    wifiMulti.addAP(WIFI_SSID_1, WIFI_PASS_1);
#endif
#ifdef WIFI_SSID_2
    wifiMulti.addAP(WIFI_SSID_2, WIFI_PASS_2);
#endif
#ifdef WIFI_SSID_3
    wifiMulti.addAP(WIFI_SSID_3, WIFI_PASS_3);
#endif
}

// ==================== 传感器读取 ====================
uint16_t readZG09SR() {
    uint8_t result;
    for (int i = 0; i < 3; i++) {
        result = node.readHoldingRegisters(CO2_REG_ADDR, 1);
        if (result == node.ku8MBSuccess) return node.getResponseBuffer(0);
        delay(200);
    }
    return 0;
}

// ==================== 气泵控制 ====================
void controlPump(bool on, int duty) {
    ledcWrite(PWM_CHANNEL, on ? duty : 0);
}

// ==================== 时间对齐 ====================
time_t getNextAlignedEpoch() {
    time_t now = time(NULL);
    time_t next = (now / INTERVAL_SECONDS + 1) * INTERVAL_SECONDS;
    if (next - now < 10) next += INTERVAL_SECONDS;
    return next;
}

// ==================== NTP时间同步 ====================
void syncNTPTime() {
    if (checkNetwork()) {
        Serial.print("   [NTP] 正在同步时间... ");
        configTime(GMT_OFFSET_SEC, 0, NTP_SERVER1, NTP_SERVER2);
        struct tm t;
        if (getLocalTime(&t, 3000)) {
            Serial.printf("成功 (%02d:%02d:%02d)\n", t.tm_hour, t.tm_min, t.tm_sec);
        } else {
            Serial.println("超时");
        }
    }
}

// ==================== 辅助函数 ====================
bool checkNetwork() { return wifiMulti.run() == WL_CONNECTED; }
bool isTimeSynced() { struct tm t; return getLocalTime(&t, 0) && t.tm_year > (2020 - 1900); }
