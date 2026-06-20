/*
 * ESP32-C3 多功能桌面摆件：时钟 + 天气 + GIF动画 + 恐龙小游戏
 * 按钮导航：GPIO2左, GPIO3右, GPIO4返回, GPIO5确认/跳跃
 */

#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <U8g2lib.h>

#include "MAX30105.h"          // SparkFun MAX3010x 库（请在库管理器中安装）
#include "heartRate.h"         // 心率算法（库自带示例）
#include "spo2_algorithm.h"    // 血氧算法（库自带示例）

#define FINGER_DETECT_THRESHOLD 5000
#define SAMPLE_RATE 100          // Hz
#define BUFFER_SIZE 100
#define SMOOTH_WINDOW 5
#define MIN_PEAK_HEIGHT 200      // 最小峰谷高度差（ADC值）
#define MIN_PEAK_INTERVAL 40     // 最小波峰间隔样本数（对应最大心率 150 BPM）

MAX30105 particleSensor;

#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>

// ========== 用户设置 ==========
String ssid     = "";               // 将从配置读取，若没有则进入AP模式
String password = "";
String weatherLocation = "ip";      // 天气城市（可从配置读取，默认ip）

const long  gmtOffset_sec = 8 * 3600;
const int   daylightOffset_sec = 0;

Preferences preferences;          // 永久保存配置

#define WEATHER_API_KEY "SVOYTZFAJ5AyeZA7O"
#define WEATHER_UPDATE_INTERVAL 1800000      // 30分钟

// ========== 按钮引脚 ==========
#define BTN_LEFT    2
#define BTN_RIGHT   3
#define BTN_BACK    4
#define BTN_OK      5

// ========== 硬件接口 ==========
#define OLED_ADDR 0x3C
#define I2C_SDA   1
#define I2C_SCL   0

U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ========== GIF 资源（请替换为你的28个位图） ==========
const uint8_t bitmap_01[] = {};

const uint8_t bitmap_28[] = {};

// ========== 菜单图标 (32x32) ==========
const uint8_t icon_clock[128] = {};
const uint8_t icon_weather[128] = {};
const uint8_t icon_gif[128] = {};
const uint8_t icon_game[128] = {};
const uint8_t icon_health[128] = {};

// 更新图标指针数组（5个）
const uint8_t* menuIcons[5] = { icon_clock, icon_weather, icon_gif, icon_game, icon_health };

const unsigned char* bitmaps[28] = {
  bitmap_01, bitmap_02, bitmap_03, bitmap_04,
  bitmap_05, bitmap_06, bitmap_07, bitmap_08, bitmap_09,
  bitmap_10, bitmap_11, bitmap_12, bitmap_13, bitmap_14,
  bitmap_15, bitmap_16, bitmap_17, bitmap_18, bitmap_19,
  bitmap_20, bitmap_21, bitmap_22, bitmap_23, bitmap_24,
  bitmap_25, bitmap_26, bitmap_27, bitmap_28
};
const int numbitmaps = sizeof(bitmaps) / sizeof(bitmaps[0]);

uint8_t reverse_byte(uint8_t x) {
    x = ((x & 0x55) << 1) | ((x & 0xAA) >> 1);
    x = ((x & 0x33) << 2) | ((x & 0xCC) >> 2);
    x = ((x & 0x0F) << 4) | ((x & 0xF0) >> 4);
    return x;
}

// ========== 时钟动画参数 ==========
#define ANIM_DURATION 150
#define FONT_HEIGHT   24
#define Y_BASE        50
#define MOVE_UP       20
#define TOP_LIMIT     24

const int digitX[6] = { 0, 18, 44, 62, 88, 106 };
const int colonX[2] = { 36, 80 };

int targetDigits[6] = {0};
bool animating[6] = {false};
int oldDigits[6] = {0};
float animProgress[6] = {0.0};
unsigned long animStart[6] = {0};

// ========== 天气数据 ==========
String weatherCity = "未知";
String weatherDesc = "";
String weatherTemp = "";
bool weatherValid = false;
unsigned long lastWeatherUpdate = 0;

// ========== 健康数据 ==========
int heartRate = 0;            // 心率
int spo2 = 0;                 // 血氧
bool sensorReady = false;     // MAX30102 是否正常工作
bool dataValid = false;       // 数据是否有效
unsigned long lastHealthRead = 0;
const int HEALTH_READ_MS = 10; // 读取间隔

int lastHeartRate = 0;
int lastSpO2 = 0;

// ========== 系统状态 ==========
enum AppMode { MENU_MODE, CLOCK_MODE, WEATHER_MODE, GIF_MODE, GAME_MODE, HEALTH_MODE };
AppMode currentMode = MENU_MODE;
int menuIndex = 0;                // 0:时钟, 1:天气, 2:动画, 3:游戏
unsigned long gifStartTime = 0;

