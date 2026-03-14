import createSolverModule from './solver.js';

const MOVE_NAMES = ['LEFT', 'RIGHT', 'UP', 'DOWN'];
const boardEl = document.getElementById('board');
const boardShellEl = document.getElementById('boardShell');
const moveLayerEl = document.getElementById('moveLayer');
const statusEl = document.getElementById('status');
const chosenMoveEl = document.getElementById('chosenMove');
const perfEl = document.getElementById('perf');
const selectedDirectionArrowEl = document.getElementById('selectedDirectionArrow');
const selectedDirectionTextEl = document.getElementById('selectedDirectionText');
const scoreWeightInputEl = document.getElementById('scoreWeightInput');
const movePenaltyInputEl = document.getElementById('movePenaltyInput');
const tilePickerEl = document.getElementById('tilePicker');
const tileAfterMoveEl = document.getElementById('tileAfterMove');
const btnAnalyze = document.getElementById('btnAnalyze');
const btnStep = document.getElementById('btnStep');
const btnReset = document.getElementById('btnReset');
const btnMode = document.getElementById('btnMode');
const toggleAutoStepAfterSpawn = document.getElementById('toggleAutoStepAfterSpawn');
const scoresEl = document.getElementById('scores');

const state = {
  lastBestMove: -1,
  board: Array(16).fill(0),
  pendingBoard: null,
  waitingForSpawn: false,
  setupMode: true,
  selectedTile: 1,
  module: null,
  analyze: null,
  autoStepAfterSpawn: true,
  scoreWeight: 1.0,
  movePenalty: 5000.0,
  timeLimitMs: 70,
  animationMs: 260,
  animating: false,
  hiddenIndices: null,
  lang: localStorage.getItem('qtl_lang') || 'ar',
};

