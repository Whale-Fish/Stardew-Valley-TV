/*
 * sketch_rle.ino — 星露谷小电视（RLE 帧版本）
 * =============================================================
 * 在 sketch_may24a.ino 基础上只换了两处：
 *   1. drawBin   → drawRleBin   （解压 RLE 帧并显示）
 *   2. playAnimation 调用 drawRLEBin
 *
 * 数据格式由 convert_rle.py 生成：
 *   头部 8 字节 magic + 2 字节宽度 + 2 字节高度 + 逐行 RLE run 流
 *
 * 其余（菜单 / WiFi / 和风天气 API 映射 / luck）保持不变。
 *
 * 烧录顺序：
 *   1. 用 convert_rle.py 把 PNG 压到 data/ 下
 *   2. Arduino IDE → 工具 → ESP32 Sketch Data Upload
 *   3. 本 .ino 按普通方式上传
 */
#include <Arduino.h>
#include <time.h>   // NTP 时间：占卜按"天"抽结果要用
#include <SPI.h>
#include <SPIFFS.h>
#include <FS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <ArduinoJson.h>
#include <zlib_turbo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <U8g2_for_Adafruit_GFX.h>   // 中文字体（库管理器搜 U8g2_for_Adafruit_GFX 安装）

// ====== WiFi 配置 ======
const char* WIFI_SSID = "";     // ESP32 不支持 5G，用 2.4G SSID
const char* WIFI_PASS = "";

// ====== 和风天气 API（与 tv.py 的 get_weather_in_China() 一致） ======
const char* QWEATHER_HOST = "";
const char* QWEATHER_KEY  = "";
const char* QWEATHER_LOC  = "";   // 杭州

// ====== 引脚 ======
#define TFT_CS   5
#define TFT_DC   16
#define TFT_RST  4
#define BTN1     14
#define BTN2     12

#define DISPLAY_W  240
#define DISPLAY_H  240

// 木头色边框（RGB 176,122,53，暖棕，嫌浅/深可调这里的值）
#define WOOD_COLOR 0xB3C6

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2_for_adafruit_gfx;   // u8g2 渲染引擎（画中文用）

// ====== 模式 ======
enum Mode { MODE_MENU, MODE_WEATHER, MODE_LUCK };
Mode currentMode = MODE_MENU;
Mode lastMode    = MODE_MENU;

// 按键"按下"事件标志（边沿检测置位，各模式消费后清零；长按只触发一次）
bool btn1Pressed = false;
bool btn2Pressed = false;

// ====== 天气播放状态 ======
zlib_turbo zt;    // gzip 解压器（内部 ~7KB 状态，必须全局，不能放函数里）
const char* weatherFolder     = "/weather/sunny";
#define WEATHER_REFRESH_MS    (30UL * 60UL * 1000UL)
uint32_t lastWeatherFetch     = 0;
int      fetchFailCount      = 0;

// ====== 占卜状态 ======
#define LUCK_BIRTHDAY 19980903UL   // 生日 YYYYMMDD：掺入种子，换生日换结果
const char* luckFolder = "/luck/star";   // 当前抽中的占卜结果目录
// 五种结果及权重：star=5, gold=4, fairy=3, bat=2, skull=1
const char* luckFolders[5] = { "/luck/star", "/luck/gold", "/luck/fairy", "/luck/bat", "/luck/skull" };
const int   luckWeights[5] = { 5, 4, 3, 2, 1 };

// 按权重抽一个结果（权重和 15）：
// 种子 = 日历日 + 生日 → 同一天内每次进入结果都相同（跨天变化、重启稳定）
// 依赖 NTP 时间同步（setup 里 configTime）；未同步时退回开机天数，保证结果不漂移
// 不能用 randomSeed()：ESP32 的 random() 走硬件真随机，种子无效
const char* pickLuckFolder() {
    uint32_t seed;
    time_t now = time(nullptr);
    if (now > 100000000) {                        // 时间已同步（1973 年后；未同步时接近 1970）
        struct tm* t = localtime(&now);
        seed = (uint32_t)(t->tm_year * 372 + t->tm_mon * 31 + t->tm_mday);
    } else {
        seed = (uint32_t)(millis() / 86400000UL);  // 兜底：开机以来的天数
    }

    seed ^= LUCK_BIRTHDAY;                        // 掺入生日，不同生日结果不同
    seed ^= seed >> 13;                           // 混淆，均匀分布到 5 档
    seed *= 0x5bd1e995;
    seed ^= seed >> 15;

    uint32_t r = seed % 15;
    int acc = 0;
    for (int i = 0; i < 5; i++) {
        acc += luckWeights[i];
        if (r < acc) return luckFolders[i];
    }
    return luckFolders[0];
}

