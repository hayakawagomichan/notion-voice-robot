/**
 * Notion Custom Agent へメッセージを送信してタスク投入
 *
 * notion-agents-sdk-js をローカルビルドして npm install する必要がある:
 *   git clone https://github.com/makenotion/notion-agents-sdk-js
 *   cd notion-agents-sdk-js && npm install && npm run build
 *   cd ../server && npm install /path/to/notion-agents-sdk-js
 *
 * SDK が未インストールの場合は fetch で直接 API を叩くフォールバック付き
 */

let NotionAgentsClient: any;
let stripLangTags: any;
let sdkAvailable = false;

try {
  const sdk = await import("@notionhq/agents-client");
  NotionAgentsClient = sdk.NotionAgentsClient;
  stripLangTags = sdk.stripLangTags;
  sdkAvailable = true;
  console.log("✓ Notion Agents SDK loaded");
} catch {
  console.log("⚠ Notion Agents SDK not found, using direct API fallback");
}

// --- SDK版 ---
async function sendViaSDK(message: string): Promise<string> {
  const client = new NotionAgentsClient({ auth: process.env.NOTION_API_TOKEN });
  const agent = client.agents.agent(process.env.NOTION_AGENT_ID);

  // 応答を簡潔にするための指示を付加
  const wrapped = `${message}\n\n（必ず30文字以内で簡潔に返答してください。詳細説明は不要です）`;

  let reply = "";
  for await (const chunk of agent.chatStream({ message: wrapped })) {
    if (chunk.type === "message" && chunk.role === "agent") {
      // chunk.content は累積文字列なので上書き
      reply = stripLangTags ? stripLangTags(chunk.content) : chunk.content;
    }
    if (chunk.type === "error") {
      throw new Error(`Notion Agent error [${chunk.code}]: ${chunk.message}`);
    }
  }
  return reply.trim() || "タスクを処理しました。";
}

// --- フォールバック: fetch で直接叩く ---
async function sendViaFetch(message: string): Promise<string> {
  // Step 1: スレッド作成 (chat)
  const token = process.env.NOTION_API_TOKEN!;
  const agentId = process.env.NOTION_AGENT_ID!;
  const chatRes = await fetch(
    `https://api.notion.com/v1/agents/${agentId}/chat`,
    {
      method: "POST",
      headers: {
        Authorization: `Bearer ${token}`,
        "Content-Type": "application/json",
        "Notion-Version": "2025-09-03",
      },
      body: JSON.stringify({ message }),
    }
  );

  if (!chatRes.ok) {
    const errText = await chatRes.text();
    throw new Error(`Notion API error ${chatRes.status}: ${errText}`);
  }

  const { thread_id } = (await chatRes.json()) as { thread_id: string };

  // Step 2: ポーリング
  for (let i = 0; i < 30; i++) {
    await new Promise((r) => setTimeout(r, 2000));

    const statusRes = await fetch(
      `https://api.notion.com/v1/agents/${agentId}/threads/${thread_id}`,
      {
        headers: {
          Authorization: `Bearer ${token}`,
          "Notion-Version": "2025-09-03",
        },
      }
    );

    const data = (await statusRes.json()) as { status: string };
    if (data.status === "completed") break;
    if (data.status === "failed") throw new Error("Notion Agent failed");
  }

  // Step 3: メッセージ取得
  const msgRes = await fetch(
    `https://api.notion.com/v1/agents/${agentId}/threads/${thread_id}/messages`,
    {
      headers: {
        Authorization: `Bearer ${token}`,
        "Notion-Version": "2025-09-03",
      },
    }
  );

  const msgData = (await msgRes.json()) as {
    results: Array<{ role: string; content: string }>;
  };

  const agentMessages = msgData.results.filter((m) => m.role === "agent");
  return agentMessages.map((m) => m.content).join("\n").trim() || "タスクを処理しました。";
}

/**
 * Notion Custom Agent にメッセージを送り、応答を返す
 */
export async function sendToNotionAgent(message: string): Promise<string> {
  if (sdkAvailable) {
    return sendViaSDK(message);
  }
  return sendViaFetch(message);
}
