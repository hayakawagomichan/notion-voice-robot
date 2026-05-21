import express from "express";
import multer from "multer";
import { transcribeAudio } from "./whisper.js";
import { sendToNotionAgent } from "./notion-agent.js";
import { textToSpeech } from "./tts.js";

// --- .env を手動読み込み (dotenv不要にするため) ---
import { readFileSync } from "fs";
import { resolve, dirname } from "path";
import { fileURLToPath } from "url";

const __dirname = dirname(fileURLToPath(import.meta.url));
try {
  const envPath = resolve(__dirname, "..", ".env");
  const envContent = readFileSync(envPath, "utf-8");
  for (const line of envContent.split("\n")) {
    const trimmed = line.trim();
    if (!trimmed || trimmed.startsWith("#")) continue;
    const eqIdx = trimmed.indexOf("=");
    if (eqIdx === -1) continue;
    const key = trimmed.slice(0, eqIdx).trim();
    const val = trimmed.slice(eqIdx + 1).trim();
    if (!process.env[key]) process.env[key] = val;
  }
} catch {
  console.log("⚠ .env not found, using system env vars");
}

const app = express();
const PORT = Number(process.env.PORT) || 3000;

// multer: メモリに受け取る (multipart用)
const upload = multer({ storage: multer.memoryStorage(), limits: { fileSize: 5 * 1024 * 1024 } });

// raw audio body も受ける
app.use("/voice", express.raw({ type: "audio/wav", limit: "5mb" }));

// ============================
// POST /voice — メインエンドポイント
// ============================
app.post("/voice", upload.single("audio"), async (req, res) => {
  const start = Date.now();
  // multipart の req.file または raw の req.body どちらでもOK
  const audioBuffer: Buffer | undefined =
    req.file?.buffer ?? (Buffer.isBuffer(req.body) ? req.body : undefined);
  const size = audioBuffer?.length ?? 0;
  console.log(`\n🎤 /voice received (${size} bytes)`);

  if (!audioBuffer || size === 0) {
    res.status(400).json({ error: "audio file required" });
    return;
  }

  // 音声振幅分析 (WAV header 44バイトをスキップして PCM int16 を見る)
  {
    const pcm = audioBuffer.subarray(44);
    let peak = 0;
    let sumSq = 0;
    const samples = Math.floor(pcm.length / 2);
    for (let i = 0; i < pcm.length; i += 2) {
      const s = pcm.readInt16LE(i);
      const a = Math.abs(s);
      if (a > peak) peak = a;
      sumSq += s * s;
    }
    const rms = Math.sqrt(sumSq / samples);
    console.log(`  → Audio: peak=${peak}, rms=${rms.toFixed(0)}, samples=${samples}`);

    // RMS が極端に低い (無音) なら Whisper 通さずに弾く
    if (rms < 200) {
      console.log("  → Audio too quiet (rms < 200), skipping");
      res.set({ "Content-Type": "audio/wav" });
      res.send(Buffer.alloc(44));
      return;
    }
  }

  try {
    // 1) Whisper: 音声→テキスト
    console.log("  → Whisper transcribing...");
    const text = await transcribeAudio(audioBuffer, "audio.wav");
    console.log(`  → Text: "${text}"`);

    if (!text || text.trim().length === 0) {
      console.log("  → Empty transcription, returning silence");
      res.status(200).json({ text: "", reply: "聞き取れませんでした" });
      return;
    }

    // Whisper の典型的な hallucination を弾く
    const hallucinations = [
      "視聴",
      "チャンネル登録",
      "ご清聴",
      "字幕",
      "購読",
      "ありがとうございました",
      "次回もお楽しみに",
      "Thanks for watching",
    ];
    if (hallucinations.some(h => text.includes(h))) {
      console.log(`  → Hallucination detected, ignoring: "${text}"`);
      // 無音WAVを返して何もしない
      res.set({ "Content-Type": "audio/wav" });
      res.send(Buffer.alloc(44));  // 空のWAVっぽいもの
      return;
    }

    // 2) Notion Agent: タスク投入
    console.log("  → Sending to Notion Agent...");
    const agentReply = await sendToNotionAgent(text);
    console.log(`  → Agent reply: "${agentReply.slice(0, 100)}..."`);

    // 3) TTS: テキスト→音声
    console.log("  → Generating TTS...");
    const ttsBuffer = await textToSpeech(agentReply);
    console.log(`  → TTS done (${ttsBuffer.length} bytes), total ${Date.now() - start}ms`);

    res.set({
      "Content-Type": "audio/wav",
      "X-Transcription": encodeURIComponent(text),
      "X-Agent-Reply": encodeURIComponent(agentReply.slice(0, 200)),
    });
    res.send(ttsBuffer);
  } catch (err) {
    console.error("  ✗ Error:", err);
    res.status(500).json({ error: String(err) });
  }
});

// ============================
// GET /health — 疎通確認
// ============================
app.get("/health", (_req, res) => {
  res.json({ status: "ok", uptime: process.uptime() });
});

// ============================
// POST /test-text — テキストでテスト (curlデバッグ用)
// ============================
app.use(express.json());
app.post("/test-text", async (req, res) => {
  const { text } = req.body;
  if (!text) {
    res.status(400).json({ error: "text required" });
    return;
  }

  try {
    console.log(`\n📝 /test-text: "${text}"`);
    const agentReply = await sendToNotionAgent(text);
    console.log(`  → Agent reply: "${agentReply.slice(0, 100)}..."`);
    res.json({ text, reply: agentReply });
  } catch (err) {
    console.error("  ✗ Error:", err);
    res.status(500).json({ error: String(err) });
  }
});

app.listen(PORT, "0.0.0.0", () => {
  console.log(`\n🤖 Notion Voice Robot Server`);
  console.log(`   http://0.0.0.0:${PORT}`);
  console.log(`   POST /voice       — WAV音声を送ってNotionにタスク投入`);
  console.log(`   POST /test-text   — テキストで直接テスト`);
  console.log(`   GET  /health      — 疎通確認\n`);
});