const TRANSLATIONS = {
  en: {
    title: 'QTL K172',
    description: 'C++ expectimax solver compiled to WebAssembly. The board uses exponent-style values (1, 2, 3...) where merging equal tiles increments the value.',
    statusTitle: 'Status:',
    statusLoading: 'Loading C++ solver…',
    statusReady: 'Ready. Build a board, then analyze or step.',
    statusEmpty: 'Board is empty. Place at least one tile first.',
    statusNoMove: 'No legal move available.',
    statusSpawnedManual: 'Spawn placed. Analyze again or step the next move.',
    statusSpawnedAuto: 'Spawn placed. Automatically analyzing and playing the next move…',
    statusApplied: 'Applied {move}. Now place the next tile.',
    statusWaitingSpawn: 'Choose the next tile location first.',
    statusAnalysis: 'Analysis complete in {time} ms.',
    statusCleared: 'Board cleared.',
    statusSetupMode: 'Setup mode: click cells to toggle the selected tile.',
    statusPlayMode: 'Play mode: use Analyze / Step, then place the spawned tile manually.',
    emptiesLabel: 'Empties:',
    tilePickerTitle: 'Tile picker',
    btnModePlay: 'Switch to Play Mode',
    btnModeSetup: 'Switch to Setup Mode',
    btnAnalyze: 'Analyze',
    btnStep: 'Step best move',
    btnReset: 'Reset',
    toggleAuto: 'After placing the next tile, automatically analyze and play the next move',
    nextTileTitle: 'Next tile after move',
    analysisTitle: 'Analysis',
    chosenMoveTitle: 'Chosen move:',
    playDirectionLabel: 'Play this direction',
    waiting: 'Waiting',
    searchLabel: 'Search:',
    depthLabel: 'Depth',
    nodesLabel: 'nodes',
    cacheLabel: 'cache hits',
    partialLabel: 'partial',
    noneMove: 'NONE',
    naScore: 'N/A',
    notesTitle: 'Notes',
    notes1: 'Setup mode lets you build any board position by clicking cells. In play mode, Step best move applies the solver\'s move, then waits for you to place the next tile manually.',
    notes2: 'Under the hood the solver uses bitboards, precomputed transitions, expectimax, a transposition table, move ordering, and a strong heuristic (gradient, smoothness, monotonicity, merge potential, corner anchoring, snake bonus, and dedicated endgame scoring).',
    scoreWeightLabel: 'SCORE_WEIGHT',
    movePenaltyLabel: 'MOVE_PENALTY',
    timeLimitLabel: 'TIME_LIMIT_MS',
    configHelp: 'These values are applied immediately on Analyze and Step best move. Setting TIME_LIMIT_MS to 0 disables the time limit.',
    move0: 'LEFT',
    move1: 'RIGHT',
    move2: 'UP',
    move3: 'DOWN',
    none: 'NONE',
    na: 'N/A',
    langToggle: 'العربية'
  },
  ar: {
    title: 'QTL K172',
    description: 'محرك حل 2048 بلغة C++ تم تجميعه إلى WebAssembly. تستخدم اللوحة قيماً تعتمد على الأس (1, 2, 3...) حيث يؤدي دمج البلاطات المتساوية لزيادة القيمة بمقدار واحد.',
    statusTitle: 'الحالة:',
    statusLoading: 'جاري تحميل محرك C++...',
    statusReady: 'جاهز. قم ببناء اللوحة، ثم اضغط تحليل أو خطوة.',
    statusEmpty: 'اللوحة فارغة. ضع بلاطة واحدة على الأقل أولاً.',
    statusNoMove: 'لا توجد حركة قانونية متاحة.',
    statusSpawnedManual: 'تم وضع البلاطة. قم بالتحليل مجدداً أو اتخذ الخطوة التالية.',
    statusSpawnedAuto: 'تم وضع البلاطة. جاري التحليل واللعب تلقائياً...',
    statusApplied: 'تم تنفيذ حركة {move}. الآن ضع البلاطة التالية.',
    statusWaitingSpawn: 'اختر موقع البلاطة التالية أولاً.',
    statusAnalysis: 'اكتمل التحليل في {time} مللي ثانية.',
    statusCleared: 'تم مسح اللوحة.',
    statusSetupMode: 'وضع الإعداد: انقر على الخلايا لتبديل البلاطة المختارة.',
    statusPlayMode: 'وضع اللعب: استخدم تحليل / خطوة، ثم ضع البلاطة الناتجة يدوياً.',
    emptiesLabel: 'الفراغات:',
    tilePickerTitle: 'منتقي البلاطات',
    btnModePlay: 'التبديل لوضع اللعب',
    btnModeSetup: 'التبديل لوضع الإعداد',
    btnAnalyze: 'تحليل',
    btnStep: 'تنفيذ أفضل حركة',
    btnReset: 'إعادة ضبط',
    toggleAuto: 'بعد وضع البلاطة التالية، قم بالتحليل واللعب تلقائياً',
    nextTileTitle: 'البلاطة التالية بعد الحركة',
    analysisTitle: 'التحليل',
    chosenMoveTitle: 'الحركة المختارة:',
    playDirectionLabel: 'العب في هذا الاتجاه',
    waiting: 'بانتظار التحليل',
    searchLabel: 'البحث:',
    depthLabel: 'العمق',
    nodesLabel: 'عقدة',
    cacheLabel: 'مرات استرجاع الذاكرة',
    partialLabel: 'جزئي',
    noneMove: 'لا يوجد',
    naScore: 'غير متاح',
    notesTitle: 'ملاحظات',
    notes1: 'يسمح لك وضع الإعداد ببناء أي موضع للوحة عن طريق النقر على الخلايا. في وضع اللعب، تقوم "خطوة" بتطبيق حركة المحلل، ثم تنتظر منك وضع البلاطة التالية يدوياً.',
    notes2: 'يستخدم المحلل داخلياً bitboards، وانتقالات محسوبة مسبقاً، و expectimax، وجدول تبديل، وترتيب الحركات، ومعادلات تقييم قوية (الجاذبية، السلاسة، الرتابة، احتمالية الدمج، قفل الأركان، مكافأة الثعبان، وتقييم مخصص لنهاية اللعبة).',
    scoreWeightLabel: 'وزن النتيجة',
    movePenaltyLabel: 'عقوبة الحركة',
    timeLimitLabel: 'وقت البحث (مللي ثانية)',
    configHelp: 'يتم تطبيق هذه القيم فوراً عند التحليل أو تنفيذ الخطوة. تعيين القيمة إلى 0 يلغي الحد الزمني.',
    move0: 'يسار',
    move1: 'يمين',
    move2: 'فوق',
    move3: 'تحت',
    none: 'لا يوجد',
    na: 'غير متاح',
    langToggle: 'English'
  }
};

