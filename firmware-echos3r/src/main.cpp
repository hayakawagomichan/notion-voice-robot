/**
 * ATOM EchoS3R — しゃべれるNotionタスク投入ロボ
 * M5Unified の M5.Mic / M5.Speaker API を使用
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "config.h"

static int16_t* recordBuffer = nullptr;
static uint8_t* responseBuffer = nullptr;

// --- ビープ (起動診断用) ---
void beep(int count) {
  if (!M5.Speaker.isEnabled()) {
    M5.Mic.end();
    M5.Speaker.begin();
  }
  uint8_t prevVol = M5.Speaker.getVolume();
  M5.Speaker.setVolume(45);  // 小さめ
  for (int i = 0; i < count; i++) {
    M5.Speaker.tone(2000, 120);
    while (M5.Speaker.isPlaying()) delay(5);
    delay(120);
  }
  M5.Speaker.setVolume(prevVol);
}

// --- WAVヘッダ作成 ---
void writeWavHeader(uint8_t* buf, uint32_t dataSize) {
  uint32_t fileSize = dataSize + 36;
  uint32_t byteRate = SAMPLE_RATE * 1 * 16 / 8;
  uint16_t blockAlign = 1 * 16 / 8;

  memcpy(buf, "RIFF", 4);
  memcpy(buf + 4, &fileSize, 4);
  memcpy(buf + 8, "WAVE", 4);
  memcpy(buf + 12, "fmt ", 4);
  uint32_t fmtSize = 16;
  memcpy(buf + 16, &fmtSize, 4);
  uint16_t audioFmt = 1;
  memcpy(buf + 20, &audioFmt, 2);
  uint16_t numCh = 1;
  memcpy(buf + 22, &numCh, 2);
  uint32_t sr = SAMPLE_RATE;
  memcpy(buf + 24, &sr, 4);
  memcpy(buf + 28, &byteRate, 4);
  memcpy(buf + 32, &blockAlign, 2);
  uint16_t bps = 16;
  memcpy(buf + 34, &bps, 2);
  memcpy(buf + 36, "data", 4);
  memcpy(buf + 40, &dataSize, 4);
}

// --- Wi-Fi接続 ---
void connectWiFi() {
  Serial.printf("Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    retries++;
    if (retries > 40) {
      Serial.println("\nWiFi FAILED");
      while (1) { beep(3); delay(2000); }
    }
  }
  Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
}

// --- 音量計算 (VAD) ---
int16_t getAudioLevel(int16_t* buf, size_t samples) {
  int32_t sum = 0;
  for (size_t i = 0; i < samples; i++) sum += abs(buf[i]);
  return (int16_t)(sum / samples);
}

// --- 録音 (VADベース、無音検知で終了) ---
uint32_t recordWithVAD() {
  const size_t chunkSamples = SAMPLE_RATE / 10;  // 100ms
  // メインループで先頭1チャンクが既に recordBuffer に書き込まれているのでそこから続ける
  uint32_t totalSamples = chunkSamples;
  uint32_t maxSamples = SAMPLE_RATE * RECORD_DURATION;
  uint32_t silenceStart = millis();
  bool recording = true;  // 既に発話検知済みで呼ばれる前提

  while (totalSamples < maxSamples) {
    int16_t* writePos = recordBuffer + totalSamples;
    if (!M5.Mic.record(writePos, chunkSamples, SAMPLE_RATE)) break;
    while (M5.Mic.isRecording()) delay(2);

    int16_t level = getAudioLevel(writePos, chunkSamples);
    totalSamples += chunkSamples;

    if (level > VAD_THRESHOLD) {
      if (!recording) {
        recording = true;
        Serial.printf("Recording started (level=%d)\n", level);
      }
      silenceStart = millis();
    } else if (recording) {
      if (silenceStart == 0) silenceStart = millis();
      if (millis() - silenceStart > VAD_SILENCE_MS) {
        Serial.println("Silence detected, stopping");
        break;
      }
    }
  }

  return totalSamples * 2;  // bytes
}

// --- サーバ送信 ---
bool sendToServer(uint32_t pcmBytes) {
  HTTPClient http;
  String url = String("http://") + SERVER_HOST + ":" + SERVER_PORT + VOICE_ENDPOINT;
  http.begin(url);
  http.setTimeout(30000);

  size_t totalSize = 44 + pcmBytes;
  uint8_t* wavBuf = (uint8_t*)ps_malloc(totalSize);
  if (!wavBuf) {
    Serial.println("Failed to alloc WAV buffer");
    return false;
  }
  writeWavHeader(wavBuf, pcmBytes);
  memcpy(wavBuf + 44, recordBuffer, pcmBytes);

  // multipart の代わりにシンプルにバイナリPOSTでも動くようにする
  http.addHeader("Content-Type", "audio/wav");
  int code = http.POST(wavBuf, totalSize);
  free(wavBuf);

  Serial.printf("HTTP %d\n", code);

  if (code == 200) {
    int len = http.getSize();
    Serial.printf("Response size: %d\n", len);

    if (len > 0) {
      responseBuffer = (uint8_t*)ps_realloc(responseBuffer, len);
      WiFiClient* stream = http.getStreamPtr();
      int read = 0;
      while (read < len && http.connected()) {
        size_t avail = stream->available();
        if (avail) {
          int r = stream->read(responseBuffer + read, avail);
          if (r > 0) read += r;
        }
        delay(1);
      }

      // WAV ヘッダから sample_rate を読む (24バイト目から4バイト)
      if (read > 44) {
        uint32_t ttsRate = 24000;
        memcpy(&ttsRate, responseBuffer + 24, 4);
        Serial.printf("TTS rate: %u Hz, bytes: %d\n", ttsRate, read - 44);
        M5.Mic.end();
        M5.Speaker.begin();
        M5.Speaker.setVolume(180);
        M5.Speaker.playRaw((int16_t*)(responseBuffer + 44), (read - 44) / 2, ttsRate, false, 1, 0);
        while (M5.Speaker.isPlaying()) delay(10);
      }
    }
    http.end();
    return true;
  }
  http.end();
  return false;
}

// ============================
// setup
// ============================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  Serial.begin(115200);
  Serial.println("\n=== Notion Voice Robot (EchoS3R) ===");

  M5.Speaker.setVolume(180);
  beep(1);  // ピッ x1 = M5.begin OK

  // PSRAMバッファ確保
  recordBuffer = (int16_t*)ps_malloc(RECORD_BUFFER_SIZE);
  responseBuffer = (uint8_t*)ps_malloc(1024);
  if (!recordBuffer) {
    Serial.println("FATAL: no PSRAM");
    while (1) { beep(4); delay(2000); }
  }

  connectWiFi();
  beep(2);  // ピピッ x2 = READY

  // マイク設定: ゲイン上げてノイズフィルタを最大化
  auto micCfg = M5.Mic.config();
  micCfg.noise_filter_level = 200;
  micCfg.stereo = false;
  micCfg.magnification = 4;  // 16倍だとクリッピングしたので4倍
  M5.Mic.config(micCfg);

  // マイクを起動しっぱなしにする
  M5.Speaker.end();
  M5.Mic.begin();

  Serial.println("Ready!\n");
}

// ============================
// loop
// ============================
void loop() {
  M5.update();

  // VAD: 100ms単位でレベル監視
  const size_t checkSamples = SAMPLE_RATE / 10;
  int16_t checkBuf[checkSamples];

  if (!M5.Mic.record(checkBuf, checkSamples, SAMPLE_RATE)) {
    delay(100);
    return;
  }
  while (M5.Mic.isRecording()) delay(2);

  int16_t level = getAudioLevel(checkBuf, checkSamples);

  // ハートビート: 2秒ごとに出力
  static unsigned long lastHB = 0;
  if (millis() - lastHB > 2000) {
    Serial.printf("[HB] level=%d\n", level);
    lastHB = millis();
  }

  if (level > VAD_THRESHOLD) {
    Serial.printf("Voice detected (level=%d)\n", level);
    memcpy(recordBuffer, checkBuf, checkSamples * 2);

    uint32_t bytes = recordWithVAD();

    if (bytes > VAD_MIN_DURATION * SAMPLE_RATE / 1000 * 2) {
      Serial.printf("Sending %u bytes\n", bytes);
      sendToServer(bytes);
    } else {
      Serial.println("Too short, ignoring");
    }

    // 再生後にクールダウン (エコー誤検知防止)
    M5.Speaker.end();
    delay(2000);  // 2秒待ってからマイク再開
    M5.Mic.begin();
  }
}
