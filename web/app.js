let mode = 'class';
let selectedNpc = null;
const $ = (id)=>document.getElementById(id);
for (const b of document.querySelectorAll('.modeBtn')) b.onclick=()=>{mode=b.dataset.mode; $('stepTitle').textContent = mode==='dialogue' ? 'Dialogue Step A: Who do you want to talk to and why' : `Search ${mode}`; $('results').innerHTML='';};

async function post(url,p){const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(p)}); return r.json();}

function renderProposalLabel(p, i) {
  const pj = p.proposal_json || {};
  if (pj.mode === 'candidate_set') {
    return pj.name || p.proposal_title || ('Option '+(i+1));
  }
  if (pj.mode === 'dialogue_options') {
    const intent = pj.intent_tag ? ` [${pj.intent_tag}]` : '';
    return `${pj.utterance || p.proposal_title || ('Option '+(i+1))}${intent}`;
  }
  return p.proposal_title || ('Option '+(i+1));
}

$('searchBtn').onclick = async ()=>{
  const text=$('text').value;
  const qd = mode==='class'?'class':mode==='skill'?'skill':'npc_intent';
  const j = await post('/api/funnel/resolve',{text,query_domain:qd});
  $('status').textContent = JSON.stringify(j,null,2);
  $('results').innerHTML = (j.similar||[]).map(x=>`<button class='pick' data-id='${x.query_id}'>prior #${x.query_id} (${x.score.toFixed(3)})</button>`).join(' ');
};

$('otherBtn').onclick = async ()=>{
  const text=$('text').value;
  const qd = mode==='class'?'class':mode==='skill'?'skill':(selectedNpc?'dialogue_option':'npc_intent');
  const j = await post('/api/funnel/bootstrap',{text,query_domain:qd,llm_mode:$('llmMode').value});
  $('status').textContent = JSON.stringify(j,null,2);
  const proposals = j.proposals||[];
  $('results').innerHTML = proposals.map((p,i)=>`<button class='proposal' data-i='${i}'>${renderProposalLabel(p,i)}</button>`).join(' ');
  for (const el of document.querySelectorAll('.proposal')) {
    el.onclick = async ()=>{
      const p = proposals[Number(el.dataset.i)];
      const pj = p.proposal_json || {};
      if (mode==='dialogue' && !selectedNpc && pj.mode === 'candidate_set') {
        selectedNpc = { npc_name: pj.name || p.proposal_title || 'Unknown', npc_role: pj.role || 'Unknown', query_id: j.query_id };
        await post('/api/dialogue/npc/select', selectedNpc);
        $('stepTitle').textContent = 'Dialogue Step B: What do you want to say';
        $('status').textContent = 'NPC selected. Enter dialogue intent and click Other/IDK.';
      } else if (pj.mode === 'dialogue_options') {
        $('status').textContent = JSON.stringify({selected:p.proposal_id,chosen_utterance:pj,proposal:p},null,2);
      } else {
        $('status').textContent = JSON.stringify({selected:p.proposal_id,proposal:p},null,2);
      }
    };
  }
};