// ========== 菜单动画 ==========
bool menuAnimating = false;
unsigned long menuAnimStart = 0;
int menuAnimDirection = 0;        // 1左(prev), -1右(next)
int menuOldIndex = 0;
int menuNewIndex = 0;
const int MENU_ANIM_DURATION = 500;

// ========== 恐龙游戏相关 ==========
enum GameScreen { GS_DIFFICULTY, GS_READY, GS_PLAYING };
GameScreen gameState = GS_DIFFICULTY;   // 初始为难度选择
bool gameOverFlag = false;
float dinoY = 0;
float dinoVel = 0;
bool dinoJumping = false;
const float GRAVITY = 0.6;
const float JUMP_VEL = -4.5;
const int GROUND_Y = 54;
const int DINO_X = 10;
const int DINO_W = 8;
const int DINO_H = 10;
int obstacleX = 128;
const int obstacleW = 5;
const int obstacleH = 10;
int obstacleSpeed = 2;                 // 新增，动态速度
const int OBSTACLE_BASE_SPEED = 2;     // 新增，基础速度
const int SPEED_DIV = 5;              // 新增，每5分速度+1
unsigned long lastGameUpdate = 0;
const int GAME_FRAME_MS = 20;

int score = 0;                        // 新增，积分
bool obstaclePassed = false;          // 新增，计分标记
int lastScore = 0;   // 用于死亡界面显示，不受清空影响
int difficulty = 1;   // 难度 1~5
bool gameModeJustEntered = false;   // 标记是否刚进入游戏，用于防连击

// ========== 启动日志 ==========
String logLines[4] = {"", "", "", ""};
int lineCount = 0;
bool startupDone = false;

void addLogLine(const char* line) {
    if (startupDone) return;
    const int baseY[4] = {12, 26, 40, 54};
    if (lineCount == 4) {
        const int animSteps = 8, stepDelay = 25, rowHeight = 14;
        for (int k = 0; k <= animSteps; k++) {
            int shift = (k * rowHeight) / animSteps;
            u8g2.firstPage();
            do {
                u8g2.setFont(u8g2_font_wqy12_t_gb2312);
                for (int i = 0; i < 4; i++) {
                    int y = baseY[i] - shift;
                    if (y > 0) u8g2.drawUTF8(0, y, logLines[i].c_str());
                }
            } while (u8g2.nextPage());
            delay(stepDelay);
        }
        logLines[0] = logLines[1]; logLines[1] = logLines[2]; logLines[2] = logLines[3]; logLines[3] = "";
        lineCount = 3;
    }
    logLines[lineCount++] = String(line);
    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_wqy12_t_gb2312);
        for (int i = 0; i < lineCount; i++) u8g2.drawUTF8(0, 12 + i*14, logLines[i].c_str());
    } while (u8g2.nextPage());
}

void showStatus(const char* msg) { addLogLine(msg); }
void showMessage(const char* a, const char* b, const char* c) {
    if(a) addLogLine(a); if(b) addLogLine(b); if(c) addLogLine(c);
}

// ========== WiFi 状态机 ==========
volatile bool wifiDisconnected = false;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 5000;

void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("WiFi 断开");
            wifiDisconnected = true;
            break;
        default: break;
    }
}