//==========================================================
// 调试用：列出 SPIFFS 全部文件
//==========================================================
void listFiles() {
    File root = SPIFFS.open("/");
    if (!root) return;
    File file = root.openNextFile();
    while (file) {
        Serial.print("  ");
        Serial.print(file.name());
        Serial.print("  SIZE: ");
        Serial.println(file.size());
        file = root.openNextFile();
    }
}

//==========================================================
// 菜单
//==========================================================
void drawMenu() {
    tft.fillScreen(ST77XX_BLACK);

    // 屏幕四周 5px 木头色边框（5 层递进 drawRect 叠出 5px 厚度）
    for (int b = 0; b < 5; b++) {
        tft.drawRect(b, b, DISPLAY_W - 2 * b, DISPLAY_H - 2 * b, WOOD_COLOR);
    }

    // 中文字体：文泉驿微米黑 16px（u8g2 中文点阵的最大档，只能这么大）
    u8g2_for_adafruit_gfx.setFontMode(1);            // 1 = 文字透明背景
    u8g2_for_adafruit_gfx.setFontDirection(0);
    u8g2_for_adafruit_gfx.setForegroundColor(ST77XX_WHITE);
    u8g2_for_adafruit_gfx.setBackgroundColor(ST77XX_BLACK);
    u8g2_for_adafruit_gfx.setFont(u8g2_font_wqy16_t_gb2312);

    // 水平居中后左移 20px
    int x;

    x = (DISPLAY_W - u8g2_for_adafruit_gfx.getUTF8Width("选择频道:")) / 2 - 20;
    u8g2_for_adafruit_gfx.setCursor(x, 52);
    u8g2_for_adafruit_gfx.print("选择频道:");

    x = (DISPLAY_W - u8g2_for_adafruit_gfx.getUTF8Width("天气预报")) / 2 - 20;
    u8g2_for_adafruit_gfx.setCursor(x, 124);
    u8g2_for_adafruit_gfx.print("天气预报");

    x = (DISPLAY_W - u8g2_for_adafruit_gfx.getUTF8Width("算命占卜")) / 2 - 20;
    u8g2_for_adafruit_gfx.setCursor(x, 172);
    u8g2_for_adafruit_gfx.print("算命占卜");
}

//==========================================================
// 简单一行提示（获取天气时 / 文件损毁时显示）
//==========================================================
void showMessage(const char* msg) {
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(20, 110);
    tft.println(msg);
}

//==========================================================
// 逐行解 RLE：把 lineBuffer 用行号 y 居中画到屏幕
//==========================================================
bool drawRleBin(const char* path) {
    // [debug] 每帧都会打印，循环播放时刷屏，排查时再打开
    // Serial.print("open: ");
    // Serial.println(path);

    File f = SPIFFS.open(path, "r");
    if (!f) {
        Serial.println("FILE NOT FOUND");
        return false;
    }

    if (f.size() < 12) {
        Serial.printf("BAD FILE: size=%u < 12\n", f.size());
        f.close();
        return false;
    }

    // ----- 读 12 字节头 -----
    char magic[8];
    f.read((uint8_t*)magic, 8);
    if (memcmp(magic, "RLE1", 4) != 0) {
        Serial.println("BAD MAGIC (不是 RLE1)");
        f.close();
        return false;
    }

    uint16_t frameW = 0, frameH = 0;
    f.read((uint8_t*)&frameW, 2);
    f.read((uint8_t*)&frameH, 2);
    // [debug] 刷屏，排查时再打开
    // Serial.printf("frame %ux%u\n", frameW, frameH);

    if (frameW == 0 || frameW > DISPLAY_W || frameH == 0 || frameH > DISPLAY_H) {
        Serial.printf("BAD GEOMETRY: %ux%u\n", frameW, frameH);
        f.close();
        return false;
    }

    uint16_t offsetY = (DISPLAY_H - frameH) / 2;
    // [debug] 刷屏，排查时再打开
    // Serial.printf("DBG frame=%ux%u  offsetY=%u  (will draw y=[%u..%u])\n",
    //               frameW, frameH, offsetY, offsetY, offsetY + frameH - 1);

    // ----- 逐行读数并解压 -----
    static uint16_t lineBuffer[DISPLAY_W];

    for (uint16_t y = 0; y < frameH; y++) {
        uint16_t x = 0;

        while (x < frameW) {
            uint8_t runBuf[4];
            if (f.read(runBuf, 4) != 4) {
                Serial.printf("READ ERROR @ y=%u x=%u\n", y, x);
                f.close();
                return false;
            }
            uint16_t count = runBuf[0] | (runBuf[1] << 8);
            uint16_t color = runBuf[2] | (runBuf[3] << 8);

            if (count == 0 || x + count > frameW) {
                Serial.printf("BAD RUN: count=%u @ y=%u x=%u\n", count, y, x);
                f.close();
                return false;
            }

            uint16_t end = x + count;
            for (; x < end; x++) lineBuffer[x] = color;
        }

        // 未覆盖区域填 0（黑），保险
        for (uint16_t xx = frameW; xx < DISPLAY_W; xx++) lineBuffer[xx] = 0;

        tft.drawRGBBitmap(0, (int16_t)(y + offsetY), lineBuffer, DISPLAY_W, 1);
    }

    f.close();
    return true;
}

