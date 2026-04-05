const canvas = document.getElementById('worldCanvas');
const ctx = canvas.getContext('2d');
const summary = document.getElementById('summary');
const editor = document.getElementById('editor');
const selectionHeader = document.getElementById('selectionHeader');
const runStatus = document.getElementById('runStatus');
const playerStatus = document.getElementById('playerStatus');
const interactionStatus = document.getElementById('interactionStatus');

const PLAY_INTERVAL_MS = 100;

let world = null;
let selection = null;
let runTimer = null;
const keyState = { w: false, a: false, s: false, d: false, e: false };

async function post(url, payload = {}) {
  const r = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  });
  return r.json();
}

async function getState() {
  const r = await fetch('/api/sandbox/state');
  world = await r.json();
  render();
}

function worldToCanvas(x, y) {
  const scaleX = canvas.width / world.map.width;
  const scaleY = canvas.height / world.map.height;
  return { x: x * scaleX, y: y * scaleY };
}

function hitTest(mx, my) {
  if (!world) return null;
  const wx = (mx / canvas.width) * world.map.width;
  const wy = (my / canvas.height) * world.map.height;

  for (const agent of world.agents) {
    const dx = agent.x - wx;
    const dy = agent.y - wy;
    if ((dx * dx + dy * dy) <= (agent.radius * agent.radius)) return { type: 'agent', id: agent.agent_id };
  }
  for (const object of world.objects) {
    const dx = object.x - wx;
    const dy = object.y - wy;
    if ((dx * dx + dy * dy) <= ((object.radius + 3) * (object.radius + 3))) return { type: 'object', id: object.object_id };
  }
  return null;
}

function drawArrow(px, py, r, angle, color) {
  const tip = { x: px + Math.cos(angle) * r * 1.6, y: py + Math.sin(angle) * r * 1.6 };
  const left = { x: px + Math.cos(angle + 2.6) * r * 0.7, y: py + Math.sin(angle + 2.6) * r * 0.7 };
  const right = { x: px + Math.cos(angle - 2.6) * r * 0.7, y: py + Math.sin(angle - 2.6) * r * 0.7 };

  ctx.fillStyle = color;
  ctx.beginPath();
  ctx.moveTo(tip.x, tip.y);
  ctx.lineTo(left.x, left.y);
  ctx.lineTo(right.x, right.y);
  ctx.closePath();
  ctx.fill();
}

function selectedLabel(text, p, radiusPx) {
  ctx.fillStyle = '#f8fafc';
  ctx.font = '12px Inter, Arial, sans-serif';
  ctx.fillText(text, p.x + radiusPx + 8, p.y - radiusPx - 8);
}

function getPlayerAgent() {
  if (!world) return null;
  return world.agents.find((a) => a.is_player_controlled) || null;
}

function updateStatusRow() {
  const player = getPlayerAgent();
  runStatus.textContent = `Status: ${runTimer ? 'Running' : 'Paused'}`;
  playerStatus.textContent = player ? `Player: #${player.agent_id} (${player.label})` : 'Player: n/a';
  interactionStatus.textContent = player && player.last_interaction_result
    ? `Last interaction: ${player.last_interaction_result}`
    : 'Last interaction: none';
}