bool tryConnectStoredWiFi() {
    // 读取偏好设置
    preferences.begin("wificonfig", false);
    ssid = preferences.getString("ssid", "");
    password = preferences.getString("password", "");
    weatherLocation = preferences.getString("city", "ip");
    preferences.end();

    if (ssid.length() == 0) {
        addLogLine("无已保存WiFi 进入配置");
        return false;
    }

    addLogLine(("尝试连接: " + ssid).c_str());
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.setAutoReconnect(false);
    WiFi.persistent(false);
    WiFi.onEvent(WiFiEvent);

    WiFi.begin(ssid.c_str(), password.c_str());
    int attempt = 0;
    while (WiFi.status() != WL_CONNECTED && attempt < 40) {
        delay(500); attempt++;
        if (attempt % 5 == 0) {
            char buf[32]; snprintf(buf, 32, "连接中 %d/40", attempt);
            addLogLine(buf);
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        addLogLine("WiFi连接成功");
        wifiDisconnected = false;
        return true;
    } else {
        addLogLine("连接失败 进入配置");
        return false;
    }
}

void startAPConfigMode() {
    addLogLine("启动配置热点");
    delay(500);

    // OLED 提示
    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_wqy12_t_gb2312);
        u8g2.drawUTF8(0, 12, "请连接手机热点:");
        u8g2.drawUTF8(0, 30, "ESP32_Config");       // 简化命名
        u8g2.drawUTF8(0, 48, "访问任意网页设置");
    } while (u8g2.nextPage());

    // 强制切换到 AP 模式（先关闭 STA）
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(200);
    WiFi.mode(WIFI_AP);

    // 尝试启动 AP
    bool apStarted = false;
    for (int i = 0; i < 3; i++) {
        if (WiFi.softAP("ESP32_Config")) {          // 无密码
            apStarted = true;
            break;
        }
        delay(500);
    }

    if (!apStarted) {
        // 启动失败则在 OLED 上显示错误信息，死循环
        u8g2.firstPage();
        do {
            u8g2.setFont(u8g2_font_wqy12_t_gb2312);
            u8g2.drawUTF8(0, 20, "热点创建失败");
            u8g2.drawUTF8(0, 40, "请复位重试");
        } while (u8g2.nextPage());
        while (1) { delay(1000); }
    }

    IPAddress ip = WiFi.softAPIP();
    Serial.print("AP IP: ");
    Serial.println(ip);

    // OLED 显示 IP 地址（便于手机连接）
    char ipStr[16];
    sprintf(ipStr, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_wqy12_t_gb2312);
        u8g2.drawUTF8(0, 12, "热点: ESP32_Config");
        u8g2.drawUTF8(0, 30, ("IP: " + String(ipStr)).c_str());
        u8g2.drawUTF8(0, 48, "打开浏览器输入任意地址");
    } while (u8g2.nextPage());

    // DNS 劫持
    DNSServer dnsServer;
    dnsServer.start(53, "*", ip);

    WebServer server(80);
    server.on("/", HTTP_GET, [&]() {
        String html = "<html><body><h2>WiFi设置</h2><form action='/save' method='post'>";
        html += "WiFi名称: <input name='ssid'><br>";
        html += "密码: <input name='pw' type='password'><br>";
        html += "天气城市(例如ip或beijing): <input name='city' value='" + weatherLocation + "'><br>";
        html += "<input type='submit' value='保存并重启'>";
        html += "</form></body></html>";
        server.send(200, "text/html; charset=utf-8", html);
    });

    server.on("/save", HTTP_POST, [&]() {
        String newSSID = server.arg("ssid");
        String newPW = server.arg("pw");
        String newCity = server.arg("city");
        if (newCity.length() == 0) newCity = "ip";

        preferences.begin("wificonfig", false);
        preferences.putString("ssid", newSSID);
        preferences.putString("password", newPW);
        preferences.putString("city", newCity);
        preferences.end();

        server.send(200, "text/html; charset=utf-8",
                    "<html><body>保存成功，设备即将重启... </body></html>");
        delay(1000);
        ESP.restart();
    });

    server.begin();
    addLogLine("配置服务器已启动");

    while (true) {
        dnsServer.processNextRequest();
        server.handleClient();
        delay(10);
    }
}

