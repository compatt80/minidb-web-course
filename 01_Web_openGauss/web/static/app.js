const editor = document.querySelector('#sql-editor');
const resultArea = document.querySelector('#result-area');
const resultMeta = document.querySelector('#result-meta');
const resultBadge = document.querySelector('#result-badge');
const tableList = document.querySelector('#table-list');
const detailsTitle = document.querySelector('#details-title');
const detailsArea = document.querySelector('#details-area');

function setHealth(ok, text) {
  document.querySelector('#health-dot').className = `dot ${ok ? 'ok' : 'error'}`;
  document.querySelector('#health-text').textContent = text;
}

function setResultBadge(kind, text) {
  resultBadge.className = `result-badge ${kind}`;
  resultBadge.textContent = text;
}

function renderResult(data) {
  if (!data.ok) {
    resultArea.innerHTML = `<div class="error-box">${escapeHtml(data.error)}</div>`;
    resultMeta.textContent = 'Request failed';
    setResultBadge('error', 'ERROR');
    return;
  }
  resultMeta.textContent = data.message;
  setResultBadge('success', 'SUCCESS');
  if (!data.columns.length) {
    resultArea.innerHTML = `<div class="result-placeholder"><span class="placeholder-icon">✓</span><p>${escapeHtml(data.message)}</p></div>`;
    return;
  }
  const head = data.columns.map((column) => `<th>${escapeHtml(column)}</th>`).join('');
  const body = data.rows.map((row) => `<tr>${row.map((value) => value === null ? '<td class="null">NULL</td>' : `<td>${escapeHtml(value)}</td>`).join('')}</tr>`).join('');
  resultArea.innerHTML = `<table><thead><tr>${head}</tr></thead><tbody>${body}</tbody></table>`;
}

function escapeHtml(value) {
  return String(value).replace(/[&<>'"]/g, (char) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', "'": '&#39;', '"': '&quot;' }[char]));
}

async function loadHealth() {
  try {
    const response = await fetch('/api/health');
    const data = await response.json();
    setHealth(data.ok, data.ok ? `${data.user} · connected` : 'Database unavailable');
    if (data.ok) document.querySelector('#database-label').textContent = data.database;
  } catch (error) { setHealth(false, 'Service unavailable'); }
}

async function loadTables() {
  tableList.innerHTML = '<div class="empty">Loading tables...</div>';
  try {
    const response = await fetch('/api/tables');
    const data = await response.json();
    if (!data.ok) throw new Error(data.error);
    tableList.innerHTML = data.tables.length ? data.tables.map((name) => `<button class="table-item" data-table="${escapeHtml(name)}"><span class="table-icon">▦</span>${escapeHtml(name)}</button>`).join('') : '<div class="empty">No tables</div>';
    document.querySelectorAll('.table-item').forEach((button) => button.addEventListener('click', () => loadTableInfo(button.dataset.table)));
  } catch (error) { tableList.innerHTML = `<div class="empty">${escapeHtml(error.message)}</div>`; }
}

async function loadTableInfo(name) {
  detailsTitle.textContent = name;
  detailsArea.innerHTML = '<div class="empty">Loading schema...</div>';
  document.querySelectorAll('.table-item').forEach((button) => button.classList.toggle('active', button.dataset.table === name));
  try {
    const response = await fetch(`/api/tables/${encodeURIComponent(name)}`);
    const data = await response.json();
    if (!data.ok) throw new Error(data.error);
    detailsArea.innerHTML = data.columns.map((column) => `<div class="column-row"><div><div class="column-name">${escapeHtml(column[0])}</div><div class="column-type">${escapeHtml(column[1])}</div></div><div class="column-null">${column[2] === 'YES' ? 'NULL' : 'required'}</div></div>`).join('');
  } catch (error) { detailsArea.innerHTML = `<div class="error-box">${escapeHtml(error.message)}</div>`; }
}

async function runQuery() {
  const button = document.querySelector('#run-query');
  button.disabled = true;
  button.textContent = 'Running...';
  try {
    const response = await fetch('/api/query', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ sql: editor.value }) });
    renderResult(await response.json());
    loadTables();
  } catch (error) { renderResult({ ok: false, error: error.message }); }
  finally { button.disabled = false; button.innerHTML = '<span>▶</span> Run query'; }
}

document.querySelector('#run-query').addEventListener('click', runQuery);
document.querySelector('#clear-query').addEventListener('click', () => { editor.value = ''; editor.focus(); });
document.querySelector('#refresh-tables').addEventListener('click', loadTables);
document.querySelectorAll('[data-sql]').forEach((button) => button.addEventListener('click', () => { editor.value = button.dataset.sql; runQuery(); }));
editor.addEventListener('keydown', (event) => { if ((event.ctrlKey || event.metaKey) && event.key === 'Enter') { event.preventDefault(); runQuery(); } });

loadHealth();
loadTables();
