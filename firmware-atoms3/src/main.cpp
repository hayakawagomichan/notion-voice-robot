/**
 * ATOM S3 — 顔表示プレースホルダ
 *
 * 128x128 LCD に "Ready" とだけ表示する最小実装です。
 * 顔アニメーション等で使う画像はライセンス上同梱していません。
 *
 * --- 自分の画像を表示したい場合 ---
 * 1. お好きな 128x128 画像 (PNG/JPEG等) を用意
 * 2. RGB565 形式の C 配列に変換
 *    例: https://lvgl.io/tools/imageconverter (Color format: RGB565, Output: C array)
 * 3. include/faces.h を作成し、PROGMEM 配列として保持:
 *      const uint16_t my_face[16384] PROGMEM = { 0xFFFF, ... };
 * 4. setup() / loop() で表示:
 *      M5.Lcd.pushImage(0, 0, 128, 128, my_face);
 */

#include <Arduino.h>
#include <M5Unified.h>

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  Serial.begin(115200);
  Serial.println("=== ATOM S3 Face Display (placeholder) ===");

  M5.Lcd.setRotation(2);  // 本体逆さマウント想定
  M5.Lcd.setBrightness(80);
  M5.Lcd.fillScreen(TFT_BLACK);

  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(28, 50);
  M5.Lcd.print("Ready");

  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TFT_DARKGREY);
  M5.Lcd.setCursor(8, 90);
  M5.Lcd.print("Add your own");
  M5.Lcd.setCursor(8, 102);
  M5.Lcd.print("faces.h");
}

void loop() {
  M5.update();
  delay(100);
}