bool connectWiFi() {
    addLogLine("扫描WiFi...");
    int n = WiFi.scanNetworks();
    if (n == 0) { showMessage("未发现任何WiFi", "请检查天线", "长按复位重试"); return false; }

    char buf[32];
    snprintf(buf, sizeof(buf), "发现 %d 个WiFi", n);
    addLogLine(buf);

    bool found = false;
    for (int i = 0; i < n; ++i) if (WiFi.SSID(i) == ssid) found = true;
    if (!found) { showMessage("未找到目标WiFi", ssid.c_str(), "请检查配置"); return false; }

    addLogLine("找到目标WiFi");
    addLogLine("正在连接...");

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.setAutoReconnect(false);
    WiFi.persistent(false);
    WiFi.onEvent(WiFiEvent);

    WiFi.begin(ssid.c_str(), password.c_str());
    int attempt = 0;
    while (WiFi.status() != WL_CONNECTED && attempt < 60) {
        delay(500); attempt++;
        if (attempt % 5 == 0) {
            char msg[32]; snprintf(msg, sizeof(msg), "连接中 %d/60", attempt);
            addLogLine(msg);
        }
        if (WiFi.status() == WL_CONNECT_FAILED) {
            addLogLine("密码错误或拒绝连接");
            break;
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        addLogLine("WiFi连接成功");
        wifiDisconnected = false;
        return true;
    } else {
        showMessage("WiFi连接失败", "请检查密码/距离", "长按复位重试");
        return false;
    }
}

void handleWiFiReconnect() {
    if (wifiDisconnected && (millis() - lastReconnectAttempt > RECONNECT_INTERVAL)) {
        lastReconnectAttempt = millis();
        Serial.println("尝试重连 WiFi...");
        WiFi.begin(ssid.c_str(), password.c_str());
    }
    if (WiFi.status() == WL_CONNECTED) {
        wifiDisconnected = false;
    }
}

bool syncTime() {
    struct tm timeinfo;
    addLogLine("同步时间(域名)");
    configTime(gmtOffset_sec, daylightOffset_sec, "ntp.aliyun.com", "cn.pool.ntp.org", "time.nist.gov");
    int retry = 0;
    while (retry < 30) {
        if (getLocalTime(&timeinfo, 500)) { addLogLine("时间同步成功"); return true; }
        delay(1000); retry++;
        if (retry % 5 == 0) { char buf[32]; snprintf(buf, 32, "域名重试 %d/30", retry); addLogLine(buf); }
    }
    addLogLine("域名失败");
    addLogLine("尝试IP直连...");
    configTime(gmtOffset_sec, daylightOffset_sec, "203.107.6.88");
    retry = 0;
    while (retry < 30) {
        if (getLocalTime(&timeinfo, 500)) { addLogLine("IP直连成功"); return true; }
        delay(1000); retry++;
        if (retry % 5 == 0) { char buf[32]; snprintf(buf, 32, "IP重试 %d/30", retry); addLogLine(buf); }
    }
    showMessage("时间同步失败", "检查防火墙", "UDP 123端口");
    return false;
}

bool initMAX30102() {
    Wire.begin(1, 0);
    if (!particleSensor.begin(Wire)) {
        Serial.println("MAX30102 not found");
        return false;
    }
    // LED电流：0x1F (6.4mA) ~ 0x3F (12.5mA)，选 0x2F (约10mA) 较均衡
    // 平均次数：1 (不平均，保留细节)，模式：Red+IR，采样率100Hz，脉宽411us，量程4096
    particleSensor.setup(0x2F, 1, 2, 1, 411, 4096);
    sensorReady = true;
    return true;
}

#define MAX30102_ADDR  0x57
#define REG_FIFO_DATA  0x07
#define REG_FIFO_WR_PTR 0x04
#define REG_FIFO_RD_PTR 0x06

uint8_t readReg(uint8_t reg) {
    Wire.beginTransmission(MAX30102_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(MAX30102_ADDR, (uint8_t)1);
    if (Wire.available()) return Wire.read();
    return 0;
}

void readFIFOSample(uint32_t &ir, uint32_t &red) {
    Wire.beginTransmission(MAX30102_ADDR);
    Wire.write(REG_FIFO_DATA);
    Wire.endTransmission(false);
    Wire.requestFrom(MAX30102_ADDR, (uint8_t)6);
    if (Wire.available() >= 6) {
        uint8_t temp[6];
        for (int i = 0; i < 6; i++) temp[i] = Wire.read();
        ir  = ((uint32_t)(temp[3] & 0x03) << 16) | ((uint32_t)temp[4] << 8) | temp[5];
        red = ((uint32_t)(temp[0] & 0x03) << 16) | ((uint32_t)temp[1] << 8) | temp[2];
    }
}

bool updateWeather() {
    if (WiFi.status() != WL_CONNECTED) {
        weatherCity = "网络断开";
        weatherDesc = "";
        weatherTemp = "";
        weatherValid = false;
        return false;
    }

    HTTPClient http;
    String url = "http://api.seniverse.com/v3/weather/now.json?key=";
    url += WEATHER_API_KEY;
    url += "&location=";
    url += weatherLocation;               // 改用变量
    url += "&language=zh-Hans&unit=c";

    http.begin(url);
    int httpCode = http.GET();
    if (httpCode != 200) {
        http.end();
        weatherCity = "服务异常";
        weatherDesc = "";
        weatherTemp = "";
        weatherValid = false;
        return false;
    }

    String payload = http.getString();
    http.end();

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        weatherCity = "解析错误";
        weatherDesc = "";
        weatherTemp = "";
        weatherValid = false;
        return false;
    }

    JsonObject loc = doc["results"][0]["location"];
    weatherCity = loc["name"].as<String>();
    JsonObject now = doc["results"][0]["now"];
    weatherDesc = now["text"].as<String>();
    weatherTemp = now["temperature"].as<String>();

    weatherValid = true;
    return true;
}

// ========== 按钮处理 ==========
bool btnLeft = false, btnRight = false, btnBack = false, btnOk = false;

void readButtons() {
    static unsigned long lastDebounce = 0;
    if (millis() - lastDebounce < 50) return;
    lastDebounce = millis();

    btnLeft  = (digitalRead(BTN_LEFT)  == LOW);
    btnRight = (digitalRead(BTN_RIGHT) == LOW);
    btnBack  = (digitalRead(BTN_BACK)  == LOW);
    btnOk    = (digitalRead(BTN_OK)    == LOW);
}

void resetGame() {
    gameState = GS_READY;
    gameOverFlag = false;
    dinoY = 0;
    dinoVel = 0;
    dinoJumping = false;
    obstacleX = 128;
    obstacleSpeed = OBSTACLE_BASE_SPEED;
    score = 0;
    obstaclePassed = false;
}

void handleNavigation() {
    if (!startupDone) return;

    if (currentMode == MENU_MODE) {
        if (!menuAnimating) {
            if (btnLeft) {
                menuOldIndex = menuIndex;
                menuNewIndex = (menuIndex - 1 + 5) % 5;
                menuAnimDirection = 1;
                menuAnimating = true;
                menuAnimStart = millis();
                delay(250);
            }
            if (btnRight) {
                menuOldIndex = menuIndex;
                menuNewIndex = (menuIndex + 1) % 5;
                menuAnimDirection = -1;
                menuAnimating = true;
                menuAnimStart = millis();
                delay(250);
            }
            if (btnOk) {
                switch (menuIndex) {
                    case 0: currentMode = CLOCK_MODE; break;
                    case 1: currentMode = WEATHER_MODE; break;
                    case 2:
                        currentMode = GIF_MODE;
                        gifStartTime = millis();
                        break;
                    case 3:
                        currentMode = GAME_MODE;
                        gameState = GS_DIFFICULTY;
                        difficulty = 1;
                        gameModeJustEntered = true;    // ★ 标记刚进入
                        delay(250);
                        break;
                    case 4:
                        currentMode = HEALTH_MODE;
                        break;
                }
                delay(250);
            }
        }
    } else if (currentMode == GAME_MODE) {
        // 游戏模式下，返回键回到菜单
        if (btnBack) {
            currentMode = MENU_MODE;
            menuAnimating = false;
            delay(250);
            return;
        }
        // 游戏控制由 updateGame 处理
    } else {
        // 其他模式下返回键
        if (btnBack) {
            currentMode = MENU_MODE;
            menuAnimating = false;
            delay(250);
        }
    }
}

// ========== 界面绘制 ==========
void drawMenu() {
    const int iconSize = 32;
    const int gap = 6;
    const int itemWidth = iconSize + gap;
    const int centerX = (128 - iconSize) / 2;
    const int baseY = (64 - iconSize) / 2 + 2;   // 已下移2像素

    // 计算滚动偏移（像素）
    float scrollPos;
    if (menuAnimating) {
        float t = (millis() - menuAnimStart) / (float)MENU_ANIM_DURATION;
        if (t > 1.0) t = 1.0;
        float oldTarget = (float)menuOldIndex * itemWidth;
        float newTarget = (float)menuNewIndex * itemWidth;
        scrollPos = oldTarget + (newTarget - oldTarget) * t;
    } else {
        scrollPos = (float)menuIndex * itemWidth;
    }

    u8g2.firstPage();
    do {
        // 绘制所有 5 个图标（循环水平布局）
        for (int i = 0; i < 5; i++) {
            int x = centerX - (int)scrollPos + i * itemWidth;
            if (x > -iconSize && x < 128) {
                uint8_t reversed[128];
                for (int j = 0; j < 128; j++) {
                    reversed[j] = reverse_byte(menuIcons[i][j]);
                }
                u8g2.drawXBMP(x, baseY, iconSize, iconSize, reversed);
            }
        }
        // 选中框固定在中央
        u8g2.drawFrame(centerX - 2, baseY - 2, iconSize + 4, iconSize + 4);
    } while (u8g2.nextPage());
}

void drawClock() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;

    int year = timeinfo.tm_year + 1900;
    int mon  = timeinfo.tm_mon + 1;
    int day  = timeinfo.tm_mday;
    int wday = timeinfo.tm_wday;
    const char* weekDays[] = {"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};
    const char* weekStr = weekDays[wday];
    char dateStr[20];
    snprintf(dateStr, sizeof(dateStr), "%04d/%02d/%02d", year, mon, day);

    char statusChar = (WiFi.status() == WL_CONNECTED) ? ' ' : '!';

    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_wqy12_t_gb2312);
        u8g2.drawUTF8(0, 12, dateStr);
        if (statusChar != ' ') {
            char indicator[3] = {statusChar, ' ', '\0'};
            u8g2.drawUTF8(72, 12, indicator);
            u8g2.drawUTF8(84, 12, weekStr);
        } else {
            u8g2.drawUTF8(72, 12, weekStr);
        }

        u8g2.setFont(u8g2_font_logisoso24_tr);
        u8g2.setCursor(colonX[0], Y_BASE);
        u8g2.print(":");
        u8g2.setCursor(colonX[1], Y_BASE);
        u8g2.print(":");

        for (int i = 0; i < 6; i++) {
            int x = digitX[i];
            if (animating[i]) {
                float t = animProgress[i];
                int yOld = Y_BASE - (int)(t * MOVE_UP);
                int yNew = Y_BASE + MOVE_UP - (int)(t * MOVE_UP);
                if (yOld - FONT_HEIGHT >= TOP_LIMIT) {
                    u8g2.setCursor(x, yOld);
                    u8g2.print((char)('0' + oldDigits[i]));
                }
                u8g2.setCursor(x, yNew);
                u8g2.print((char)('0' + targetDigits[i]));
            } else {
                u8g2.setCursor(x, Y_BASE);
                u8g2.print((char)('0' + targetDigits[i]));
            }
        }
    } while (u8g2.nextPage());
}