//==========================================================
// 播放动画
//   common 参数：共用开场帧目录（先播 common/frame_000~001，
//                再播 folder 下的 frame_002~005）；NULL 时从 frame_000 播起
//   天气传 "/weather/common"，占卜传 "/luck/common"
//==========================================================
void playAnimation(const char* folder, int delayMs, const char* common) {
    // [debug] 每次循环播放都打印，刷屏，排查时再打开
    // Serial.printf("playAnimation(\"%s\") start\n", folder);
    char path[64];

    if (common) {
        snprintf(path, sizeof(path), "%s/frame_000.bin", common);
        if (SPIFFS.exists(path)) {
            if (!drawRleBin(path)) return;
            if (!waitWithButtons(delayMs)) return;   // 播放中按下按键 → 中断退出
            snprintf(path, sizeof(path), "%s/frame_001.bin", common);
            if (!drawRleBin(path)) return;
            if (!waitWithButtons(delayMs)) return;
        } else {
            Serial.printf("no %s frames\n", common);
        }
    }

    for (int i = common ? 2 : 0; i < 64; i++) {
        snprintf(path, sizeof(path), "%s/frame_%03d.bin", folder, i);

        // 文件不存在则停止
        if (!SPIFFS.exists(path)) {
            break;
        }

        if (!drawRleBin(path)) {
            // 任何一帧失败就中止，避免花屏
            return;
        }

        if (!waitWithButtons(delayMs)) return;   // 播放中按下按键 → 中断退出
    }
}

//===========================================================/ 天气：归一 + API + 映射
//==========================================================
String normalizeWeather(const String& text) {
    if (text.indexOf("晴") >= 0) return "Clear";
    if (text.indexOf("云") >= 0 || text.indexOf("雾") >= 0 || text.indexOf("阴") >= 0) return "Clouds";
    if (text.indexOf("雨") >= 0) return "Rain";
    if (text.indexOf("雪") >= 0) return "Snow";
    if (text.indexOf("雷") >= 0) return "Thunderstorm";
    return "";
}

//==========================================================
// gzip 解压：和风 API 强制 gzip（官方文档+实测，无法通过请求头关闭）
// 依赖 zlib_turbo 库（Arduino 库管理器搜索 zlib_turbo 安装）
//==========================================================
String gunzipPayload(const String& in) {
    // 不是 gzip（1F 8B 魔数）就直接返回
    if (in.length() < 18 || (uint8_t)in[0] != 0x1F || (uint8_t)in[1] != 0x8B) {
        return in;
    }

    // zlib_turbo 会越界读 4~8 字节，输入缓冲区必须多分配
    uint8_t* compressed = (uint8_t*)malloc(in.length() + 8);
    if (!compressed) return String();
    memcpy(compressed, in.c_str(), in.length());
    memset(compressed + in.length(), 0, 8);

    uint32_t uncSize = zt.gzip_info(compressed, in.length());
    if (uncSize == 0 || uncSize > 8192) {   // 防御：天气 JSON 最多几百字节
        free(compressed);
        Serial.printf("[gzip] bad gzip_info: %u\n", uncSize);
        return String();
    }

    uint8_t* uncompressed = (uint8_t*)malloc(uncSize + 8);
    if (!uncompressed) { free(compressed); return String(); }
    memset(uncompressed + uncSize, 0, 8);

    int rc = zt.gunzip(compressed, in.length(), uncompressed);
    free(compressed);
    if (rc != ZT_SUCCESS) {
        free(uncompressed);
        Serial.printf("[gzip] gunzip failed: rc=%d\n", rc);
        return String();
    }

    String result((const char*)uncompressed, uncSize);
    free(uncompressed);
    Serial.printf("[gzip] %u -> %u bytes\n", in.length(), uncSize);
    return result;
}

