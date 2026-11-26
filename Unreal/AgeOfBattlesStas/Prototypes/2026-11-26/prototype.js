
'use strict';

const eras = {
  ancient: 'Античность',
  high_medieval: 'Классическое средневековье',
  late_medieval: 'Позднее средневековье',
  renaissance: 'Возрождение',
  sengoku: 'Сэнгоку',
  great_northern_war: 'Северная война',
  napoleonic: 'Наполеоновские войны',
};

const proposedBattles = [
  ['ancient', 'Фермопилы', 'Удержать узкий проход до истечения времени.'],
  ['ancient', 'Алезия', 'Оборонять два рубежа и решать, куда направить резерв.'],
  ['high_medieval', 'Гастингс', 'Удержать гребень и выбрать момент для контратаки.'],
  ['high_medieval', 'Бувин', 'Перебрасывать резерв между центром и флангами.'],
  ['late_medieval', 'Пате', 'Перехватить противника, пока его полки занимают позиции.'],
  ['late_medieval', 'Босворт', 'Выбрать момент вступления отдельной группы в бой.'],
  ['renaissance', 'Рокруа', 'Использовать конницу на флангах и сохранить устойчивость центра.'],
  ['renaissance', 'Нёрдлинген', 'Овладеть высотой и удержать её до подхода подкрепления.'],
  ['sengoku', 'Окэхадзама', 'Скрыть подход ударной группы и выйти к лагерю.'],
  ['sengoku', 'Микатагахара', 'Прикрыть отход основной группы и сохранить резерв.'],
  ['great_northern_war', 'Клишов', 'Подвести войска через сложную местность и удержать темп наступления.'],
  ['great_northern_war', 'Фрауштадт', 'Согласовать выход двух флангов и сохранить линию центра.'],
  ['napoleonic', 'Йена и Ауэрштедт', 'Координировать группы на двух участках с задержкой доставки приказов.'],
  ['napoleonic', 'Лейпциг', 'Распределять резерв между несколькими направлениями наступления.'],
].map(([era, name, brief]) => ({era, name, brief, candidate: true}));

const battles = [...builtBattles, ...proposedBattles];
const views = {catalogue: 'Выбор битвы', overview: 'Обзор поля', officer: 'Офицер'};
const concepts = {
  field: {
    catalogue: 'Полный список битв с быстрым поиском по эпохе и доступности.',
    overview: 'Дневной свет и дальние холмы. Состав полков и их состояние видны в нижней полосе.',
    officer: 'Состояние офицера слева. Выбранный полк и команды открываются по Tab.',
  },
  line: {
    catalogue: 'Краткий список битв и крупный эскиз оформления.',
    overview: 'Тёплый вечерний свет. За полем читаются рощи, дороги и крыши ферм.',
    officer: 'Ближняя камера, атака и парирование рядом. Приказы открываются снизу справа.',
  },
  atlas: {
    catalogue: 'Эпохи слева, список битв в центре, задача и параметры сценария справа.',
    overview: 'Прохладное утро и дымка у горизонта. Цель, резерв и связь с полком собраны слева.',
    officer: 'Цель остаётся на экране. Панель приказов показывает передачу и выполнение.',
  },
};

const goalNames = {
  Eliminate: 'Разгромить войска противника',
  HillHold: 'Удержать высоту',
  Breakthrough: 'Осуществить прорыв',
  Crossing: 'Провести полки через переправу',
};

const regiments = [
  {name: '3-й линейный', count: 388, capacity: 420, formation: 'Линия', morale: 'Боевой дух высокий'},
  {name: '2-й лёгкий', count: 249, capacity: 280, formation: 'В строю', morale: 'Боевой дух устойчивый'},
  {name: '4-й кирасирский', count: 296, capacity: 320, formation: 'Резерв', morale: 'Готов к вводу'},
];

const params = new URLSearchParams(location.search);
const battleDemo = battles.find((battle) => battle.terrain === 'Waterloo');
const orders = new Map();
const state = {
  style: Object.hasOwn(concepts, params.get('variant')) ? params.get('variant') : 'field',
  view: Object.hasOwn(views, params.get('view')) ? params.get('view') : 'overview',
  era: 'all',
  status: 'all',
  query: '',
  battle: battleDemo,
  regiment: 0,
  closeCamera: false,
  commandOpen: false,
  stamina: 76,
};

const $ = (selector) => document.querySelector(selector);
const number = new Intl.NumberFormat('ru-RU');
const battleList = $('#battle-list');
const eraTabs = $('#era-tabs');
const scene = $('#scene');
const commands = $('#commands');
const sceneImage = $('#scene-image');
const previewImage = $('#preview-image');
const regimentBar = $('.regiments');
let feedbackTimeout;

Object.entries({all: 'Все эпохи', ...eras}).forEach(([id, label]) => {
  const button = document.createElement('button');
  button.type = 'button';
  button.dataset.era = id;
  button.textContent = label;
  eraTabs.append(button);
});