void drawWeather() {
    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_wqy12_t_gb2312);
        u8g2.drawUTF8((128 - u8g2.getUTF8Width(weatherCity.c_str())) / 2, 15, weatherCity.c_str());

        if (weatherValid) {
            u8g2.drawUTF8((128 - u8g2.getUTF8Width(weatherDesc.c_str())) / 2, 33, weatherDesc.c_str());
            String tempStr = weatherTemp + "°C";
            u8g2.drawUTF8((128 - u8g2.getUTF8Width(tempStr.c_str())) / 2, 53, tempStr.c_str());
        } else {
            u8g2.drawUTF8(0, 35, "天气数据暂不可用");
        }
    } while (u8g2.nextPage());
}

void drawGIF() {
    unsigned long elapsed = millis() - gifStartTime;
    int idx = (elapsed / 70) % numbitmaps;

    uint8_t reversed[1024];
    const uint8_t* src = bitmaps[idx];
    for (int i = 0; i < 1024; i++) {
        reversed[i] = reverse_byte(src[i]);
    }

    u8g2.firstPage();
    do {
        u8g2.drawXBMP(0, 6, 128, 64, reversed);
    } while (u8g2.nextPage());
}

// ========== 游戏逻辑 ==========
void updateGame() {
    if (currentMode != GAME_MODE) return;

    // 难度选择
    if (gameState == GS_DIFFICULTY) {
        // 防止进入游戏时直接跳过难度选择
        if (gameModeJustEntered) {
            if (btnOk) {
                return;                     // 确认键还按着，等待释放
            } else {
                gameModeJustEntered = false; // 确认键已释放，允许后续操作
            }
        }
        if (btnLeft) {
            difficulty = (difficulty == 1) ? 5 : difficulty - 1;
            delay(200);
        }
        if (btnRight) {
            difficulty = (difficulty == 5) ? 1 : difficulty + 1;
            delay(200);
        }
        if (btnOk) {
            gameState = GS_READY;
            gameOverFlag = false;
            lastScore = 0;
            delay(200);
        }
        if (btnBack) {
            currentMode = MENU_MODE;
            menuAnimating = false;
            delay(200);
        }
        return;
    }

    // 准备开始
    if (gameState == GS_READY) {
        if (btnOk) {
            gameState = GS_PLAYING;
            gameOverFlag = false;
            score = 0;
            lastScore = 0;
            dinoY = 0;
            dinoVel = 0;
            dinoJumping = false;
            obstacleX = 128;
            obstacleSpeed = OBSTACLE_BASE_SPEED;
            obstaclePassed = false;
            delay(200);
        }
        if (btnBack) {
            currentMode = MENU_MODE;
            menuAnimating = false;
            delay(200);
        }
        return;
    }

    // 游戏进行中
    if (btnOk && !dinoJumping) {
        dinoVel = JUMP_VEL;
        dinoJumping = true;
    }

    if (millis() - lastGameUpdate >= GAME_FRAME_MS) {
        lastGameUpdate = millis();

        dinoY += dinoVel;
        dinoVel += GRAVITY;
        if (dinoY > 0) {
            dinoY = 0;
            dinoVel = 0;
            dinoJumping = false;
        }

        obstacleX -= obstacleSpeed;
        if (obstacleX < -obstacleW) {
            obstacleX = 128 + random(20, 80);
            obstacleSpeed = OBSTACLE_BASE_SPEED + score / (6 - difficulty);
            obstaclePassed = false;
        }

        if (!obstaclePassed && (obstacleX + obstacleW < DINO_X)) {
            score++;
            obstaclePassed = true;
        }

        int dinoTop = GROUND_Y + (int)dinoY - DINO_H;
        int dinoBottom = GROUND_Y + (int)dinoY;
        int dinoLeft = DINO_X;
        int dinoRight = DINO_X + DINO_W;

        int obsTop = GROUND_Y - obstacleH;
        int obsBottom = GROUND_Y;
        int obsLeft = obstacleX;
        int obsRight = obstacleX + obstacleW;

        if (dinoRight > obsLeft && dinoLeft < obsRight &&
            dinoBottom > obsTop && dinoTop < obsBottom) {
            gameState = GS_READY;
            gameOverFlag = true;
            lastScore = score;
            score = 0;
            obstacleX = 128;
            dinoY = 0;
            dinoVel = 0;
            dinoJumping = false;
        }
    }
}

