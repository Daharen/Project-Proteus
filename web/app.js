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
  $('bestGuess').textContent = '';
  $('alternates').innerHTML = '';
  $('chooseBestBtn').disabled = true;
  $('forceNovelBtn').disabled = true;
  resetAdjudicationUI();
}

function renderBest(best) {
  const label = best.canonical_label || '(no canonical label)';
  $('bestGuess').textContent = `${label} | ${best.cluster_id} (${best.decision_band}, ${Number(best.score || 0).toFixed(3)})`;
  $('chooseBestBtn').disabled = false;

  chosenClusterId = best.cluster_id;
  $('chosenCluster').textContent = chosenClusterId;
  $('adjudicateBtn').disabled = !chosenClusterId;
}

function renderAlternates(alts) {
  const host = $('alternates');
  host.innerHTML = '';
  for (const a of alts || []) {
    const b = document.createElement('button');
    const lbl = a.canonical_label ? `${a.canonical_label}` : a.cluster_id;
    b.textContent = `${lbl} (${Number(a.score || 0).toFixed(3)})`;
    b.onclick = () => {
      chosenClusterId = a.cluster_id;
      $('chosenCluster').textContent = chosenClusterId;
      $('adjudicateBtn').disabled = !chosenClusterId;
      $('adjudicateStatus').textContent = 'Selected alternate for adjudication.';
    };
    host.appendChild(b);
  }
}

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

async function resolveGuess(text, query_domain) {
  const j = await post('/api/funnel/resolve_guess', {
    text,
    query_domain,
    thresholds_version: 'v1',
    limit: 8,
  });
  setStatus({ endpoint: 'resolve_guess', response: j });
  if (!j || !j.ok || !j.best) {
    $('adjudicateStatus').textContent = 'resolve_guess failed or unexpected shape.';
    return;
  }

  renderBest(j.best);
  renderAlternates(j.alternates || []);

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

$('chooseBestBtn').onclick = () => {
  $('adjudicateStatus').textContent = 'Best guess selected for adjudication.';
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