regiments.forEach((regiment, index) => {
  const button = document.createElement('button');
  const name = document.createElement('strong');
  const count = document.createElement('small');
  const bar = document.createElement('i');
  button.type = 'button';
  button.dataset.regiment = index;
  name.textContent = regiment.name;
  count.textContent = number.format(regiment.count) + ' в строю';
  bar.style.setProperty('--strength', Math.round(100 * regiment.count / regiment.capacity) + '%');
  bar.setAttribute('aria-hidden', 'true');
  button.append(name, count, bar);
  regimentBar.append(button);
});

function setSelected(selector, key, value) {
  document.querySelectorAll(selector).forEach((element) => {
    element.setAttribute('aria-pressed', String(element.dataset[key] === String(value)));
  });
}

function renderChrome() {
  document.body.dataset.style = state.style;
  $('#view-title').textContent = views[state.view];
  $('#concept-line').textContent = concepts[state.style][state.view];
  setSelected('[data-style-choice]', 'styleChoice', state.style);
  setSelected('[data-view-choice]', 'viewChoice', state.view);

  const image = artwork[state.style];
  if (sceneImage.getAttribute('src') !== image) {
    sceneImage.src = image;
    previewImage.src = image;
  }
}

function matchingBattles() {
  const query = state.query.trim().toLocaleLowerCase('ru');
  return battles.filter((battle) => {
    const eraMatches = state.era === 'all' || battle.era === state.era;
    const statusMatches = state.status === 'all' || (state.status === 'candidate') === Boolean(battle.candidate);
    return eraMatches && statusMatches && battle.name.toLocaleLowerCase('ru').includes(query);
  });
}

function renderCatalogue() {
  setSelected('[data-era]', 'era', state.era);
  setSelected('[data-status]', 'status', state.status);

  const matches = matchingBattles();
  const count = matches.filter((battle) => !battle.candidate).length;
  $('#result-count').textContent = matches.length
    ? 'В игре: ' + count + ' · К добавлению: ' + (matches.length - count)
    : 'Битвы не найдены. Измените запрос или фильтры.';
  if (!matches.includes(state.battle)) {
    state.battle = matches[0];
  }

  const rows = matches.map((battle) => {
    const button = document.createElement('button');
    const title = document.createElement('strong');
    const status = document.createElement('small');
    const era = document.createElement('span');
    button.type = 'button';
    button.className = 'battle-btn';
    button.dataset.battle = battles.indexOf(battle);
    button.toggleAttribute('data-candidate', Boolean(battle.candidate));
    title.textContent = battle.name;
    status.textContent = battle.candidate ? 'К добавлению' : 'В игре';
    era.textContent = eras[battle.era];
    button.append(title, status, era);
    return button;
  });

  battleList.replaceChildren(...rows);
  renderBattleDetails();
}

function renderBattleDetails() {
  const battle = state.battle;
  $('#mission-preview').hidden = !battle;
  if (!battle) return;
  setSelected('[data-battle]', 'battle', battles.indexOf(battle));
  $('#preview-name').textContent = battle.name;
  $('#preview-brief').textContent = battle.brief;
  const info = battle.candidate
    ? [eras[battle.era], 'Предложение для каталога']
    : [goalNames[battle.goal], number.format(battle.figures) + ' фигур', battle.width + ' × ' + battle.depth + ' м'];
  const labels = info.filter(Boolean).map((label) => {
    const span = document.createElement('span');
    span.textContent = label;
    return span;
  });
  $('#preview-meta').replaceChildren(...labels);
}

function renderBattle() {
  const officer = state.view === 'officer';
  scene.dataset.view = state.view;
  scene.dataset.camera = officer && state.closeCamera ? 'close' : 'wide';
  $('#possess').hidden = officer;
  $('#camera').hidden = !officer;
  $('#camera').textContent = state.closeCamera ? 'Камера дальше' : 'Камера ближе';
  $('#camera').setAttribute('aria-pressed', String(state.closeCamera));

  for (const selector of ['#officer-status', '#actions', '#crosshair']) {
    $(selector).hidden = !officer;
  }
  $('#focus-regiment').hidden = officer;
  regimentBar.hidden = officer;

  const regiment = regiments[state.regiment];
  const order = orders.get(state.regiment);
  const formation = order?.delivered && order.label === 'Перестроиться в линию' ? 'Линия' : regiment.formation;
  const orderStatus = order
    ? order.label + ' · ' + (order.delivered ? 'Получен, выполняется' : 'Передаётся')
    : '';
  setSelected('[data-regiment]', 'regiment', state.regiment);
  $('#focus-name').textContent = regiment.name;
  $('#focus-summary').textContent = orderStatus || (formation + ' · ' + regiment.morale);
  $('#officer-name').textContent = 'Офицер · ' + regiment.name;
  $('#command-target').textContent = regiment.name + ' · ' + formation.toLowerCase();
  $('#order-stamp').textContent = orderStatus || 'Ожидает приказа';
  $('#audit-text').textContent = orderStatus || 'Связной готов. Приказов в пути нет.';
  $('#command-sheet').hidden = !state.commandOpen;
  commands.setAttribute('aria-expanded', String(state.commandOpen));
}