function updateLanguage(lang) {
  state.lang = lang;
  localStorage.setItem('qtl_lang', lang);
  document.documentElement.lang = lang;
  document.documentElement.dir = (lang === 'ar' ? 'rtl' : 'ltr');
  
  const strings = TRANSLATIONS[lang];
  document.querySelectorAll('[data-i18n]').forEach(el => {
    const key = el.getAttribute('data-i18n');
    if (strings[key]) {
      el.textContent = strings[key];
    }
  });

  // Special cases for buttons and status
  btnMode.textContent = state.setupMode ? strings.btnModePlay : strings.btnModeSetup;
  if (!state.board.some(Boolean)) {
    statusEl.textContent = strings.statusReady;
  }
}

function moveArrow(move) {
  switch (move) {
    case 0: return '←';
    case 1: return '→';
    case 2: return '↑';
    case 3: return '↓';
    default: return '—';
  }
}

function renderSelectedDirection(move) {
  state.lastBestMove = move;
  const strings = TRANSLATIONS[state.lang];
  if (!selectedDirectionArrowEl || !selectedDirectionTextEl) return;
  if (move >= 0) {
    selectedDirectionArrowEl.textContent = moveArrow(move);
    selectedDirectionTextEl.textContent = strings[`move${move}`] || MOVE_NAMES[move];
  } else {
    selectedDirectionArrowEl.textContent = '—';
    selectedDirectionTextEl.textContent = strings.waiting;
  }
}

function displayValue(exp) {
  return exp === 0 ? '' : String(exp);
}

function queueAutoStepAfterSpawn() {
  window.setTimeout(async () => {
    if (state.setupMode) return;
    if (state.waitingForSpawn) return;
    if (!state.board.some(Boolean)) return;
    await stepBestMove();
  }, 0);
}

function emptyCount(board) {
  return board.filter(v => v === 0).length;
}

function nextFrame() {
  return new Promise(resolve => requestAnimationFrame(() => resolve()));
}

function sleep(ms) {
  return new Promise(resolve => window.setTimeout(resolve, ms));
}

function render() {
  const source = state.pendingBoard ?? state.board;
  boardEl.innerHTML = '';
  for (let i = 0; i < 16; i++) {
    const cell = document.createElement('button');
    cell.className = 'cell';
    cell.type = 'button';
    const value = source[i];
    if (value) cell.classList.add(`v${Math.min(value, 12)}`);
    cell.textContent = displayValue(value);
    cell.setAttribute('aria-label', value ? `Tile ${value}` : 'Empty cell');
    if (state.hiddenIndices?.has(i)) {
      cell.classList.add('hidden-source');
    }
    cell.addEventListener('click', () => onCellClick(i));
    boardEl.appendChild(cell);
  }
  if (moveLayerEl) moveLayerEl.innerHTML = '';
  document.getElementById('empties').textContent = String(emptyCount(state.board));
}

function setStatus(key, data = {}) {
  const strings = TRANSLATIONS[state.lang];
  let text = strings[key] || key;
  for (const k in data) {
    let val = data[k];
    if (k === 'move') {
      const idx = MOVE_NAMES.indexOf(val);
      if (idx !== -1 && strings[`move${idx}`]) val = strings[`move${idx}`];
    }
    text = text.replace(`{${k}}`, val);
  }
  statusEl.textContent = text;
}

function makeTilePicker(targetEl) {
  targetEl.innerHTML = '';
  for (let value = 1; value <= 12; value++) {
    const btn = document.createElement('button');
    btn.textContent = String(value);
    btn.type = 'button';
    btn.className = 'pick';
    if (value === state.selectedTile) btn.classList.add('active');
    btn.addEventListener('click', () => {
      state.selectedTile = value;
      syncTilePickers();
    });
    targetEl.appendChild(btn);
  }
}

function syncTilePickers() {
  for (const container of [tilePickerEl, tileAfterMoveEl]) {
    [...container.children].forEach((child, idx) => {
      child.classList.toggle('active', idx + 1 === state.selectedTile);
      child.setAttribute('aria-pressed', idx + 1 === state.selectedTile ? 'true' : 'false');
    });
  }
}

