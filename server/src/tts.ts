import OpenAI from "openai";

let openai: OpenAI;
function getClient() {
  if (!openai) openai = new OpenAI({ apiKey: process.env.OPENAI_API_KEY });
  return openai;
}

export async function textToSpeech(text: string): Promise<Buffer> {
  // 短く切る (EchoS3Rのダウンロード負荷軽減 + 30sec以内)
  const trimmed = text.slice(0, 100);

  const response = await getClient().audio.speech.create({
    model: "tts-1",
    voice: "nova",       // 明るめの声
    input: trimmed,
    response_format: "wav",
    speed: 1.1,          // 少し速め
  });

  const arrayBuf = await response.arrayBuffer();
  return Buffer.from(arrayBuf);
}