function render() {
  if (!world) return;

  if (selection?.type === 'agent' && !world.agents.some((a) => a.agent_id === selection.id)) {
    selection = null;
  }
  if (selection?.type === 'object' && !world.objects.some((o) => o.object_id === selection.id)) {
    selection = null;
  }

  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.strokeStyle = '#475569';
  ctx.strokeRect(0, 0, canvas.width, canvas.height);

  for (const obstacle of world.obstacles || []) {
    const p = worldToCanvas(obstacle.x, obstacle.y);
    const scaleX = canvas.width / world.map.width;
    const scaleY = canvas.height / world.map.height;
    ctx.fillStyle = 'rgba(148,163,184,0.25)';
    ctx.fillRect(p.x, p.y, obstacle.w * scaleX, obstacle.h * scaleY);
  }

  for (const object of world.objects) {
    const p = worldToCanvas(object.x, object.y);
    const radiusPx = object.radius * (canvas.width / world.map.width);
    ctx.fillStyle = object.color_hex;
    ctx.beginPath();
    ctx.arc(p.x, p.y, radiusPx, 0, Math.PI * 2);
    ctx.fill();

    if (selection && selection.type === 'object' && selection.id === object.object_id) {
      ctx.strokeStyle = '#f8fafc';
      ctx.lineWidth = 3;
      ctx.beginPath();
      ctx.arc(p.x, p.y, object.interaction_radius * (canvas.width / world.map.width), 0, Math.PI * 2);
      ctx.stroke();
      selectedLabel(`selected object #${object.object_id}`, p, radiusPx);
    }
  }

  for (const agent of world.agents) {
    const p = worldToCanvas(agent.x, agent.y);
    const radiusPx = agent.radius * (canvas.width / world.map.width);
    ctx.fillStyle = agent.color_hex;
    ctx.beginPath();
    ctx.arc(p.x, p.y, radiusPx, 0, Math.PI * 2);
    ctx.fill();
    drawArrow(p.x, p.y, radiusPx, agent.facing_radians, '#111827');

    if (agent.is_player_controlled) {
      ctx.strokeStyle = '#fbbf24';
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.arc(p.x, p.y, radiusPx + 6, 0, Math.PI * 2);
      ctx.stroke();
      ctx.fillStyle = '#fbbf24';
      ctx.font = 'bold 11px Inter, Arial, sans-serif';
      ctx.fillText('PLAYER', p.x - 20, p.y - radiusPx - 10);
    }

    if (selection && selection.type === 'agent' && selection.id === agent.agent_id) {
      ctx.strokeStyle = '#f8fafc';
      ctx.lineWidth = 3;
      ctx.beginPath();
      ctx.arc(p.x, p.y, radiusPx + 10, 0, Math.PI * 2);
      ctx.stroke();
      selectedLabel(`selected agent #${agent.agent_id}`, p, radiusPx);
    }
  }

  updateStatusRow();
  renderInspector();
}

function numberInput(label, key, value) {
  const safe = Number(value).toFixed(3);
  return `<label>${label}<input type="number" step="0.05" name="${key}" value="${safe}" /></label>`;
}

function renderInspector() {
  if (!world) return;

  if (!selection) {
    selectionHeader.textContent = 'Selected: No selection';
    summary.textContent = `tick=${world.tick}\nselection: none\nSelect an agent or object to inspect/edit.`;
    editor.innerHTML = '';
    return;
  }

  if (selection.type === 'agent') {
    const agent = world.agents.find((a) => a.agent_id === selection.id);
    if (!agent) {
      selection = null;
      renderInspector();
      return;
    }

    selectionHeader.textContent = `Selected: Agent #${agent.agent_id}`;
    summary.textContent = [
      `tick=${world.tick}`,
      `selection: agent ${agent.agent_id}`,
      `agent=${agent.label} (#${agent.agent_id})`,
      `player_controlled=${agent.is_player_controlled}`,
      `pos=(${agent.x.toFixed(2)}, ${agent.y.toFixed(2)}) facing=${agent.facing_radians.toFixed(3)}`,
      `last_action=${agent.last_action}`,
      `target_object_id=${agent.target_object_id} target_agent_id=${agent.target_agent_id}`,
      `influence_summary=${agent.influence_summary}`,
      `last_interaction=${agent.last_interaction || 'none'}`,
      `last_interaction_result=${agent.last_interaction_result || 'none'}`,
    ].join('\n');

    editor.innerHTML = `
      <h3>Agent semantic attachments ${agent.is_player_controlled ? '(player-controlled)' : ''}</h3>
      <form id="agentForm">
        ${numberInput('motivation', 'motivation', agent.semantic.motivation)}
        ${numberInput('affect', 'affect', agent.semantic.affect)}
        ${numberInput('temperament', 'temperament', agent.semantic.temperament)}
        ${numberInput('trust', 'trust', agent.semantic.trust)}
        ${numberInput('fear', 'fear', agent.semantic.fear)}
        ${numberInput('loyalty', 'loyalty', agent.semantic.loyalty)}
        <h4>Behavior weights</h4>
        ${numberInput('seek_interest_weight', 'seek_interest_weight', agent.behavior.seek_interest_weight)}
        ${numberInput('avoid_threat_weight', 'avoid_threat_weight', agent.behavior.avoid_threat_weight)}
        ${numberInput('ally_pull_weight', 'ally_pull_weight', agent.behavior.ally_pull_weight)}
        ${numberInput('rival_repulsion_weight', 'rival_repulsion_weight', agent.behavior.rival_repulsion_weight)}
        ${numberInput('idle_wander_weight', 'idle_wander_weight', agent.behavior.idle_wander_weight)}
        <button type="submit">Apply Agent Update</button>
      </form>
    `;

    document.getElementById('agentForm').onsubmit = async (e) => {
      e.preventDefault();
      const f = new FormData(e.target);
      const payload = {
        agent_id: agent.agent_id,
        semantic: {
          motivation: Number(f.get('motivation')),
          affect: Number(f.get('affect')),
          temperament: Number(f.get('temperament')),
          trust: Number(f.get('trust')),
          fear: Number(f.get('fear')),
          loyalty: Number(f.get('loyalty')),
        },
        behavior: {
          seek_interest_weight: Number(f.get('seek_interest_weight')),
          avoid_threat_weight: Number(f.get('avoid_threat_weight')),
          ally_pull_weight: Number(f.get('ally_pull_weight')),
          rival_repulsion_weight: Number(f.get('rival_repulsion_weight')),
          idle_wander_weight: Number(f.get('idle_wander_weight')),
        },
      };
      world = await post('/api/sandbox/agent/update', payload);
      render();
    };
    return;
  }

  const object = world.objects.find((o) => o.object_id === selection.id);
  if (!object) {
    selection = null;
    renderInspector();
    return;
  }

  selectionHeader.textContent = `Selected: Object #${object.object_id}`;
  summary.textContent = [
    `tick=${world.tick}`,
    `selection: object ${object.object_id}`,
    `object=${object.label} (#${object.object_id}) kind=${object.kind}`,
    `pos=(${object.x.toFixed(2)}, ${object.y.toFixed(2)})`,
  ].join('\n');

  editor.innerHTML = `
    <h3>Object affordances</h3>
    <form id="objectForm">
      ${numberInput('interest_tag', 'interest_tag', object.interest_tag)}
      ${numberInput('threat_tag', 'threat_tag', object.threat_tag)}
      ${numberInput('social_tag', 'social_tag', object.social_tag)}
      ${numberInput('resource_tag', 'resource_tag', object.resource_tag)}
      ${numberInput('interaction_radius', 'interaction_radius', object.interaction_radius)}
      <button type="submit">Apply Object Update</button>
    </form>
  `;

  document.getElementById('objectForm').onsubmit = async (e) => {
    e.preventDefault();
    const f = new FormData(e.target);
    world = await post('/api/sandbox/object/update', {
      object_id: object.object_id,
      interest_tag: Number(f.get('interest_tag')),
      threat_tag: Number(f.get('threat_tag')),
      social_tag: Number(f.get('social_tag')),
      resource_tag: Number(f.get('resource_tag')),
      interaction_radius: Number(f.get('interaction_radius')),
    });
    render();
  };
}

