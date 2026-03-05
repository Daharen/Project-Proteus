let mode = 'class';
let selectedNpc = null;

let lastGuess = null;
let chosenClusterId = '';
let synonymQueue = [];

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

function setStatus(obj) {
  $('status').textContent = JSON.stringify(obj, null, 2);
}

function clearRecognition() {
  lastGuess = null;
  chosenClusterId = '';
  synonymQueue = [];
  $('bestGuess').textContent = '';
  $('alternates').innerHTML = '';
  $('chosenCluster').textContent = 'None';
  $('synQueue').textContent = '';
  $('adjudicateStatus').textContent = '';
  $('chooseBestBtn').disabled = true;
  $('forceNovelBtn').disabled = true;
  $('adjudicateBtn').disabled = true;
}

function renderBest(best) {
  $('bestGuess').textContent = `${best.cluster_id} (${best.decision_band}, ${Number(best.score || 0).toFixed(3)})`;
  $('chooseBestBtn').disabled = false;
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
  for (let i = 0; i < synonymQueue.length; i++) {
    const item = synonymQueue[i];
    const row = document.createElement('div');
    row.className = 'row';
    const span = document.createElement('span');
    span.textContent = `${item.term} → ${item.canonical_term}`;
    const rm = document.createElement('button');
    rm.textContent = 'Remove';
    rm.onclick = () => {
      synonymQueue.splice(i, 1);
      renderSynQueue();
    };
    row.appendChild(span);
    row.appendChild(rm);
    host.appendChild(row);
  }
}

for (const b of document.querySelectorAll('.modeBtn')) {
  b.onclick = () => {
    mode = b.dataset.mode;
    selectedNpc = null;
    $('stepTitle').textContent = mode === 'dialogue'
      ? 'Dialogue Step A: Who do you want to talk to and why'
      : `Search ${mode}`;
    $('results').innerHTML = '';
    clearRecognition();
  };
}

$('searchBtn').onclick = async () => {
  clearRecognition();

  const text = $('text').value;
  const qd = domainForMode();

  const j = await post('/api/funnel/resolve_guess', {
    text,
    query_domain: qd,
    thresholds_version: 'v1',
    alternates_limit: 5,
  });

  lastGuess = j;
  setStatus(j);

  if (!j || !j.ok) {
    $('adjudicateStatus').textContent = 'resolve_guess failed.';
    return;
  }

  renderBest(j.best);
  renderAlternates(j.alternates);

  $('forceNovelBtn').disabled = !(j.force_novel_available === true);
  $('forceNovelBtn').onclick = async () => {
    const boot = await post('/api/funnel/bootstrap', {
      text,
      query_domain: qd,
      llm_mode: $('llmMode').value,
      thresholds_version: 'v1',
    });
    setStatus(boot);
  };

  $('chooseBestBtn').onclick = () => {
    chosenClusterId = j.best.cluster_id;
    $('chosenCluster').textContent = chosenClusterId;
    $('adjudicateBtn').disabled = !chosenClusterId;
    $('adjudicateStatus').textContent = 'Best guess selected for adjudication.';
  };
};

$('otherBtn').onclick = async () => {
  clearRecognition();

  const text = $('text').value;
  const qd = domainForMode();

  const j = await post('/api/funnel/bootstrap', {
    text,
    query_domain: qd,
    llm_mode: $('llmMode').value,
    thresholds_version: 'v1',
  });
  setStatus(j);
};

$('addSynBtn').onclick = () => {
  const term = ($('synTerm').value || '').trim();
  const canon = ($('synCanon').value || '').trim();
  if (!term || !canon) {
    $('adjudicateStatus').textContent = 'Synonym term and canonical are required.';
    return;
  }
  synonymQueue.push({ term, canonical_term: canon });
  $('synTerm').value = '';
  $('synCanon').value = '';
  renderSynQueue();
  $('adjudicateStatus').textContent = 'Synonym mapping queued.';
};

$('adjudicateBtn').onclick = async () => {
  const text = $('text').value;
  const qd = domainForMode();

  if (!chosenClusterId) {
    $('adjudicateStatus').textContent = 'Choose a cluster first.';
    return;
  }

  const payload = {
    text,
    query_domain: qd,
    chosen_cluster_id: chosenClusterId,
    mapping_version: 1,
    synonyms: synonymQueue.slice(),
  };

  const j = await post('/api/funnel/adjudicate', payload);
  setStatus(j);

  if (j && j.ok) {
    $('adjudicateStatus').textContent = `Adjudicated. alias_written=${j.alias_written} synonyms_written=${j.synonyms_written}`;
  } else {
    $('adjudicateStatus').textContent = 'Adjudication failed.';
  }
};

clearRecognition();
renderSynQueue();