void drawGame() {
    u8g2.firstPage();
    do {
        if (gameState == GS_DIFFICULTY) {
            // 难度选择界面
            u8g2.setFont(u8g2_font_wqy12_t_gb2312);
            u8g2.drawUTF8((128 - u8g2.getUTF8Width("恐龙小游戏")) / 2, 15, "恐龙小游戏");
            u8g2.drawUTF8((128 - u8g2.getUTF8Width("选择难度")) / 2, 33, "选择难度");
            String diffStr = "<" + String(difficulty) + ">";
            u8g2.drawUTF8((128 - u8g2.getUTF8Width(diffStr.c_str())) / 2, 51, diffStr.c_str());
        } else {
            // 游戏画面（READY 或 PLAYING）
            u8g2.drawHLine(0, GROUND_Y, 128);
            u8g2.drawBox(obstacleX, GROUND_Y - obstacleH, obstacleW, obstacleH);
            u8g2.drawBox(DINO_X, GROUND_Y + (int)dinoY - DINO_H, DINO_W, DINO_H);

            // 右上角分数
            u8g2.setFont(u8g2_font_6x10_tr);
            String scoreStr = String(score);
            int scoreWidth = u8g2.getStrWidth(scoreStr.c_str());
            u8g2.setCursor(128 - scoreWidth - 2, 10);
            u8g2.print(scoreStr);

            if (gameState == GS_READY) {
                u8g2.setFont(u8g2_font_wqy12_t_gb2312);
                if (gameOverFlag) {
                    u8g2.drawUTF8(10, 15, "游戏结束！按确认重来");
                    String finalScore = "得分: " + String(lastScore);
                    int w = u8g2.getUTF8Width(finalScore.c_str());
                    u8g2.drawUTF8((128 - w) / 2, 35, finalScore.c_str());
                } else {
                    u8g2.drawUTF8(10, 15, "按下确认键开始");
                }
            }
        }
    } while (u8g2.nextPage());
}