bool fetchWeather() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[weather] WiFi not connected");
        return false;
    }

    Serial.printf("[weather] WiFi RSSI=%d dBm\n", WiFi.RSSI());

    HTTPClient http;
    // 注意：该 API 域名的 80 端口已不可用（连接超时），必须用 https://
    // core 3.x 的 HTTPClient 没有 setInsecure()，TLS 配置在客户端对象上
    String url = String("https://") + QWEATHER_HOST
                 + "/v7/weather/now?location=" + QWEATHER_LOC
                 + "&key=" + QWEATHER_KEY;

    Serial.print("[weather] GET ");
    Serial.println(url);

    // 跳过证书校验（工程里本来就没做 TLS 校验）
    NetworkClientSecure httpsClient;
    httpsClient.setInsecure();
    http.begin(httpsClient, url);
    // 注意：和风 API 强制 gzip（官方文档+实测，Accept-Encoding 头无效，
    //       且 core 3.x 的 addHeader 会静默忽略该头），响应体在下方用 zlib_turbo 解压
    http.setTimeout(8000);
    http.collectAllHeaders(true);   // core 3.x：不设这个拿不到响应头
    int code = http.GET();

    if (code != HTTP_CODE_OK) {
        if (code < 0) {
            // 传输层错误：连接超时 / 拒连 / TLS 失败等
            Serial.printf("[weather] transport error: %s\n",
                          http.errorToString(code).c_str());
        } else {
            Serial.printf("[weather] HTTP code: %d\n", code);
        }
        // 出错时把响应头和 body 也打出来，方便定位
        // core 3.x：headers() 返回数量，逐条用 headerName(i)/header(i) 取
        int nHdr = http.headers();
        Serial.println("[weather] response headers:");
        for (int i = 0; i < nHdr; i++) {
            Serial.printf("  %s: %s\n",
                          http.headerName(i).c_str(),
                          http.header(i).c_str());
        }
        String body = http.getString();
        if (body.length()) {
            Serial.println("[weather] body: " + body);
        }
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();
    payload = gunzipPayload(payload);   // 解 gzip 再解析
    Serial.println("[weather] payload: " + payload);

    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, payload)) {
        Serial.println("[weather] JSON parse failed");
        return false;
    }
    const char* apiCode = doc["code"];
    if (!apiCode || String(apiCode) != "200") {
        Serial.printf("[weather] API code: %s\n", apiCode ? apiCode : "(null)");
        return false;
    }

    String text = doc["now"]["text"].as<String>();
    if (text.length() == 0) { Serial.println("[weather] empty text"); return false; }
    Serial.println("[weather] text = " + text);

    String norm = normalizeWeather(text);
    if (norm.length() == 0) { Serial.println("[weather] unknown: " + text); return false; }
    Serial.println("[weather] normalized = " + norm);

    const char* folder = "/weather/sunny";
    if (norm == "Clear")              folder = "/weather/sunny";
    else if (norm == "Clouds")        folder = "/weather/wind";
    else if (norm == "Rain")          folder = "/weather/rain";
    else if (norm == "Snow")          folder = "/weather/snow";
    else if (norm == "Thunderstorm")  folder = "/weather/thunder";

    weatherFolder = folder;
    Serial.print("[weather] folder = ");
    Serial.println(weatherFolder);
    return true;
}

//==========================================================
// 按键：20ms 轮询 + 边沿检测
// 只在"按下那一刻"置一次事件标志，长按/抖动不会重复触发；
// 具体行为（进哪个模式 / 退出）由各模式消费标志时决定
//==========================================================
void checkButtons() {
    static uint32_t lastCheck = 0;
    static bool lastB1 = HIGH, lastB2 = HIGH;

    if (millis() - lastCheck < 20) return;
    lastCheck = millis();

    bool b1 = (digitalRead(BTN1) == LOW);
    bool b2 = (digitalRead(BTN2) == LOW);

    if (b1 && !lastB1) btn1Pressed = true;   // 下降沿：刚按下
    if (b2 && !lastB2) btn2Pressed = true;

    lastB1 = b1;
    lastB2 = b2;
}

