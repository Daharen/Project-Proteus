let mode = 'class';
let selectedNpc = null;
let chosenClusterId = '';
let synonymQueue = [];
let lastSearch = { text: '', query_domain: '' };

const $ = (id) => document.getElementById(id);

function domainForMode() {
  if (mode === 'class') return 'class';
  if (mode === 'skill') return 'skill';
  return selectedNpc ? 'dialogue_option' : 'npc_intent';
}

async function post(url, payload) {
  const r = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  });
  return r.json();
}

function setStatus(obj) { $('status').textContent = JSON.stringify(obj, null, 2); }

function resetAdjudicationUI() {
  chosenClusterId = '';
  synonymQueue = [];
  $('chosenCluster').textContent = 'None';
  $('synQueue').textContent = '(empty)';
  $('adjudicateStatus').textContent = '';
  $('adjudicateBtn').disabled = true;
}

function clearRecognition() {
  $('resolverOutcome').textContent = '';
  $('resolutionBadge').textContent = '';
  $('closestExisting').textContent = '';
  $('recognitionConfidence').textContent = '';
  $('recognitionConfidence').className = '';
  $('alternates').innerHTML = '';
  $('recognitionPrompt').innerHTML = '';
  $('chooseClosestBtn').disabled = true;
  $('forceNovelBtn').disabled = true;
  resetAdjudicationUI();
}

function candidateText(c) {
  const label = c.canonical_label || '(no canonical label)';
  return `${label} | ${c.cluster_id} (${Number(c.score || 0).toFixed(3)})`;
}

function setChosenCluster(clusterId, msg) {
  chosenClusterId = clusterId || '';
  $('chosenCluster').textContent = chosenClusterId || 'None';
  $('adjudicateBtn').disabled = !chosenClusterId;
  if (msg) $('adjudicateStatus').textContent = msg;
}

function renderResolution(resolution) {
  $('resolverOutcome').textContent = resolution?.decision_band || 'unknown';
  $('resolutionBadge').textContent = resolution?.decision_band || '';
  $('resolutionBadge').className = `badge band-${resolution?.decision_band || 'unknown'}`;
}

function renderRecognition(recognition) {
  const closest = recognition?.closest_existing;
  if (closest) {
    $('closestExisting').textContent = candidateText(closest);
    $('recognitionConfidence').textContent = closest.confidence_band || 'unknown';
    $('recognitionConfidence').className = `confidence-${closest.confidence_band || 'exploratory'}`;
    $('chooseClosestBtn').disabled = false;
    setChosenCluster(closest.cluster_id);
  } else {
    $('closestExisting').textContent = '(none)';
    $('recognitionConfidence').textContent = 'n/a';
    $('chooseClosestBtn').disabled = true;
  }

  const host = $('alternates');
  host.innerHTML = '';
  for (const a of recognition?.alternates || []) {
    const row = document.createElement('div');
    const b = document.createElement('button');
    b.textContent = `Choose ${candidateText(a)}`;
    b.onclick = () => setChosenCluster(a.cluster_id, 'Selected alternate for adjudication.');
    row.appendChild(b);
    host.appendChild(row);
  }

  const promptHost = $('recognitionPrompt');
  promptHost.innerHTML = '';
  if (recognition?.prompt?.needed) {
    const q = document.createElement('div');
    q.textContent = recognition.prompt.prompt_text;
    promptHost.appendChild(q);
    for (const option of recognition.prompt.options || []) {
      const pill = document.createElement('span');
      pill.className = 'prompt-option';
      pill.textContent = option;
      promptHost.appendChild(pill);
    }
  }
}