function cloneBoard(board) {
  return board.slice();
}

function compressLine(line) {
  const arr = line.filter(v => v !== 0);
  let score = 0;
  for (let i = 0; i < arr.length - 1; i++) {
    if (arr[i] === arr[i + 1] && arr[i] < 12) {
      arr[i]++;
      arr[i + 1] = 0;
      score += arr[i];
    }
  }
  const compact = arr.filter(v => v !== 0);
  while (compact.length < 4) compact.push(0);
  return { line: compact, score };
}

function rows(board) {
  return [0, 1, 2, 3].map(r => board.slice(r * 4, r * 4 + 4));
}

function fromRows(r) {
  return r.flat();
}

function transpose(board) {
  const out = Array(16).fill(0);
  for (let r = 0; r < 4; r++) for (let c = 0; c < 4; c++) out[c * 4 + r] = board[r * 4 + c];
  return out;
}

function reverseRows(board) {
  return fromRows(rows(board).map(r => r.slice().reverse()));
}

function moveLeft(board) {
  const rs = rows(board);
  let moved = false;
  let score = 0;
  const out = rs.map(r => {
    const before = r.join(',');
    const res = compressLine(r);
    if (res.line.join(',') !== before) moved = true;
    score += res.score;
    return res.line;
  });
  return { board: fromRows(out), moved, score };
}

function moveRight(board) {
  const rev = reverseRows(board);
  const res = moveLeft(rev);
  return { board: reverseRows(res.board), moved: res.moved, score: res.score };
}

function moveUp(board) {
  const t = transpose(board);
  const res = moveLeft(t);
  return { board: transpose(res.board), moved: res.moved, score: res.score };
}

function moveDown(board) {
  const t = transpose(board);
  const rev = reverseRows(t);
  const res = moveLeft(rev);
  return { board: transpose(reverseRows(res.board)), moved: res.moved, score: res.score };
}

const MOVE_FNS = [moveLeft, moveRight, moveUp, moveDown];

function lineIndices(move, line) {
  switch (move) {
    case 0: return [line * 4, line * 4 + 1, line * 4 + 2, line * 4 + 3];
    case 1: return [line * 4 + 3, line * 4 + 2, line * 4 + 1, line * 4];
    case 2: return [line, line + 4, line + 8, line + 12];
    case 3: return [line + 12, line + 8, line + 4, line];
    default: return [];
  }
}

function buildMoveAnimation(board, move) {
  const instructions = [];
  const destinationCounts = new Map();

  for (let line = 0; line < 4; line++) {
    const indices = lineIndices(move, line);
    const tiles = indices
      .map((idx) => ({ idx, value: board[idx] }))
      .filter((tile) => tile.value !== 0);

    let target = 0;
    for (let i = 0; i < tiles.length; i++) {
      const current = tiles[i];
      const next = tiles[i + 1];
      const to = indices[target];

      if (next && current.value === next.value && current.value < 12) {
        instructions.push({ from: current.idx, to, value: current.value });
        instructions.push({ from: next.idx, to, value: next.value });
        destinationCounts.set(to, (destinationCounts.get(to) || 0) + 2);
        i++;
      } else {
        instructions.push({ from: current.idx, to, value: current.value });
        destinationCounts.set(to, (destinationCounts.get(to) || 0) + 1);
      }

      target++;
    }
  }

  return instructions.filter((item) => item.from !== item.to || (destinationCounts.get(item.to) || 0) > 1);
}