function setView(view) {
  state.view = view;
  state.commandOpen = false;
  $('#action-feedback').textContent = '';
  $('#catalogue').hidden = view !== 'catalogue';
  scene.hidden = view === 'catalogue';
  renderChrome();
  if (view === 'catalogue') renderCatalogue();
  else renderBattle();
}

function toggleCommands(restoreFocus = false) {
  state.commandOpen = !state.commandOpen;
  renderBattle();
  if (state.commandOpen) $('#command-list button').focus({preventScroll: true});
  else if (restoreFocus) commands.focus({preventScroll: true});
}

function issueOrder(label) {
  const regiment = state.regiment;
  const order = {label, delivered: false};
  orders.set(regiment, order);
  renderBattle();
  window.setTimeout(() => {
    if (orders.get(regiment) !== order) return;
    order.delivered = true;
    renderBattle();
  }, 1100);
}

function playAction(action) {
  const attack = action === 'attack';
  const cost = attack ? 9 : 6;
  const spent = state.stamina >= cost;
  if (spent) {
    state.stamina -= cost;
    $('#stamina').value = state.stamina;
    $('#stamina-value').textContent = state.stamina + ' / 100';
  }
  $('#action-feedback').textContent = spent
    ? (attack ? 'Удар' : 'Парирование')
    : 'Нужна передышка';
  window.clearTimeout(feedbackTimeout);
  feedbackTimeout = window.setTimeout(() => {
    $('#action-feedback').textContent = '';
  }, 1100);
  if (spent) {
    window.setTimeout(() => {
      state.stamina = Math.min(76, state.stamina + cost);
      $('#stamina').value = state.stamina;
      $('#stamina-value').textContent = state.stamina + ' / 100';
    }, 2400);
  }
}

$('#style-tabs').addEventListener('click', (event) => {
  const choice = event.target.closest('[data-style-choice]');
  if (!choice) return;
  state.style = choice.dataset.styleChoice;
  renderChrome();
});
$('#view-tabs').addEventListener('click', (event) => {
  const choice = event.target.closest('[data-view-choice]');
  if (!choice) return;
  setView(choice.dataset.viewChoice);
  if (state.view === 'officer') scene.focus({preventScroll: true});
});
$('#era-tabs').addEventListener('click', (event) => {
  const choice = event.target.closest('[data-era]');
  if (!choice) return;
  state.era = choice.dataset.era;
  renderCatalogue();
});
$('#status-tabs').addEventListener('click', (event) => {
  const choice = event.target.closest('[data-status]');
  if (!choice) return;
  state.status = choice.dataset.status;
  renderCatalogue();
});
$('#battle-search').addEventListener('input', (event) => {
  state.query = event.target.value;
  renderCatalogue();
});
battleList.addEventListener('click', (event) => {
  const choice = event.target.closest('[data-battle]');
  if (!choice) return;
  state.battle = battles[Number(choice.dataset.battle)];
  renderBattleDetails();
});

$('#demo').addEventListener('click', () => {
  setView('overview');
  $('#view-tabs [data-view-choice=overview]').focus({preventScroll: true});
});
$('#possess').addEventListener('click', () => {
  setView('officer');
  scene.focus({preventScroll: true});
});
$('#camera').addEventListener('click', () => {
  state.closeCamera = !state.closeCamera;
  renderBattle();
});
regimentBar.addEventListener('click', (event) => {
  const choice = event.target.closest('[data-regiment]');
  if (!choice) return;
  state.regiment = Number(choice.dataset.regiment);
  renderBattle();
});
commands.addEventListener('click', () => toggleCommands(true));
$('#close-commands').addEventListener('click', () => toggleCommands(true));
$('#command-list').addEventListener('click', (event) => {
  const choice = event.target.closest('[data-order]');
  if (choice) issueOrder(choice.dataset.order);
});
$('#actions').addEventListener('click', (event) => {
  const choice = event.target.closest('[data-action]');
  if (choice) playAction(choice.dataset.action);
});
scene.tabIndex = -1;
scene.addEventListener('click', (event) => {
  if (!event.target.closest('button, .command-sheet')) scene.focus({preventScroll: true});
});
document.addEventListener('keydown', (event) => {
  if (state.view === 'catalogue') return;
  if (event.key === 'Escape' && state.commandOpen) {
    event.preventDefault();
    toggleCommands(true);
  }
  if (event.key === 'Tab' && !event.shiftKey && !event.ctrlKey && !event.altKey &&
      !event.metaKey && document.activeElement === scene) {
    event.preventDefault();
    toggleCommands();
  }
});

setView(state.view);
