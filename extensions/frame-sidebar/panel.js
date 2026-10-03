const $ = (id) => document.getElementById(id);
const favouritesNode = $('favourites');
const addressInput = $('address-input');
const statusNode = $('status');
const tabQuery = $('tab-query');
const tabResults = $('tab-results');

const DEFAULT_FAVOURITES = [
  {id: 'youtube', name: 'YouTube', url: 'https://www.youtube.com/'},
  {id: 'github', name: 'GitHub', url: 'https://github.com/'},
  {id: 'wikipedia', name: 'Wikipedia', url: 'https://www.wikipedia.org/'},
  {id: 'archive', name: 'Archive', url: 'https://web.archive.org/'},
];
const QUICK_SEARCHES = {
  yt: 'https://www.youtube.com/results?search_query=%s',
  gh: 'https://github.com/search?q=%s',
  wiki: 'https://en.wikipedia.org/w/index.php?search=%s',
  maps: 'https://www.google.com/maps/search/%s',
};

let favourites = [];
let dragId = null;
let statusTimer;

function say(message) {
  statusNode.textContent = message;
  clearTimeout(statusTimer);
  statusTimer = setTimeout(() => { statusNode.textContent = 'READY'; }, 1800);
}

async function activeTab() {
  return (await chrome.tabs.query({active: true, currentWindow: true}))[0] || null;
}