function playerInputPayload() {
  const moveX = (keyState.d ? 1 : 0) + (keyState.a ? -1 : 0);
  const moveY = (keyState.s ? 1 : 0) + (keyState.w ? -1 : 0);
  return {
    move_x: moveX,
    move_y: moveY,
    interact: keyState.e,
  };
}

async function stepWorld(steps = 1) {
  world = await post('/api/sandbox/step', {
    steps,
    player_input: playerInputPayload(),
  });
  render();
}

function startRunLoop() {
  if (runTimer) return;
  runTimer = setInterval(() => {
    stepWorld(1);
  }, PLAY_INTERVAL_MS);
  updateStatusRow();
}

function stopRunLoop() {
  if (!runTimer) return;
  clearInterval(runTimer);
  runTimer = null;
  updateStatusRow();
}

canvas.addEventListener('click', (e) => {
  const rect = canvas.getBoundingClientRect();
  selection = hitTest(e.clientX - rect.left, e.clientY - rect.top);
  render();
});

document.getElementById('resetBtn').onclick = async () => {
  stopRunLoop();
  world = await post('/api/sandbox/reset');
  selection = null;
  render();
};

document.getElementById('stepBtn').onclick = async () => {
  await stepWorld(1);
};

document.getElementById('step10Btn').onclick = async () => {
  await stepWorld(10);
};

document.getElementById('playBtn').onclick = () => {
  startRunLoop();
};

document.getElementById('pauseBtn').onclick = () => {
  stopRunLoop();
};

const keyMap = {
  w: 'w',
  a: 'a',
  s: 's',
  d: 'd',
  e: 'e',
};

function handleKey(event, isDown) {
  const key = event.key.toLowerCase();
  if (!Object.prototype.hasOwnProperty.call(keyMap, key)) {
    return;
  }
  event.preventDefault();
  keyState[keyMap[key]] = isDown;
}

window.addEventListener('keydown', (event) => handleKey(event, true));
window.addEventListener('keyup', (event) => handleKey(event, false));

window.addEventListener('blur', () => {
  for (const key of Object.keys(keyState)) {
    keyState[key] = false;
  }
});

getState();
