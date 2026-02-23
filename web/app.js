let lastProposalId = "";

function randSession() {
  return Math.random().toString(16).slice(2) + Date.now().toString(16);
}

function getPlayerId() {
  const v = (document.getElementById("playerId")?.value || "").trim();
  return v.length ? v : "player_default";
}

function applyPlayerContext(payload) {
  payload.player_context = payload.player_context || {};
  payload.player_context.stable_player_id = getPlayerId();
  return payload;
}

function loadSession() {
  const saved = localStorage.getItem('proteus_session_id');
  if (saved) document.getElementById('session').value = saved;
}

function persistSession() {
  const session = document.getElementById('session').value.trim();
  if (session) localStorage.setItem('proteus_session_id', session);
}

function loadPlayerId() {
  const remember = localStorage.getItem("proteus_remember_player_id");
  const pid = localStorage.getItem("proteus_player_id");
  const rememberEl = document.getElementById("rememberPlayerId");
  const pidEl = document.getElementById("playerId");
  if (rememberEl) rememberEl.checked = (remember !== "0");
  if (pidEl && pid && (remember !== "0")) pidEl.value = pid;
}

function savePlayerId() {
  const rememberEl = document.getElementById("rememberPlayerId");
  const pidEl = document.getElementById("playerId");
  if (!rememberEl || !pidEl) return;
  localStorage.setItem("proteus_remember_player_id", rememberEl.checked ? "1" : "0");
  if (rememberEl.checked) localStorage.setItem("proteus_player_id", pidEl.value.trim());
}

async function refreshPlayerPanel() {
  const pid = getPlayerId();
  const panel = document.getElementById("playerPanel");
  if (!panel) return;

  try {
    const r = await fetch(`/dev/player_state?player_id=${encodeURIComponent(pid)}`);
    const j = await r.json();
    panel.textContent = JSON.stringify(j, null, 2);
  } catch (e) {
    panel.textContent = `Failed to load player state: ${e}`;
  }
}

async function fetchRecentInteractions() {
  const pid = getPlayerId();
  const panel = document.getElementById("playerPanel");
  if (!panel) return;
  try {
    const r = await fetch(`/dev/recent_interactions?player_id=${encodeURIComponent(pid)}&limit=25`);
    const j = await r.json();
    panel.textContent = JSON.stringify(j, null, 2);
  } catch (e) {
    panel.textContent = `Failed to load recent interactions: ${e}`;
  }
}

document.getElementById('genSession').addEventListener('click', () => {
  const s = randSession();
  document.getElementById('session').value = s;
  persistSession();
});

document.getElementById('session').addEventListener('change', persistSession);

document.getElementById("refreshPlayerBtn")?.addEventListener("click", refreshPlayerPanel);
document.getElementById("recentPlayerBtn")?.addEventListener("click", fetchRecentInteractions);
document.getElementById("playerId")?.addEventListener("input", () => {
  savePlayerId();
  refreshPlayerPanel();
});
document.getElementById("rememberPlayerId")?.addEventListener("change", savePlayerId);

async function postJson(url, payload) {
  const res = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  });
  return await res.json();
}

document.getElementById('queryBtn').addEventListener('click', async () => {
  const domain = document.getElementById('domain').value.trim();
  const prompt = document.getElementById('prompt').value;
  let session_id = document.getElementById('session').value.trim();
  if (!session_id) {
    session_id = randSession();
    document.getElementById('session').value = session_id;
  }
  persistSession();

  const payload = applyPlayerContext({ domain, prompt, session_id });
  const data = await postJson('/query', payload);
  if (!data.ok) {
    document.getElementById('status').textContent = 'Error: ' + JSON.stringify(data.errors || []);
    return;
  }

  lastProposalId = data.decision.proposal_id;
  document.getElementById('proposalText').textContent = data.proposal.text || '(no text)';
  document.getElementById('proposalJson').textContent = JSON.stringify(data.proposal, null, 2);
  document.getElementById('debug').textContent = JSON.stringify({
    prompt_hash: data.prompt_hash,
    explored: data.decision.explored,
    epsilon: data.decision.epsilon,
    seed: data.decision.selection_seed,
    seed_hex: data.decision.selection_seed_hex,
    candidate_count: data.candidate_count,
    proposal_id: data.decision.proposal_id,
    stable_player_id: data.decision.stable_player_id,
    session_id: data.session_id,
  }, null, 2);
  document.getElementById('status').textContent = 'Query OK';
  await refreshPlayerPanel();
});

async function sendReward(value) {
  const session_id = document.getElementById('session').value.trim();
  if (!session_id || !lastProposalId) {
    document.getElementById('status').textContent = 'Query first to get session/proposal_id';
    return;
  }
  const payload = applyPlayerContext({ session_id, proposal_id: lastProposalId, reward: value });
  const data = await postJson('/reward', payload);
  document.getElementById('debug').textContent = JSON.stringify(data, null, 2);
  document.getElementById('status').textContent = data.ok ? `Reward ${value.toFixed(2)} updated=${data.updated}` : ('Reward error: ' + JSON.stringify(data.errors || []));
  if (data.ok) {
    await refreshPlayerPanel();
  }
}

for (const btn of document.querySelectorAll('.star')) {
  btn.addEventListener('click', async () => {
    const stars = Number(btn.dataset.score);
    const reward = (stars - 1) / 4;
    await sendReward(reward);
  });
}

document.getElementById('thumbUp').addEventListener('click', () => sendReward(1.0));
document.getElementById('thumbDown').addEventListener('click', () => sendReward(0.0));


loadSession();
loadPlayerId();
refreshPlayerPanel();