async function animateMoveTransition(fromBoard, moveIndex) {
  if (!moveLayerEl || !boardShellEl) return;

  const instructions = buildMoveAnimation(fromBoard, moveIndex);
  if (!instructions.length) return;

  state.hiddenIndices = new Set(instructions.map((item) => item.from));
  render();
  await nextFrame();

  const cells = [...boardEl.children];
  const layerRect = moveLayerEl.getBoundingClientRect();
  const fragments = [];

  for (const item of instructions) {
    const fromRect = cells[item.from].getBoundingClientRect();
    const toRect = cells[item.to].getBoundingClientRect();
    const tile = document.createElement('div');
    tile.className = `moving-tile v${Math.min(item.value, 12)}`;
    tile.textContent = displayValue(item.value);
    tile.style.left = `${fromRect.left - layerRect.left}px`;
    tile.style.top = `${fromRect.top - layerRect.top}px`;
    tile.style.width = `${fromRect.width}px`;
    tile.style.height = `${fromRect.height}px`;
    tile.dataset.dx = String(toRect.left - fromRect.left);
    tile.dataset.dy = String(toRect.top - fromRect.top);
   fragments.push(tile);
    moveLayerEl.appendChild(tile);
  }

  await nextFrame();
  for (const tile of fragments) {
    tile.style.transform = `translate(${tile.dataset.dx}px, ${tile.dataset.dy}px)`;
  }
  await sleep(state.animationMs);
  moveLayerEl.innerHTML = '';
  state.hiddenIndices = null;
}

async function ensureSolver() {
  if (state.module) return;
  setStatus('statusLoading');
  state.module = await createSolverModule();
  state.analyze = (cells, maxDepth = 8, timeLimitMs = 70, scoreWeight = state.scoreWeight, movePenalty = state.movePenalty) => {
    const ptr = state.module.ccall(
      'analyze_board_json',
      'number',
      ['array', 'number', 'number', 'number', 'number'],
      [cells, maxDepth, timeLimitMs, scoreWeight, movePenalty]
    );
    return JSON.parse(state.module.UTF8ToString(ptr));
  };
  setStatus('statusReady');
}

function getRuntimeWeights() {
  const scoreWeightInputEl = document.getElementById('scoreWeightInput');
  const movePenaltyInputEl = document.getElementById('movePenaltyInput');
  const timeLimitMsInputEl = document.getElementById('timeLimitMsInput');

  const parsedScoreWeight = Number(scoreWeightInputEl?.value);
  const parsedMovePenalty = Number(movePenaltyInputEl?.value);
  const parsedTimeLimit = Number(timeLimitMsInputEl?.value);

  const scoreWeight = Number.isFinite(parsedScoreWeight) ? parsedScoreWeight : state.scoreWeight;
  const movePenalty = Number.isFinite(parsedMovePenalty) ? parsedMovePenalty : state.movePenalty;
  const timeLimitMs = Number.isFinite(parsedTimeLimit) && parsedTimeLimit >= 0 ? parsedTimeLimit : state.timeLimitMs;

  state.scoreWeight = scoreWeight;
  state.movePenalty = movePenalty;
  state.timeLimitMs = timeLimitMs;

  if (scoreWeightInputEl && scoreWeightInputEl.value !== String(scoreWeight)) {
    scoreWeightInputEl.value = String(scoreWeight);
  }
  if (movePenaltyInputEl && movePenaltyInputEl.value !== String(movePenalty)) {
    movePenaltyInputEl.value = String(movePenalty);
  }
  if (timeLimitMsInputEl && timeLimitMsInputEl.value !== String(timeLimitMs)) {
    timeLimitMsInputEl.value = String(timeLimitMs);
  }

  return { scoreWeight, movePenalty, timeLimitMs };
}

function refreshScores(data) {
  const strings = TRANSLATIONS[state.lang];
  scoresEl.innerHTML = '';
  for (const move of data.moves) {
    const row = document.createElement('div');
    row.className = 'score-row';
    const moveName = strings[`move${move.move}`] || MOVE_NAMES[move.move];
    row.innerHTML = `<span>${moveName}</span><span>${move.legal ? move.score.toFixed(2) : strings.naScore}</span>`;
    if (move.move === data.bestMove) row.classList.add('best');
    scoresEl.appendChild(row);
  }
  chosenMoveEl.textContent = data.bestMove >= 0 ? (strings[`move${data.bestMove}`] || MOVE_NAMES[data.bestMove]) : strings.noneMove;
  renderSelectedDirection(data.bestMove);
  perfEl.textContent = `${strings.depthLabel} ${data.depthCompleted} • ${data.nodes} ${strings.nodesLabel} • ${data.cacheHits} ${strings.cacheLabel}${data.timedOut ? ` • ${strings.partialLabel}` : ''}`;
}

