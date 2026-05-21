# しゃべれる Notion タスク投入ロボ — Claude Code 引継ぎメモ

## プロジェクト概要
M5Stack ATOM EchoS3R + ATOM S3 + PCサーバで、音声でNotion Custom Agentにタスク投入するデモ。

## 構成
```
notion-voice-robot/
├── server/                  PCサーバ (Node.js/TypeScript)
│   ├── src/
│   │   ├── index.ts         Express: POST /voice, /test-text, GET /health
│   │   ├── whisper.ts       OpenAI Whisper (音声→テキスト)
│   │   ├── notion-agent.ts  Notion Custom Agent連携
│   │   └── tts.ts           OpenAI TTS (テキスト→音声)
│   └── .env.example
├── firmware-echos3r/        ATOM EchoS3R (PlatformIO/Arduino)
│   ├── src/main.cpp         VAD録音→WAV POST→TTS再生
│   └── include/
│       └── config.example.h Wi-Fi/サーバIP/VAD閾値の設定
└── firmware-atoms3/         ATOM S3 (PlatformIO/Arduino)
    ├── src/main.cpp         顔画像ループ表示
    └── include/faces.h      RGB565 128x128 x4
```

## 動作フロー
EchoS3R音声検知 → 録音 → WAV POST → Whisper → Notion Agent → TTS → EchoS3Rで再生

## ハードウェア重要事項
- **ATOM EchoS3R には LCD/LED 無し**。状態通知はビープ音のみ
- **EchoS3R 専用ピン (M5Unified が自動設定するので意識不要だが参考):**
  I2C SDA=45, SCL=0 / I2S BCK=17, WS=3, MCLK=11, DIN=4, DOUT=48 / NS4150 EN=18
- `M5Atomic-EchoBase` ライブラリは AtomS3+別売 Echo Base 用で **EchoS3R では使わない**
- **M5Unified ≥ 0.2.8 を使う**。`M5.Mic.record()` / `M5.Speaker.playRaw()` の標準APIで動く
- M5Unified はマイクとスピーカー同時不可。切替時に `end()` / `begin()` が必要
- **Octal PSRAM 対応に `board_build.arduino.memory_type = qio_opi` が必須**

## 既知の罠
- ESP32-S3 ネイティブUSB CDC: 起動直後の Serial.println は取りこぼされやすい
- upload_speed 921600 で `No serial data received` が出る場合は 460800 か 115200 に下げる
- Whisper の hallucination (「ご視聴ありがとうございました」等)。サーバ側でフィルタリング
- TTS 応答が長すぎると EchoS3R のダウンロード/再生に時間かかる → 100文字に切る
- Notion Agent SDK の chatStream で返る chunk.content は**累積文字列**。`+=` ではなく `=` で代入する

## 起動時ビープの意味 (EchoS3R)
| 音 | 意味 |
|---|---|
| ピッ x1 | M5.begin 完了 |
| ピピッ x2 | Wi-Fi 接続完了 = READY |
| ピピピッ x3 (繰返) | Wi-Fi 接続失敗 |
| ピピピピッ x4 (繰返) | PSRAM 確保失敗 |

## 技術メモ
- Notion Agents SDK: npmレジストリ未公開。ローカル clone & build → `npm install ./path` が必要
- TTS WAV は 24kHz mono 16bit。再生時は WAV ヘッダの sample_rate を読み取って `playRaw` に渡す
- `/voice` エンドポイントは multipart と raw binary 両対応
