#pragma once

// ====== Wi-Fi ======
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// ====== PCサーバ ======
#define SERVER_HOST   "192.168.1.100"   // PCのIPアドレス
#define SERVER_PORT   3000
#define VOICE_ENDPOINT "/voice"

// ====== 録音設定 ======
#define SAMPLE_RATE       16000
#define RECORD_DURATION   8
#define RECORD_BUFFER_SIZE (SAMPLE_RATE * RECORD_DURATION * 2)

// ====== VAD ======
#define VAD_THRESHOLD     1200
#define VAD_SILENCE_MS    1500
#define VAD_MIN_DURATION  500

// ====== ES8311 I2C/I2S ピン (ATOM EchoS3R 公式仕様) ======
#define I2C_SDA    45
#define I2C_SCL     0
#define I2S_BCK    17
#define I2S_WS      3
#define I2S_DIN     4
#define I2S_DOUT   48
#define SPK_EN     11