function addressLabel(url) {
  if (!/^https?:\/\//i.test(url || '')) return url || '';
  return url.replace(/^https?:\/\//i, '').replace(/^www\./i, '');
}

async function syncAddress() {
  if (document.activeElement === addressInput) return;
  addressInput.value = addressLabel((await activeTab())?.url);
}

function resolveInput(value) {
  const text = value.trim();
  if (!text) return null;
  const [rawAlias, ...rest] = text.split(/\s+/);
  const alias = rawAlias.replace(/^!/, '').toLowerCase();
  if (QUICK_SEARCHES[alias]) {
    const query = rest.join(' ');
    if (!query) return ({yt: 'https://www.youtube.com/', gh: 'https://github.com/',
      wiki: 'https://www.wikipedia.org/', maps: 'https://www.google.com/maps/'}[alias]);
    return QUICK_SEARCHES[alias].replace('%s', encodeURIComponent(query));
  }
  if (/^https?:\/\//i.test(text)) {
    try { return new URL(text).href; } catch { return null; }
  }
  if (/^(localhost|[\w-]+(?:\.[\w-]+)+)(:\d+)?(?:\/[^\s]*)?$/i.test(text)) {
    return `${text.startsWith('localhost') ? 'http' : 'https'}://${text}`;
  }
  return `https://www.google.com/search?q=${encodeURIComponent(text)}`;
}

async function openOrFocus(url) {
  const current = await activeTab();
  const tabs = await chrome.tabs.query({currentWindow: true});
  const target = new URL(url);
  const match = tabs.find((tab) => {
    try {
      const candidate = new URL(tab.url);
      return candidate.hostname === target.hostname &&
        (target.pathname === '/' || candidate.pathname === target.pathname);
    } catch { return false; }
  });
  if (match) {
    await chrome.tabs.update(match.id, {active: true});
    say('TAB REUSED');
    return;
  }
  await chrome.tabs.create({url, active: true, index: current ? current.index + 1 : undefined});
  say('OPENED');
}

async function saveFavourites() {
  await chrome.storage.local.set({favourites});
  renderFavourites();
}

function siteButton(site) {
  const button = document.createElement('button');
  button.className = 'site';
  button.type = 'button';
  button.draggable = true;
  button.title = `${site.name} · drag to reorder · right click to remove`;
  button.setAttribute('aria-label', `Open ${site.name}`);
  const sigil = document.createElement('span');
  sigil.className = 'sigil';
  sigil.textContent = site.name.slice(0, 2).toUpperCase();
  const name = document.createElement('span');
  name.textContent = site.name;
  button.append(sigil, name);
  button.addEventListener('click', () => openOrFocus(site.url));
  button.addEventListener('contextmenu', async (event) => {
    event.preventDefault();
    favourites = favourites.filter((item) => item.id !== site.id);
    await saveFavourites();
    say('REMOVED');
  });
  button.addEventListener('dragstart', (event) => {
    dragId = site.id;
    event.dataTransfer.effectAllowed = 'move';
    event.dataTransfer.setData('text/plain', site.id);
    button.classList.add('dragging');
  });
  button.addEventListener('dragend', () => { dragId = null; button.classList.remove('dragging'); });
  button.addEventListener('dragover', (event) => {
    if (!dragId || dragId === site.id) return;
    event.preventDefault();
    button.classList.add('drop-target');
  });
  button.addEventListener('dragleave', () => button.classList.remove('drop-target'));
  button.addEventListener('drop', async (event) => {
    event.preventDefault();
    button.classList.remove('drop-target');
    const from = favourites.findIndex((item) => item.id === dragId);
    const to = favourites.findIndex((item) => item.id === site.id);
    if (from < 0 || to < 0 || from === to) return;
    const [moved] = favourites.splice(from, 1);
    favourites.splice(to, 0, moved);
    await saveFavourites();
    say('REORDERED');
  });
  return button;
}

function renderFavourites() {
  favouritesNode.replaceChildren(...favourites.map(siteButton));
  if (!favourites.length) {
    const empty = document.createElement('div');
    empty.className = 'empty';
    empty.textContent = 'Add the current page to start your space.';
    favouritesNode.append(empty);
  }
}

async function renderTabSearch() {
  const query = tabQuery.value.trim().toLowerCase();
  tabResults.replaceChildren();
  if (!query) return;
  const tabs = await chrome.tabs.query({currentWindow: true});
  for (const tab of tabs.filter((item) => `${item.title} ${item.url}`.toLowerCase().includes(query)).slice(0, 8)) {
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = tab.title || tab.url || 'Untitled tab';
    button.title = tab.url || '';
    button.addEventListener('click', async () => {
      await chrome.tabs.update(tab.id, {active: true});
      tabQuery.value = '';
      tabResults.replaceChildren();
    });
    tabResults.append(button);
  }
  if (!tabResults.children.length) {
    const empty = document.createElement('div');
    empty.className = 'empty';
    empty.textContent = 'No matching tabs';
    tabResults.append(empty);
  }
}

$('address-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  const url = resolveInput(addressInput.value);
  if (!url) { say('INVALID ADDRESS'); return; }
  const tab = await activeTab();
  if (tab) await chrome.tabs.update(tab.id, {url});
  else await chrome.tabs.create({url});
  addressInput.blur();
});

$('copy-link').addEventListener('click', async () => {
  const url = (await activeTab())?.url;
  if (!url) { say('NO LINK'); return; }
  await navigator.clipboard.writeText(url);
  say('LINK COPIED');
});

$('add-current').addEventListener('click', async () => {
  const tab = await activeTab();
  if (!tab?.url || !/^https?:\/\//.test(tab.url)) { say('OPEN A SITE FIRST'); return; }
  if (favourites.some((item) => item.url === tab.url)) { say('ALREADY SAVED'); return; }
  favourites.push({id: crypto.randomUUID(), name: (tab.title || new URL(tab.url).hostname).slice(0, 28), url: tab.url});
  await saveFavourites();
  say('SAVED');
});

for (const button of document.querySelectorAll('[data-page]')) {
  button.addEventListener('click', () => chrome.tabs.create({url: button.dataset.page}));
}

$('archive-current').addEventListener('click', async () => {
  const tab = await activeTab();
  if (!/^https?:\/\//i.test(tab?.url || '')) {
    say('OPEN A SITE FIRST');
    return;
  }
  await chrome.tabs.create({
    url: `https://web.archive.org/web/${tab.url}`,
    active: true,
    index: tab.index + 1,
  });
  say('WAYBACK LOOKUP');
});

tabQuery.addEventListener('input', renderTabSearch);
chrome.tabs.onActivated.addListener(syncAddress);
chrome.tabs.onUpdated.addListener(syncAddress);

async function init() {
  const stored = await chrome.storage.local.get('favourites');
  favourites = Array.isArray(stored.favourites) ? stored.favourites : DEFAULT_FAVOURITES;
  if (!Array.isArray(stored.favourites)) await chrome.storage.local.set({favourites});
  renderFavourites();
  await syncAddress();
}

init().catch(() => say('UNAVAILABLE'));
