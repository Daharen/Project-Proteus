let lastProposalId = "";

function randSession() {
  return Math.random().toString(16).slice(2) + Date.now().toString(16);
}

function loadSession() {
  const saved = localStorage.getItem('proteus_session_id');
  if (saved) document.getElementById('session').value = saved;
}

function persistSession() {
  const session = document.getElementById('session').value.trim();
  if (session) localStorage.setItem('proteus_session_id', session);
}

document.getElementById('genSession').addEventListener('click', () => {
  const s = randSession();
  document.getElementById('session').value = s;
  persistSession();
});

document.getElementById('session').addEventListener('change', persistSession);

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

  const data = await postJson('/query', { domain, prompt, session_id });
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
    session_id: data.session_id,
  }, null, 2);
  document.getElementById('status').textContent = 'Query OK';
});

async function sendReward(value) {
  const session_id = document.getElementById('session').value.trim();
  if (!session_id || !lastProposalId) {
    document.getElementById('status').textContent = 'Query first to get session/proposal_id';
    return;
  }
  const data = await postJson('/reward', { session_id, proposal_id: lastProposalId, reward: value });
  document.getElementById('status').textContent = data.ok ? `Reward ${value.toFixed(2)} updated=${data.updated}` : ('Reward error: ' + JSON.stringify(data.errors || []));
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