async function resolveGuess(text, query_domain) {
  const j = await post('/api/funnel/resolve_guess', {
    text,
    query_domain,
    thresholds_version: 'v1',
    limit: 8,
  });
  setStatus({ endpoint: 'resolve_guess', response: j });
  if (!j || !j.ok || !j.resolution || !j.recognition) {
    $('adjudicateStatus').textContent = 'resolve_guess failed or unexpected shape.';
    return;
  }

  renderResolution(j.resolution);
  renderRecognition(j.recognition);

  if (!j.recognition.closest_existing && (j.resolution.decision_band === 'alias_hit' || j.resolution.decision_band === 'hard_duplicate' || j.resolution.decision_band === 'grey_duplicate')) {
    setChosenCluster(j.resolution.cluster_id);
  }

  $('forceNovelBtn').disabled = !(j.force_novel_available === true);
  $('forceNovelBtn').onclick = async () => {
    const boot = await post('/api/funnel/bootstrap', { text, query_domain, llm_mode: $('llmMode').value, thresholds_version: 'v1' });
    setStatus(boot);
  };
}

for (const b of document.querySelectorAll('.modeBtn')) {
  b.onclick = () => {
    mode = b.dataset.mode;
    selectedNpc = null;
    $('stepTitle').textContent = mode === 'dialogue' ? 'Dialogue Step A: Who do you want to talk to and why' : `Search ${mode}`;
    $('results').innerHTML = '';
    clearRecognition();
  };
}

$('searchBtn').onclick = async () => {
  clearRecognition();
  const text = $('text').value;
  const query_domain = domainForMode();
  lastSearch = { text, query_domain };
  await resolveGuess(text, query_domain);
};

$('chooseClosestBtn').onclick = () => {
  if (chosenClusterId) {
    $('adjudicateStatus').textContent = 'Closest existing selected for adjudication.';
  }
};

$('otherBtn').onclick = async () => {
  clearRecognition();
  const text = $('text').value;
  const query_domain = domainForMode();
  const j = await post('/api/funnel/bootstrap', { text, query_domain, llm_mode: $('llmMode').value, thresholds_version: 'v1' });
  setStatus(j);
};

$('addSynBtn').onclick = () => {
  const term = ($('synTerm').value || '').trim();
  const canonical_term = ($('synCanon').value || '').trim();
  if (!term || !canonical_term) {
    $('adjudicateStatus').textContent = 'Synonym term and canonical are required.';
    return;
  }
  synonymQueue.push({ term, canonical_term });
  $('synTerm').value = '';
  $('synCanon').value = '';
  renderSynQueue();
  $('adjudicateStatus').textContent = 'Synonym mapping queued.';
};

function renderSynQueue() {
  const host = $('synQueue');
  host.innerHTML = '';
  if (synonymQueue.length === 0) {
    host.textContent = '(empty)';
    return;
  }
  synonymQueue.forEach((item, i) => {
    const row = document.createElement('div');
    row.className = 'row';
    const span = document.createElement('span');
    span.textContent = `${item.term} → ${item.canonical_term}`;
    const rm = document.createElement('button');
    rm.textContent = 'Remove';
    rm.onclick = () => { synonymQueue.splice(i, 1); renderSynQueue(); };
    row.appendChild(span);
    row.appendChild(rm);
    host.appendChild(row);
  });
}

$('adjudicateBtn').onclick = async () => {
  if (!chosenClusterId) {
    $('adjudicateStatus').textContent = 'Choose a cluster first.';
    return;
  }

  const payload = {
    query_domain: lastSearch.query_domain || domainForMode(),
    cluster_id: chosenClusterId,
    alias_text: lastSearch.text || $('text').value,
    synonyms: synonymQueue.slice(),
  };

  const j = await post('/api/funnel/adjudicate', payload);
  setStatus({ endpoint: 'adjudicate', response: j });

  if (j && j.ok) {
    $('adjudicateStatus').textContent = `Adjudicated. alias_written=${j.alias_written} synonyms_written=${j.synonyms_written}`;
    await resolveGuess(payload.alias_text, payload.query_domain);
  } else {
    $('adjudicateStatus').textContent = 'Adjudication failed.';
  }
};

clearRecognition();
renderSynQueue();