void updateHealth() {
    if (!sensorReady) return;
    if (millis() - lastHealthRead < 10) return;
    lastHealthRead = millis();

    // ──────── 1. 读取 FIFO ────────
    uint8_t wr = readReg(REG_FIFO_WR_PTR);
    uint8_t rd = readReg(REG_FIFO_RD_PTR);
    int samples = (wr - rd) & 0x1F;
    if (samples == 0) return;

    static uint32_t irBuf[100];
    static uint32_t redBuf[100];          // ⬅️ 补上红光缓冲区
    static int bufIdx = 0;
    static uint32_t sampleCounter = 0;
    static uint32_t lastPeakTime = 0;
    static float bpm = 0;

    for (int i = 0; i < samples; i++) {
        uint32_t ir, red;
        readFIFOSample(ir, red);
        if (ir < FINGER_DETECT_THRESHOLD) continue;

        irBuf[bufIdx] = ir;
        redBuf[bufIdx] = red;             // ⬅️ 同时保存红光数据
        bufIdx++;

        if (bufIdx >= 100) {
            bufIdx = 0;

            // 2. 3点移动平均（仅对红外）
            float smooth[100];
            for (int j = 0; j < 100; j++) {
                long sum = 0;
                int cnt = 0;
                for (int k = j-1; k <= j+1; k++) {
                    if (k >= 0 && k < 100) { sum += irBuf[k]; cnt++; }
                }
                smooth[j] = (float)sum / cnt;
            }

            // 3. 去直流
            float dc = 0;
            for (int j = 0; j < 100; j++) dc += smooth[j];
            dc /= 100;
            float ac[100];
            for (int j = 0; j < 100; j++) ac[j] = smooth[j] - dc;

            // 4. 动态阈值
            float minAc = ac[0], maxAc = ac[0];
            for (int j = 1; j < 100; j++) {
                if (ac[j] < minAc) minAc = ac[j];
                if (ac[j] > maxAc) maxAc = ac[j];
            }
            float acRange = maxAc - minAc;
            float threshold = acRange * 0.2f;
            if (threshold < 10) threshold = 10;

            // 5. 实时寻峰（仅红外）
            bool rising = false;
            float baseline = ac[0];

            for (int j = 1; j < 99; j++) {
                float prev = ac[j-1], curr = ac[j], next = ac[j+1];

                if (!rising && curr > prev && curr > baseline + threshold) {
                    rising = true;
                }
                if (rising && curr > next && curr > prev) {
                    float height = curr - baseline;
                    if (height > threshold) {
                        uint32_t peakTime = sampleCounter + j;
                        if (lastPeakTime > 0) {
                            uint32_t interval = peakTime - lastPeakTime;
                            if (interval >= 40 && interval <= 300) {
                                float instantBPM = 6000.0f / interval;
                                if (bpm == 0) bpm = instantBPM;
                                else bpm = bpm * 0.7f + instantBPM * 0.3f;
                                lastHeartRate = (int)(bpm + 0.5f);
                                dataValid = true;
                            }
                        }
                        lastPeakTime = peakTime;
                        baseline = curr;
                    }
                    rising = false;
                }
                if (!rising && curr < baseline) {
                    baseline = curr;
                }
            }

            sampleCounter += 100;

            // 6. 血氧（使用原始算法，现在 redBuf 可用）
            int32_t spo2Val, hrVal;
            int8_t spo2Valid, hrValid;
            maxim_heart_rate_and_oxygen_saturation(irBuf, 100, redBuf,
                                                   &spo2Val, &spo2Valid,
                                                   &hrVal, &hrValid);
            if (spo2Valid) lastSpO2 = spo2Val;

            // 调试输出
            Serial.printf("Range=%d thr=%d BPM=%d SPO2=%d\n",
                          (int)acRange, (int)threshold, lastHeartRate, lastSpO2);
        }
    }
}