// 动画播放期间的延时：每 5ms 轮询按键，有按下就返回 false 提前中断
bool waitWithButtons(int ms) {
    uint32_t t0 = millis();
    while (millis() - t0 < (uint32_t)ms) {
        checkButtons();
        if (btn1Pressed || btn2Pressed) return false;
        delay(5);
    }
    return true;
}

//==========================================================
// setup
//==========================================================
void setup() {
    Serial.begin(115200);

#ifdef TFT_BL
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
#endif

    tft.init(240, 240);
    tft.setRotation(1);
    tft.fillScreen(ST77XX_BLACK);
    u8g2_for_adafruit_gfx.begin(tft);   // 中文字体引擎挂在屏幕上

    if (!SPIFFS.begin(true)) {
        Serial.println("[fs] SPIFFS Mount Failed");
        return;
    }
    Serial.printf("[fs] Total=%u Used=%u\n", SPIFFS.totalBytes(), SPIFFS.usedBytes());
    Serial.println("[fs] file list:");
    listFiles();

    pinMode(BTN1, INPUT_PULLUP);
    pinMode(BTN2, INPUT_PULLUP);
    delay(50);

    // WiFi 连接
    Serial.printf("[wifi] connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    WiFi.setAutoReconnect(true);

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
        Serial.print('.');
        delay(300);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[wifi] connected, IP=%s\n",
                      WiFi.localIP().toString().c_str());
        // NTP 时间同步（UTC+8 无夏令时）：占卜按"天"抽结果要用，失败不阻塞
        configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");
        if (fetchWeather()) {
            lastWeatherFetch = millis();
        }
    } else {
        Serial.println("\n[wifi] connect timeout");
    }
}

//==========================================================
// loop
//==========================================================
void loop() {
    // WiFi 断线后每 10s 重试一次
    if (WiFi.status() != WL_CONNECTED) {
        static uint32_t lastReconnect = 0;
        if (millis() - lastReconnect > 10000) {
            lastReconnect = millis();
            Serial.println("[wifi] reconnecting...");
            WiFi.reconnect();
        }
    }

    if (currentMode != lastMode) {
        tft.fillScreen(ST77XX_BLACK);
        lastMode = currentMode;
    }

    switch (currentMode) {

        case MODE_MENU:
            drawMenu();
            while (currentMode == MODE_MENU) {
                checkButtons();
                if (btn1Pressed) {
                    btn1Pressed = false;
                    Serial.println("BTN1 -> Weather");
                    currentMode = MODE_WEATHER;
                } else if (btn2Pressed) {
                    btn2Pressed = false;
                    luckFolder = pickLuckFolder();   // 进占卜时抽一次结果
                    Serial.printf("BTN2 -> Lucky: %s\n", luckFolder);
                    currentMode = MODE_LUCK;
                }
                delay(10);
            }
            break;

        case MODE_WEATHER: {
            bool needFetch = (lastWeatherFetch == 0) ||
                             (millis() - lastWeatherFetch > WEATHER_REFRESH_MS);
            if (needFetch) {
                showMessage("Fetching weather...");
                if (fetchWeather()) {
                    lastWeatherFetch = millis();
                    fetchFailCount   = 0;
                } else {
                    fetchFailCount++;
                    lastWeatherFetch = millis();          // 避免高频重试
                    Serial.printf("[weather] fail #%d, keep %s\n",
                                  fetchFailCount, weatherFolder);
                }
            }

            // 顶部小字：调试稳定后可删除
            // tft.setTextSize(1);
            // tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
            // tft.setCursor(4, 4);
            // tft.printf("WiFi:%s %s  try:%d  ",
            //            (WiFi.status() == WL_CONNECTED) ? "OK" : "--",
            //            weatherFolder, fetchFailCount);

            playAnimation(weatherFolder, 50, "/weather/common");   // 天气：共用开场帧 + 本目录帧

            // 播放期间任意键按下（waitWithButtons 已在帧间轮询）→ 立即回菜单
            if (btn1Pressed || btn2Pressed) {
                btn1Pressed = btn2Pressed = false;
                currentMode = MODE_MENU;
            }
            break;
        }

        case MODE_LUCK:
            playAnimation(luckFolder, 50, "/luck/common");   // 占卜：共用开场帧 + 抽中的结果

            // 播放期间任意键按下 → 立即回菜单
            if (btn1Pressed || btn2Pressed) {
                btn1Pressed = btn2Pressed = false;
                currentMode = MODE_MENU;
            }
            break;
    }
}