async function analyzeBoard() {
  await ensureSolver();
  if (!state.board.some(Boolean)) {
    setStatus('statusEmpty');
    return null;
  }
  const { scoreWeight, movePenalty, timeLimitMs } = getRuntimeWeights();
  const started = performance.now();
  const result = state.analyze(Uint8Array.from(state.board), 8, timeLimitMs, scoreWeight, movePenalty);
  const elapsed = performance.now() - started;
  refreshScores(result);
  setStatus('statusAnalysis', { time: elapsed.toFixed(1) });
  return result;
}

async function stepBestMove() {
  if (state.animating) return;
  if (state.waitingForSpawn) {
    setStatus('statusWaitingSpawn');
    return;
  }
  const result = await analyzeBoard();
  if (!result || result.bestMove < 0) {
    setStatus('statusNoMove');
    return;
  }
  const move = MOVE_FNS[result.bestMove](state.board);
  state.animating = true;
  await animateMoveTransition(state.board, result.bestMove);
  state.pendingBoard = move.board;
  state.animating = false;
  state.waitingForSpawn = true;
  render();
  setStatus('statusApplied', { move: MOVE_NAMES[result.bestMove] });
}

function onCellClick(index) {
  if (state.setupMode) {
    if (state.animating) return;
    state.board[index] = state.board[index] === state.selectedTile ? 0 : state.selectedTile;
    render();
    return;
  }

  if (!state.waitingForSpawn || !state.pendingBoard) return;
  if (state.pendingBoard[index] !== 0) return;
  state.pendingBoard[index] = state.selectedTile;
  state.board = cloneBoard(state.pendingBoard);
  state.pendingBoard = null;
  state.waitingForSpawn = false;
  render();

  if (state.autoStepAfterSpawn) {
    setStatus('statusSpawnedAuto');
    queueAutoStepAfterSpawn();
  } else {
    setStatus('statusSpawnedManual');
  }
}

function resetBoard() {
  state.board = Array(16).fill(0);
  state.pendingBoard = null;
  state.waitingForSpawn = false;
  scoresEl.innerHTML = '';
  chosenMoveEl.textContent = '—';
  renderSelectedDirection(-1);
  state.hiddenIndices = null;
  perfEl.textContent = '—';
  setStatus('statusCleared');
  render();
}

function toggleMode() {
  state.setupMode = !state.setupMode;
  const strings = TRANSLATIONS[state.lang];
  btnMode.textContent = state.setupMode ? strings.btnModePlay : strings.btnModeSetup;
  setStatus(state.setupMode ? 'statusSetupMode' : 'statusPlayMode');
}

btnAnalyze.addEventListener('click', analyzeBoard);
btnStep.addEventListener('click', stepBestMove);
btnReset.addEventListener('click', resetBoard);
btnMode.addEventListener('click', toggleMode);

if (toggleAutoStepAfterSpawn) {
  toggleAutoStepAfterSpawn.checked = state.autoStepAfterSpawn;
  toggleAutoStepAfterSpawn.addEventListener('change', (e) => {
    state.autoStepAfterSpawn = e.target.checked;
  });
}

if (scoreWeightInputEl) {
  scoreWeightInputEl.value = String(state.scoreWeight);
  scoreWeightInputEl.addEventListener('change', () => {
    getRuntimeWeights();
  });
}

if (movePenaltyInputEl) {
  movePenaltyInputEl.value = String(state.movePenalty);
  movePenaltyInputEl.addEventListener('change', () => {
    getRuntimeWeights();
  });
}

const timeLimitMsInputEl = document.getElementById('timeLimitMsInput');
if (timeLimitMsInputEl) {
  timeLimitMsInputEl.value = String(state.timeLimitMs);
  timeLimitMsInputEl.addEventListener('change', () => {
    getRuntimeWeights();
  });
}

makeTilePicker(tilePickerEl);
makeTilePicker(tileAfterMoveEl);
syncTilePickers();
getRuntimeWeights();
renderSelectedDirection(-1);
  render();
  ensureSolver();

  // Language Toggle Listener
  const langToggleBtn = document.getElementById('btnLang');
  if (langToggleBtn) {
    langToggleBtn.addEventListener('click', () => {
      updateLanguage(state.lang === 'en' ? 'ar' : 'en');
    });
  }
  updateLanguage(state.lang);