void drawHealth() {
    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_wqy12_t_gb2312);

        if (dataValid) {
            String hrStr = "心率: " + String(lastHeartRate);
            String spStr = "血氧: " + String(lastSpO2) + "%";
            u8g2.drawUTF8(0, 15, hrStr.c_str());
            u8g2.drawUTF8(0, 33, spStr.c_str());
        } else {
            u8g2.drawUTF8(0, 15, "心率: --");
            u8g2.drawUTF8(0, 33, "血氧: --");
        }
        u8g2.drawUTF8(0, 51, "按下确认查看心电图");
    } while (u8g2.nextPage());
}

// ========== 初始化 ==========
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\nESP32-C3 多功能摆件启动");

    pinMode(BTN_LEFT, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    pinMode(BTN_BACK, INPUT_PULLUP);
    pinMode(BTN_OK, INPUT_PULLUP);

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);
    if (!u8g2.begin()) while (1);
    u8g2.enableUTF8Print();
    u8g2.sendF("c", 0xC8);

    addLogLine("系统初始化完成");
    delay(300);

    // WiFi 连接（配置或已保存）
    if (!tryConnectStoredWiFi()) {
        startAPConfigMode();   // 不返回，配置完自动重启
    }

    // 时间同步
    if (!syncTime()) { startupDone = true; while (1); }

    addLogLine("时钟启动成功！");
    delay(300);

    if (!initMAX30102()) {
        addLogLine("健康功能不可用");
    }

    updateWeather();
    lastWeatherUpdate = millis();

    startupDone = true;

    struct tm t;
    if (getLocalTime(&t)) {
        targetDigits[0] = t.tm_hour/10; targetDigits[1] = t.tm_hour%10;
        targetDigits[2] = t.tm_min/10;  targetDigits[3] = t.tm_min%10;
        targetDigits[4] = t.tm_sec/10;  targetDigits[5] = t.tm_sec%10;
    }
}

// ========== 主循环 ==========
void loop() {
    if (!startupDone) return;

    readButtons();
    handleNavigation();

    // 后台任务：WiFi重连、时间、天气
    handleWiFiReconnect();
    static unsigned long lastTimeSync = 0;
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
        // 更新时钟动画数字
        int hour = timeinfo.tm_hour;
        int min  = timeinfo.tm_min;
        int sec  = timeinfo.tm_sec;
        int newDigits[6] = { hour/10, hour%10, min/10, min%10, sec/10, sec%10 };

        for (int i = 0; i < 6; i++) {
            if (newDigits[i] != targetDigits[i]) {
                if (animating[i]) {
                    float prog = animProgress[i];
                    int visible = (prog < 0.5f) ? oldDigits[i] : targetDigits[i];
                    oldDigits[i] = visible;
                    animStart[i] = millis();
                    animProgress[i] = 0.0f;
                } else {
                    oldDigits[i] = targetDigits[i];
                    animating[i] = true;
                    animStart[i] = millis();
                    animProgress[i] = 0.0f;
                }
                targetDigits[i] = newDigits[i];
            }
        }

        if (millis() - lastWeatherUpdate > WEATHER_UPDATE_INTERVAL) {
            updateWeather();
            lastWeatherUpdate = millis();
        }
    }

    // 动画进度更新
    unsigned long now = millis();
    for (int i = 0; i < 6; i++) {
        if (animating[i]) {
            animProgress[i] = (float)(now - animStart[i]) / ANIM_DURATION;
            if (animProgress[i] >= 1.0f) {
                animProgress[i] = 1.0f;
                animating[i] = false;
            }
        }
    }

    // 菜单动画结束处理
    if (menuAnimating) {
        if (millis() - menuAnimStart >= MENU_ANIM_DURATION) {
            menuAnimating = false;
            menuIndex = menuNewIndex;
        }
    }

    // 游戏更新
    updateGame();

    // 健康数据更新（始终运行，以便后台采集，即使不在健康界面）
    updateHealth();

    // 绘制当前界面
    switch (currentMode) {
        case MENU_MODE:    drawMenu();    break;
        case CLOCK_MODE:   drawClock();   break;
        case WEATHER_MODE: drawWeather(); break;
        case GIF_MODE:     drawGIF();     break;
        case GAME_MODE:    drawGame();    break;
        case HEALTH_MODE:   drawHealth(); break;
    }

    delay(10);
}