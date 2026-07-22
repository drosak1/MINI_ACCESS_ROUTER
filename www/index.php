<?php
require __DIR__ . '/config.php';
?>
<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>Monitoring zbiorników</title>
<style>
*{box-sizing:border-box}
body{margin:0;padding:24px;font-family:Arial,sans-serif;background:#f3f4f6;color:#1f2937}
.container{max-width:1400px;margin:auto}
h1{margin:0 0 8px}
.status{margin:8px 0 18px;color:#6b7280}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:16px}
.card,.panel{background:#fff;border-radius:16px;padding:14px 16px;box-shadow:0 4px 16px rgba(0,0,0,.08)}
.card{position:relative;min-height:176px}
.card.offline{opacity:.68;filter:grayscale(.35)}
.card.error-card{border:2px solid #dc2626}
.card-top{display:flex;justify-content:space-between;align-items:flex-start;gap:10px}
.name{font-size:27px;font-weight:700;line-height:1}
.source{font-size:13px;color:#9ca3af;margin-top:3px}
.battery-top{display:flex;align-items:center;gap:8px;white-space:nowrap}
.battery-value{font-size:14px;font-weight:700}
.battery-mini{position:relative;width:46px;height:20px;border:2px solid #9ca3af;border-radius:6px;padding:2px}
.battery-mini::after{content:"";position:absolute;right:-6px;top:5px;width:4px;height:7px;background:#9ca3af;border-radius:0 3px 3px 0}
.battery-fill{height:100%;border-radius:2px;background:#16a34a}
.battery-fill.warn{background:#d97706}
.battery-fill.bad{background:#dc2626}
.level-row{display:flex;justify-content:space-between;align-items:center;margin-top:12px}
.level-label{font-size:19px;color:#6b7280}
.level-value{font-size:38px;font-weight:800;line-height:1}
.ok{color:#15803d}.warn{color:#b45309}.bad{color:#b91c1c}
.level-bar{height:20px;background:#e5e7eb;border-radius:10px;overflow:hidden;margin-top:9px}
.level-fill{height:100%;background:#2563eb;border-radius:10px}
.time-row{display:flex;justify-content:space-between;align-items:center;margin-top:10px}
.time{font-size:14px;color:#6b7280}
.online{font-size:11px;font-weight:700;color:#15803d}
.offline-label{font-size:11px;font-weight:700;color:#dc2626}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:4px;background:#16a34a}
.dot.offline-dot{background:#dc2626}
.panel{margin-top:20px;overflow-x:auto}
button{padding:10px 16px;border:0;border-radius:8px;background:#2563eb;color:#fff;font-weight:bold;cursor:pointer}
table{width:100%;min-width:750px;margin-top:18px;border-collapse:collapse}
th,td{padding:10px;border-bottom:1px solid #e5e7eb;text-align:left}
th{background:#f9fafb}
.debug{margin-top:12px;padding:12px;background:#fee2e2;color:#991b1b;border-radius:8px;white-space:pre-wrap;display:none}
@media(max-width:600px){
  body{padding:12px}
  .grid{grid-template-columns:1fr}
  .name{font-size:24px}
  .level-value{font-size:35px}
}
</style>
</head>
<body>
<div class="container">

<div class="status" id="status">Odświeżanie co <?php echo REFRESH_SECONDS; ?> s.</div>

<div class="grid" id="cards"></div>

<div class="panel">
<button id="refreshButton">Odśwież teraz</button>
<div id="debug" class="debug"></div>

<table>
<thead>
<tr>
<th>Nazwa</th>
<th>Źródło</th>
<th>Poziom</th>
<th>Bateria</th>
<th>Status</th>
<th>Ostatni pomiar</th>
</tr>
</thead>
<tbody id="rows"></tbody>
</table>
</div>
</div>

<script>
const REFRESH_SECONDS=<?php echo REFRESH_SECONDS; ?>;
const OFFLINE_AFTER_SECONDS=<?php echo OFFLINE_AFTER_SECONDS; ?>;
const MIN_BATTERY=2000;
const MAX_BATTERY=2800;

const clamp=(v,min,max)=>Math.max(min,Math.min(max,v));

function batteryClass(v){
    if(v>=2300)return'ok';
    if(v>=2000)return'warn';
    return'bad';
}

function levelClass(v){
    if(v>=70)return'ok';
    if(v>=30)return'warn';
    return'bad';
}

function formatTimestamp(ts){
    return ts ? String(ts).replace('T',' ').replace(/(?:Z|[+-]\d{2}:\d{2})$/,'') : '';
}

function esc(v){
    return String(v)
      .replace(/&/g,'&amp;')
      .replace(/</g,'&lt;')
      .replace(/>/g,'&gt;')
      .replace(/"/g,'&quot;')
      .replace(/'/g,'&#039;');
}

function isOffline(item){
    if(item.error || !item.timestamp) return true;

    const time=new Date(item.timestamp).getTime();
    if(Number.isNaN(time)) return false;

    return (Date.now()-time) > OFFLINE_AFTER_SECONDS*1000;
}

function cards(items){
return items.map(item=>{
    const level=Number(item.value)||0;
    const battery=Number(item.battery)||0;
    const lp=clamp(level,0,100);
    const bp=clamp(((battery-MIN_BATTERY)/(MAX_BATTERY-MIN_BATTERY))*100,0,100);
    const offline=isOffline(item);
    const time=item.timestamp?formatTimestamp(item.timestamp):(item.error||'Brak danych');

    return `<article class="card${item.error?' error-card':''}${offline?' offline':''}">
        <div class="card-top">
            <div>
                <div class="name">${esc(item.name)}</div>
                <div class="source">${esc(item.source)}</div>
            </div>

            <div class="battery-top">
                <span class="battery-value ${batteryClass(battery)}">
                    ${item.error?'Błąd':esc(battery)+' mV'}
                </span>

                <div class="battery-mini">
                    <div class="battery-fill ${batteryClass(battery)}"
                         style="width:${bp}%"></div>
                </div>
            </div>
        </div>

        <div class="level-row">
            <span class="level-label">Poziom</span>
            <span class="level-value ${levelClass(level)}">
                ${item.error?'--':esc(level)+'%'}
            </span>
        </div>

        <div class="level-bar">
            <div class="level-fill" style="width:${lp}%"></div>
        </div>

        <div class="time-row">
            <span class="time">${esc(time)}</span>
            <span class="${offline?'offline-label':'online'}">
                <span class="dot ${offline?'offline-dot':''}"></span>
                ${offline?'OFFLINE':'ONLINE'}
            </span>
        </div>
    </article>`;
}).join('');
}

function rows(items){
return items.map(item=>{
    const offline=isOffline(item);
    const time=item.timestamp?formatTimestamp(item.timestamp):(item.error||'-');

    return `<tr>
        <td>${esc(item.name)}</td>
        <td>${esc(item.source)}</td>
        <td>${item.value!=null?esc(item.value)+'%':'Brak'}</td>
        <td>${item.battery!=null?esc(item.battery)+' mV':'Brak'}</td>
        <td>${offline?'OFFLINE':'ONLINE'}</td>
        <td>${esc(time)}</td>
    </tr>`;
}).join('');
}

async function loadData(){
    const status=document.getElementById('status');
    const debug=document.getElementById('debug');

    status.textContent='Pobieranie danych...';
    debug.style.display='none';

    try{
        const r=await fetch('api.php?_='+Date.now(),{cache:'no-store'});
        const t=await r.text();

        if(!r.ok) throw new Error('HTTP '+r.status+'\n'+t);

        const items=JSON.parse(t);

        document.getElementById('cards').innerHTML=cards(items);
        document.getElementById('rows').innerHTML=rows(items);

        status.textContent=
            'Ostatnie odświeżenie: '+
            new Date().toLocaleTimeString('pl-PL')+
            ' | Interwał: '+REFRESH_SECONDS+' s';
    }catch(e){
        status.textContent='Błąd pobierania danych';
        debug.textContent=e.message;
        debug.style.display='block';
    }
}

document.getElementById('refreshButton').addEventListener('click',loadData);

loadData();
setInterval(loadData,REFRESH_SECONDS*1000);
</script>
</body>
</html>
