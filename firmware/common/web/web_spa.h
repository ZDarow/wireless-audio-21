// web_spa.h — SPA (HTML/CSS/JS) Web UI мастера.
// Вынесен из web_server.h для декомпозиции монолита (A1).
// Header-only: static const char[] — каждый TU получает свою копию (~6 KB),
// для проекта это приемлемо (2 TU: master_s3 + legacy master).
#pragma once

namespace audio21 {

static const char kSpaHtml[] = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Audio 2.1 Master</title>
<style>
  * { box-sizing: border-box; }
  body { margin: 0; font-family: system-ui, -apple-system, Segoe UI, Roboto, Ubuntu, Cantarell, Noto Sans, sans-serif; background:#111; color:#eee; }
  h1 { font-size: 18px; margin: 14px; }
  nav { display: flex; gap: 6px; padding: 8px; background:#1a1a1a; position: sticky; top: 0; }
  nav button { flex: 1; background:#222; color:#ddd; border: 1px solid #333; border-radius: 6px; padding: 8px; }
  nav button.active { background:#2a6; color:#fff; border-color:#2a6; }
  .page { display: none; padding: 14px; }
  .page.active { display: block; }
  .card { background:#1c1c1c; border: 1px solid #2a2a2a; border-radius: 8px; padding: 14px; margin: 10px 0; }
  .row { display: flex; gap: 8px; align-items: center; }
  input[type="password"], input[type="text"], select { background:#222; color:#eee; border:1px solid #333; border-radius: 6px; padding: 8px; }
  button.ghost { background:#333; color:#eee; border:1px solid #444; border-radius: 6px; padding: 8px 12px; }
  button.danger { background:#a33; color:#fff; border:1px solid #a33; border-radius: 6px; padding: 8px 12px; }
  table { border-collapse: collapse; width: 100%; }
  td { padding: 5px 8px; border-bottom: 1px solid #333; font-size: 13px; }
  td:first-child { color: #9e9e9e; width: 40%; }
  .mono { font-family: monospace; font-size: 12px; white-space: pre-wrap; word-break: break-all; }
  #toast { position: fixed; bottom: 16px; left: 50%; transform: translateX(-50%); background: #2a6; color: #fff; padding: 8px 14px; border-radius: 6px; display: none; z-index: 10; }
  .banner { background:#1c1c1c; border:1px solid #a33; border-radius:8px; padding:14px; margin:14px 0; }
</style>
</head>
<body>
<h1>Audio 2.1 Master</h1>
<div id="defaultPwBanner" class="banner" style="display:none">
  <b>Внимание:</b> используется заводской пароль точки доступа
  (<span class="mono">audio21master</span>). Смените его в настройках
  Wi-Fi мастера — любой в зоне действия может подключиться к системе.
</div>
<div id="loginBanner" class="banner" style="display:none">
  <h2>Требуется вход</h2>
  <label>Пароль администратора</label>
  <div class="row">
    <input type="password" id="loginPass" style="flex:1">
    <button id="loginBtn">Войти</button>
  </div>
</div>
<nav id="nav">
  <button data-p="dashboard" class="active">Dashboard</button>
  <button data-p="wifi">Wi-Fi</button>
  <button data-p="internet">Internet</button>
  <button data-p="audio">Audio</button>
  <button data-p="delays">Delays</button>
  <button data-p="satellites">Satellites</button>
  <button data-p="system">System</button>
  <button data-p="update">Update</button>
  <button data-p="logs">Logs</button>
</nav>

<div id="p-dashboard" class="page active">
  <div class="card">
    <h2>Статус</h2>
    <table>
      <tr><td>Система</td><td id="d_system">-</td></tr>
      <tr><td>Время (NTP)</td><td id="d_time">-</td></tr>
      <tr><td>Wi-Fi</td><td id="d_wifi">-</td></tr>
      <tr><td>RSSI</td><td id="d_rssi">-</td></tr>
      <tr><td>MAC</td><td id="d_mac">-</td></tr>
      <tr><td>Интернет</td><td id="d_internet">-</td></tr>
      <tr><td>IP-адрес</td><td id="d_ip">-</td></tr>
      <tr><td>Hostname</td><td id="d_host">-</td></tr>
      <tr><td>Источник аудио</td><td id="d_source">-</td></tr>
      <tr><td>Громкость</td><td id="d_volume">-</td></tr>
      <tr><td>Кроссовер</td><td id="d_crossover">-</td></tr>
      <tr><td>Задержки</td><td id="d_delays">-</td></tr>
      <tr><td>Сателлиты</td><td id="d_sats">-</td></tr>
      <tr><td>CPU load</td><td id="d_cpu">-</td></tr>
      <tr><td>Свободный heap</td><td id="d_heap">-</td></tr>
      <tr><td>Свободный PSRAM</td><td id="d_psram">-</td></tr>
      <tr><td>Версия</td><td id="d_ver">-</td></tr>
    </table>
    <div class="row">
      <button id="dMuteBtn">Mute</button>
      <button id="dSaveBtn" class="ghost">Сохранить (NVS)</button>
      <button id="dRebootBtn" class="danger">Reboot</button>
      <button id="dLogoutBtn" class="ghost">Выйти</button>
    </div>
  </div>
</div>

<div id="p-wifi" class="page">
  <div class="card">
    <h2>Wi-Fi</h2>
    <label>Режим</label>
    <select id="wMode">
      <option value="AP_DIRECT">AP_DIRECT</option>
      <option value="STA">STA</option>
      <option value="APSTA">APSTA (репитер)</option>
    </select>
    <div class="row">
      <button id="wApply" class="ghost">Применить</button>
    </div>
  </div>
  <div class="card">
    <h2>Сети</h2>
    <div id="wifiList"></div>
  </div>
</div>

<div id="p-internet" class="page">
  <div class="card">
    <h2>Интернет</h2>
    <table>
      <tr><td>Состояние</td><td id="i_state">-</td></tr>
      <tr><td>SSID</td><td id="i_ssid">-</td></tr>
      <tr><td>IP</td><td id="i_ip">-</td></tr>
      <tr><td>Gateway</td><td id="i_gw">-</td></tr>
      <tr><td>DNS</td><td id="i_dns">-</td></tr>
    </table>
  </div>
</div>

<div id="p-audio" class="page">
  <div class="card">
    <h2>Аудио</h2>
    <label>Master: <span id="a_volLabel" class="val">50</span></label>
    <input type="range" id="a_vol" min="0" max="100" value="50">
    <label>Left: <span id="a_volLeftLabel" class="val">50</span></label>
    <input type="range" id="a_volLeft" min="0" max="100" value="50">
    <label>Right: <span id="a_volRightLabel" class="val">50</span></label>
    <input type="range" id="a_volRight" min="0" max="100" value="50">
    <label>Sub: <span id="a_volSubLabel" class="val">50</span></label>
    <input type="range" id="a_volSub" min="0" max="100" value="50">
    <label>Частота: <span id="a_xoLabel" class="val">90</span> Гц</label>
    <input type="range" id="a_xo" min="40" max="160" value="90">
    <div class="row">
      <button id="aApply" class="ghost">Применить</button>
    </div>
  </div>
</div>

<div id="p-delays" class="page">
  <div class="card">
    <h2>Задержки</h2>
    <label>Left: <span id="dly_l" class="val">0</span> мс</label>
    <input type="range" id="dlyL" min="0" max="200" value="0">
    <label>Right: <span id="dly_r" class="val">0</span> мс</label>
    <input type="range" id="dlyR" min="0" max="200" value="0">
    <label>Sub: <span id="dly_s" class="val">0</span> мс</label>
    <input type="range" id="dlyS" min="0" max="200" value="0">
    <div class="row">
      <button id="dApply" class="ghost">Применить</button>
    </div>
  </div>
</div>

<div id="p-satellites" class="page">
  <div class="card">
    <h2>Сателлиты</h2>
    <table>
      <tr><td>Левый</td><td id="s_left">-</td></tr>
      <tr><td>Правый</td><td id="s_right">-</td></tr>
    </table>
  </div>
</div>

<div id="p-system" class="page">
  <div class="card">
    <h2>Система</h2>
    <label>Hostname</label>
    <input type="text" id="sysHost">
    <div class="row">
      <button id="sysApply" class="ghost">Применить</button>
    </div>
  </div>
</div>

<div id="p-update" class="page">
  <div class="card">
    <h2>OTA-обновление</h2>
    <input type="file" id="uFile">
    <div class="row">
      <button id="uBtn" class="ghost">Загрузить</button>
    </div>
    <div id="uBar" style="height:8px;background:#333;border-radius:4px;overflow:hidden;margin-top:8px">
      <div id="uFill" style="width:0;height:100%;background:#2a6;transition:width .2s"></div>
    </div>
    <div id="uMsg" style="margin-top:6px;color:#aaa;font-size:12px"></div>
  </div>
</div>

<div id="p-logs" class="page">
  <div class="card">
    <h2>Логи</h2>
    <select id="logLevel">
      <option value="">Все</option>
      <option value="I">Info</option>
      <option value="W">Warn</option>
      <option value="E">Error</option>
    </select>
    <div id="logOut" class="mono" style="background:#111;border:1px solid #333;border-radius:6px;padding:8px;height: 50vh; overflow:auto"></div>
  </div>
</div>

<div id="toast"></div>

<script>
const $ = id => document.getElementById(id);
const api = (path, opts={}) => fetch(path, Object.assign({headers:{'X-CSRF-Token': csrf}}, opts)).then(r => r.json());
function toast(msg){ const t=$('toast'); t.textContent=msg; t.style.display='block'; setTimeout(()=>t.style.display='none', 2000); }
function setVal(id,v){ const el=$(id); if(el) el.textContent=v; }

const pages = { dashboard:'p-dashboard', wifi:'p-wifi', internet:'p-internet', audio:'p-audio', delays:'p-delays', satellites:'p-satellites', system:'p-system', update:'p-update', logs:'p-logs' };
document.querySelectorAll('nav button[data-p]').forEach(b => b.onclick = () => {
  document.querySelectorAll('nav button').forEach(x=>x.classList.remove('active'));
  document.querySelectorAll('.page').forEach(x=>x.classList.remove('active'));
  b.classList.add('active');
  const el = $(pages[b.dataset.p]);
  if (el) el.classList.add('active');
});

let csrf = '';
function hideLogin(){ $('loginBanner').style.display='none'; }
function showLogin(){ $('loginBanner').style.display='block'; }

async function bootCheck() {
  try {
    const s = await api('/api/status');
    if (s.system.authed) { hideLogin(); }
    else if (!s.system.auth_enabled) {
      const pass = prompt('Первый запуск. Задайте пароль администратора (мин 4 символа):');
      if (pass) {
        const c = prompt('Повторите пароль:');
        if (pass === c) {
          await fetch('/api/admin/setup', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({password:pass, confirm:c}) });
          hideLogin();
        }
      }
    } else { showLogin(); }
    refresh();
  } catch (e) {}
}
bootCheck();
setInterval(() => { if ($('p-dashboard').classList.contains('active')) refresh(); }, 2000);

async function refresh() {
  try {
    const s = await api('/api/status');
    setVal('d_system', s.system.hostname + ' / ' + s.system.version);
    setVal('d_time', s.system.time || '-');
    setVal('d_wifi', s.wifi.ssid || '-');
    setVal('d_rssi', s.wifi.rssi + ' dBm');
    setVal('d_mac', s.system.mac);
    setVal('d_internet', s.wifi.internet);
    setVal('d_ip', s.wifi.ip);
    setVal('d_host', s.system.hostname);
    setVal('d_source', s.audio.source);
    setVal('d_volume', s.audio.master_volume);
    setVal('d_crossover', s.audio.crossover_hz);
    setVal('d_delays', (s.delays.left_ms||0)+'/'+(s.delays.right_ms||0)+'/'+(s.delays.sub_ms||0)+' ms');
    setVal('d_sats', (s.satellites.left||'?')+' / '+(s.satellites.right||'?'));
    setVal('d_cpu', s.system.cpu_load_percent+'%');
    setVal('d_heap', s.system.heap_free);
    setVal('d_psram', s.system.psram_free);
    setVal('d_ver', s.system.version);
    csrf = s.system.csrf || '';

    $('s_left').textContent = s.satellites.left;
    $('s_right').textContent = s.satellites.right;

    $('a_vol').value = s.audio.master_volume; setVal('a_volLabel', s.audio.master_volume);
    $('a_volLeft').value = s.audio.left_volume; setVal('a_volLeftLabel', s.audio.left_volume);
    $('a_volRight').value = s.audio.right_volume; setVal('a_volRightLabel', s.audio.right_volume);
    $('a_volSub').value = s.audio.sub_volume; setVal('a_volSubLabel', s.audio.sub_volume);
    $('a_xo').value = s.audio.crossover_hz; setVal('a_xoLabel', s.audio.crossover_hz);

    $('dlyL').value = s.delays.left_ms; setVal('dly_l', s.delays.left_ms);
    $('dlyR').value = s.delays.right_ms; setVal('dly_r', s.delays.right_ms);
    $('dlyS').value = s.delays.sub_ms; setVal('dly_s', s.delays.sub_ms);

    $('sysHost').value = s.system.hostname;
  } catch (e) {}
}

$('loginBtn').onclick = async () => {
  const pass = $('loginPass').value;
  const r = await fetch('/api/login', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({password:pass}) });
  if (r.ok) { const s = await r.json(); csrf = s.csrf; hideLogin(); toast('Вход выполнен'); }
  else { toast('Неверный пароль'); }
};
$('dLogoutBtn').onclick = async () => { await fetch('/api/logout', {method:'POST'}); showLogin(); };
$('dSaveBtn').onclick = async () => { await fetch('/api/config/save', {method:'POST', headers:{'X-CSRF-Token':csrf}}); toast('Сохранено'); };
$('dRebootBtn').onclick = async () => { if (confirm('Перезагрузить?')) { await fetch('/api/reboot', {method:'POST', headers:{'X-CSRF-Token':csrf}}); toast('Перезагрузка...'); } };
$('dMuteBtn').onclick = async () => { const s = await api('/api/status'); const m = !s.audio.mute; await fetch('/api/volume', {method:'PUT', headers:{'Content-Type':'application/json','X-CSRF-Token':csrf}, body:JSON.stringify({mute:m})}); refresh(); };

$('aApply').onclick = async () => {
  const body = { master_volume: +$('a_vol').value, left_volume: +$('a_volLeft').value, right_volume: +$('a_volRight').value, sub_volume: +$('a_volSub').value, crossover_hz: +$('a_xo').value };
  await fetch('/api/volume', {method:'PUT', headers:{'Content-Type':'application/json','X-CSRF-Token':csrf}, body:JSON.stringify(body)});
  toast('Применено');
};
$('dApply').onclick = async () => {
  const body = { left_ms: +$('dlyL').value, right_ms: +$('dlyR').value, sub_ms: +$('dlyS').value };
  await fetch('/api/delay', {method:'PUT', headers:{'Content-Type':'application/json','X-CSRF-Token':csrf}, body:JSON.stringify(body)});
  toast('Применено');
};
$('sysApply').onclick = async () => {
  await fetch('/api/system/hostname', {method:'PUT', headers:{'Content-Type':'application/json','X-CSRF-Token':csrf}, body:JSON.stringify({hostname:$('sysHost').value})});
  toast('Применено');
};

$('wApply').onclick = async () => {
  const mode = $('wMode').value;
  await fetch('/api/wifi/mode', {method:'PUT', headers:{'Content-Type':'application/json','X-CSRF-Token':csrf}, body:JSON.stringify({mode})});
  toast('Режим изменён');
};

$('uBtn').onclick = async () => {
  const file = $('uFile').files[0]; if (!file) return;
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/api/update', true);
  xhr.setRequestHeader('X-CSRF-Token', csrf);
  xhr.upload.onprogress = (e) => { if (e.lengthComputable) { const p = (e.loaded/e.total*100)|0; $('uFill').style.width = p+'%'; $('uMsg').textContent = p+'%'; } };
  xhr.onload = () => { $('uMsg').textContent = xhr.status===200?'Готово':'Ошибка '+xhr.status; };
  xhr.onerror = () => { $('uMsg').textContent = 'Ошибка загрузки'; };
  const fd = new FormData(); fd.append('update', file);
  xhr.send(fd);
};

const logLevels = { I: 'INFO', W: 'WARN', E: 'ERROR' };
async function refreshLogs() {
  const lvl = $('logLevel').value;
  const r = await fetch('/api/logs?level=' + encodeURIComponent(lvl));
  const lines = await r.text();
  $('logOut').textContent = lines;
}
setInterval(refreshLogs, 2000);
refreshLogs();

async function refreshWifi() {
  const r = await fetch('/api/wifi/scan');
  const list = await r.json();
  const el = $('wifiList');
  el.innerHTML = '';
  for (const n of list) {
    const row = document.createElement('div');
    row.style.cssText = 'padding:8px;border:1px solid #333;border-radius:4px;margin:4px 0;cursor:pointer;font-size:14px';
    row.textContent = n.ssid + '  (' + n.rssi + ' dBm' + (n.security === 'OPEN' ? ', open' : '') + ', ch ' + n.channel + ')';
    row.onclick = async () => {
      const pass = prompt('Пароль для '+n.ssid+':');
      if (pass === null) return;
      await fetch('/api/wifi/save', {method:'POST', headers:{'Content-Type':'application/json','X-CSRF-Token':csrf}, body:JSON.stringify({ssid:n.ssid, password:pass})});
      toast('Сохранено');
    };
    el.appendChild(row);
  }
}
setInterval(refreshWifi, 5000);
refreshWifi();
</script>
</body>
</html>
)rawliteral";

} // namespace audio21