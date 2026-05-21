import OpenAI from "openai";

let openai: OpenAI;
function getClient() {
  if (!openai) openai = new OpenAI({ apiKey: process.env.OPENAI_API_KEY });
  return openai;
}

export async function transcribeAudio(buffer: Buffer, filename: string): Promise<string> {
  const file = new File([buffer], filename, { type: "audio/wav" });

  const response = await getClient().audio.transcriptions.create({
    model: "whisper-1",
    file,
    language: "ja",
    response_format: "text",
    prompt: "これはNotionにタスクを登録するための音声命令です。例: 明日までに資料を作る、企画書を書く、買い物リストに牛乳を追加。",
    temperature: 0,
  });

  return (response as unknown as string).trim();
}
