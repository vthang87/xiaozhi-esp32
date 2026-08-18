#include "es3c28p_web_server.h"

#include "application.h"
#include "audio_codec.h"
#include "board.h"
#include "display.h"
#include "es3c28p_display.h"
#include "lvgl_display.h"
#include "sd_music_player.h"
#include "settings.h"
#include "system_info.h"
#include "theme_package.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <wifi_manager.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr char kTag[] = "ES3C28PWeb";

constexpr char kIndexHtml[] = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light dark">
<title>ES3C28P Device</title>
<style>
:root{--bg:#d9f0e7;--surface:#fffaf0;--surface-2:#edf7f2;--text:#24423d;--muted:#57716b;--line:#a7cfc3;--accent:#d95845;--accent-ink:#fffaf0;--ok:#397b4b;--error:#a63d32;--radius:16px;--shadow:0 16px 40px rgba(36,66,61,.12)}
@media(prefers-color-scheme:dark){:root{--bg:#142825;--surface:#1d3530;--surface-2:#25413b;--text:#eff8f3;--muted:#b5cbc3;--line:#446b61;--accent:#f18470;--accent-ink:#172a26;--ok:#8fd29a;--error:#ff9b90;--shadow:0 16px 40px rgba(0,0,0,.24)}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:16px/1.45 ui-rounded,"SF Pro Rounded","Avenir Next",system-ui,sans-serif}button,input{font:inherit}button{cursor:pointer}.shell{width:min(920px,calc(100% - 32px));margin:0 auto;padding:32px 0 56px}.top{display:grid;grid-template-columns:1fr auto;gap:20px;align-items:end;margin-bottom:24px}.brand{margin:0;font-size:clamp(28px,6vw,52px);line-height:1;letter-spacing:-.045em}.sub{margin:8px 0 0;color:var(--muted);max-width:42ch}.live{padding:9px 12px;border:1px solid var(--line);border-radius:999px;color:var(--ok);font-weight:700;white-space:nowrap}.status{display:grid;grid-template-columns:repeat(4,1fr);gap:1px;overflow:hidden;border:1px solid var(--line);border-radius:var(--radius);background:var(--line);box-shadow:var(--shadow);margin-bottom:20px}.metric{background:var(--surface);padding:16px;min-width:0}.metric span{display:block;color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.06em;text-transform:uppercase}.metric strong{display:block;margin-top:5px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.grid{display:grid;grid-template-columns:1fr 1.15fr;gap:20px}.panel{background:var(--surface);border:1px solid var(--line);border-radius:var(--radius);padding:22px;box-shadow:var(--shadow)}h2{margin:0 0 5px;font-size:21px;letter-spacing:-.02em}.help{margin:0 0 20px;color:var(--muted);font-size:14px}.field{display:grid;gap:8px;margin-top:18px}.field label{font-weight:750}.range-row{display:grid;grid-template-columns:1fr 54px;gap:12px;align-items:center}input[type=range]{width:100%;accent-color:var(--accent)}input[type=url],input[type=file],input[type=text]{width:100%;min-height:48px;border:1px solid var(--line);border-radius:12px;background:var(--surface-2);color:var(--text);padding:11px 12px}input:focus-visible,button:focus-visible{outline:3px solid color-mix(in srgb,var(--accent),transparent 55%);outline-offset:2px}.value{text-align:right;font-variant-numeric:tabular-nums;font-weight:750}.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:20px}.button{min-height:46px;border:1px solid var(--line);border-radius:12px;padding:10px 16px;background:var(--surface-2);color:var(--text);font-weight:800;white-space:nowrap}.button.primary{border-color:var(--accent);background:var(--accent);color:var(--accent-ink)}.button:active{transform:translateY(1px)}.button:disabled{cursor:wait;opacity:.55}.tabs{display:grid;grid-template-columns:1fr 1fr;gap:6px;padding:5px;background:var(--surface-2);border-radius:14px;margin:18px 0}.tab{border:0;border-radius:10px;padding:10px;background:transparent;color:var(--muted);font-weight:800}.tab.active{background:var(--surface);color:var(--text);box-shadow:0 2px 8px rgba(36,66,61,.09)}.tab-panel[hidden]{display:none}.notice{min-height:24px;margin-top:14px;color:var(--muted);font-size:14px}.notice.ok{color:var(--ok)}.notice.error{color:var(--error)}.maintenance,.media{grid-column:1/-1}.maintenance{display:flex;align-items:center;justify-content:space-between;gap:16px}.maintenance .help{margin:4px 0 0}.danger{color:var(--error)}.path-row{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin:0 0 12px}.path-row strong{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.path-row select{min-height:42px;border:1px solid var(--line);border-radius:12px;background:var(--surface-2);color:var(--text);padding:8px 10px}.media-list{border:1px solid var(--line);border-radius:12px;overflow:auto;background:var(--surface-2);max-height:360px;min-height:88px}.media-row{display:grid;grid-template-columns:minmax(0,1fr) auto auto;gap:8px;align-items:center;padding:10px 12px;border-top:1px solid var(--line)}.media-row:first-child{border-top:0}.media-row.dir{cursor:pointer}.media-row .button{min-height:36px;padding:6px 10px}.media-name{min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:750}.media-meta{color:var(--muted);font-size:13px;font-variant-numeric:tabular-nums}.media-empty{padding:18px;color:var(--muted)}.row-actions{display:flex;gap:6px;flex-wrap:wrap}.player{display:grid;gap:10px;margin:0 0 18px;padding:16px;border:1px solid var(--line);border-radius:14px;background:var(--surface-2)}.player-now strong{display:block;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.player-now span{display:block;margin-top:3px;color:var(--muted);font-size:13px}.player-progress{display:grid;grid-template-columns:auto 1fr auto;gap:10px;align-items:center;color:var(--muted);font-size:13px;font-variant-numeric:tabular-nums}.player-bar{height:8px;border-radius:999px;background:var(--line);overflow:hidden}.player-fill{height:100%;width:0;background:var(--accent);border-radius:inherit}.player-controls{margin-top:2px}.player-controls .button{flex:1}.media-row.playing{background:color-mix(in srgb,var(--accent),transparent 88%)}.media-row:not(.dir) .media-name{cursor:pointer}
@media(max-width:720px){.shell{width:min(100% - 20px,560px);padding-top:20px}.top{grid-template-columns:1fr;align-items:start}.live{justify-self:start}.status{grid-template-columns:1fr 1fr}.grid{grid-template-columns:1fr}.maintenance{align-items:flex-start;flex-direction:column}.maintenance .actions{margin-top:0;width:100%}.maintenance .button{flex:1}.panel{padding:18px}.media-row{grid-template-columns:minmax(0,1fr) auto}.row-actions{grid-column:1/-1}}
@media(prefers-reduced-motion:no-preference){.button,.tab{transition:transform .12s ease,background-color .18s ease,color .18s ease}}
</style>
</head>
<body>
<main class="shell">
  <header class="top">
    <div><h1 class="brand">Your ES3C28P</h1><p class="sub">Device settings, music library, and independent theme updates on your local network.</p></div>
    <div class="live" id="connection">Connecting</div>
  </header>
  <section class="status" aria-label="Device status">
    <div class="metric"><span>IP address</span><strong id="ip">...</strong></div>
    <div class="metric"><span>Wi-Fi</span><strong id="ssid">...</strong></div>
    <div class="metric"><span>Theme</span><strong id="theme">...</strong></div>
    <div class="metric"><span>Battery</span><strong id="battery">...</strong></div>
  </section>
  <div class="grid">
    <section class="panel">
      <h2>Device controls</h2>
      <p class="help">Changes are saved on the device.</p>
      <div class="field"><label for="volume">Speaker volume</label><div class="range-row"><input id="volume" type="range" min="0" max="100" step="5"><output id="volumeValue" class="value">0%</output></div></div>
      <div class="field"><label for="brightness">Screen brightness</label><div class="range-row"><input id="brightness" type="range" min="5" max="100" step="5"><output id="brightnessValue" class="value">0%</output></div></div>
      <div class="actions"><button class="button primary" id="saveSettings">Save controls</button></div>
      <p class="notice" id="settingsNotice" role="status"></p>
    </section>
    <section class="panel">
      <h2>Install a theme</h2>
      <p class="help">Packages are validated before the device restarts.</p>
      <div class="tabs" role="tablist">
        <button class="tab active" data-tab="file" role="tab">Theme file</button>
        <button class="tab" data-tab="url" role="tab">Direct URL</button>
      </div>
      <div class="tab-panel" id="filePanel">
        <div class="field"><label for="themeFile">Select .theme.bin</label><input id="themeFile" type="file" accept=".bin,.theme.bin,application/octet-stream"></div>
        <div class="actions"><button class="button primary" id="uploadFile">Upload file</button></div>
      </div>
      <div class="tab-panel" id="urlPanel" hidden>
        <div class="field"><label for="themeUrl">Package URL</label><input id="themeUrl" type="url" inputmode="url" placeholder="https://example.com/theme.bin"></div>
        <div class="actions"><button class="button primary" id="installUrl">Install URL</button></div>
      </div>
      <p class="notice" id="themeNotice" role="status"></p>
    </section>
    <section class="panel media">
      <h2>Music library</h2>
      <p class="help">Play, pause, and skip from this page. Browse folders, create or rename folders, and drag items into a folder to move them. Prefer a direct MP3 URL so the device downloads the file itself; browser upload is slower. Drag a track onto another track to change playback order.</p>
      <div class="player" aria-label="Now playing">
        <div class="player-now"><strong id="playerTitle">No track</strong><span id="playerMeta">Stopped</span></div>
        <div class="player-progress"><span id="playerElapsed">0:00</span><div class="player-bar"><div class="player-fill" id="playerFill"></div></div><span id="playerDuration">0:00</span></div>
        <div class="actions player-controls"><button class="button" id="playerPrev">Previous</button><button class="button primary" id="playerToggle">Play</button><button class="button" id="playerNext">Next</button></div>
      </div>
      <div class="path-row">
        <button class="button" id="mediaUp">Up</button>
        <strong id="mediaPath">/</strong>
        <select id="mediaSort" aria-label="Sort">
          <option value="name">Name</option>
          <option value="size">Size</option>
          <option value="playlist">Playlist order</option>
        </select>
      </div>
      <div class="media-list" id="mediaList"></div>
      <div class="field"><label for="folderName">New folder in this location</label><input id="folderName" type="text" maxlength="64" placeholder="Podcasts" autocomplete="off"></div>
      <div class="field"><label for="mediaUrl">Download MP3 from a URL</label><input id="mediaUrl" type="url" inputmode="url" placeholder="https://example.com/song.mp3"></div>
      <div class="field"><label for="mediaUrlName">Save as (optional)</label><input id="mediaUrlName" type="text" maxlength="64" placeholder="song.mp3" autocomplete="off"></div>
      <div class="field"><label for="mediaFiles">Or upload MP3 files from this computer</label><input id="mediaFiles" type="file" accept=".mp3,audio/mpeg" multiple></div>
      <div class="actions"><button class="button" id="mediaMkdir">Create folder</button><button class="button primary" id="mediaDownload">Download URL</button><button class="button" id="mediaUpload">Upload</button></div>
      <p class="notice" id="mediaNotice" role="status"></p>
    </section>
    <section class="panel maintenance">
      <div><h2>Network and restart</h2><p class="help">Wi-Fi setup restarts the device in configuration mode.</p></div>
      <div class="actions"><button class="button" id="wifiReset">Set up Wi-Fi</button><button class="button danger" id="reboot">Restart</button></div>
    </section>
  </div>
</main>
<script>
const $=id=>document.getElementById(id);let token='';let playerState={playing:false,path:'',name:'',elapsed:0,duration:0,track_index:0,track_count:0,controllable:true};let playerTimer=0;
function notice(id,text,type=''){const el=$(id);el.textContent=text;el.className='notice '+type}
function busy(button,on,label){if(!button.dataset.label)button.dataset.label=button.textContent;button.disabled=on;button.textContent=on?label:button.dataset.label}
async function api(path,options={}){options.headers={...(options.headers||{}),'X-Device-Token':token};const response=await fetch(path,options);const data=await response.json().catch(()=>({success:false,error:'Invalid device response'}));if(!response.ok||data.success===false)throw new Error(data.error||'Request failed');return data}
async function load(){try{const data=await fetch('/api/status',{cache:'no-store'}).then(r=>r.json());token=data.token;$('ip').textContent=data.ip||'Not connected';$('ssid').textContent=data.ssid||'Not connected';$('theme').textContent=data.theme_name;$('battery').textContent=data.battery_level+'%';$('volume').value=data.volume;$('brightness').value=data.brightness;$('volumeValue').textContent=data.volume+'%';$('brightnessValue').textContent=data.brightness+'%';$('connection').textContent='Connected';$('connection').classList.add('ok');await loadMedia();await refreshPlayer();if(!playerTimer)playerTimer=setInterval(()=>{if(!document.hidden)refreshPlayer()},1000)}catch(error){$('connection').textContent='Offline';notice('settingsNotice',error.message,'error')}}
for(const id of ['volume','brightness'])$(id).addEventListener('input',event=>$(id+'Value').textContent=event.target.value+'%');
document.querySelectorAll('.tab').forEach(tab=>tab.addEventListener('click',()=>{document.querySelectorAll('.tab').forEach(x=>x.classList.toggle('active',x===tab));$('filePanel').hidden=tab.dataset.tab!=='file';$('urlPanel').hidden=tab.dataset.tab!=='url'}));
$('saveSettings').addEventListener('click',async()=>{const button=$('saveSettings');busy(button,true,'Saving');notice('settingsNotice','Saving controls...');try{await api('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({volume:Number($('volume').value),brightness:Number($('brightness').value)})});notice('settingsNotice','Controls saved.','ok')}catch(error){notice('settingsNotice',error.message,'error')}finally{busy(button,false)}});
$('uploadFile').addEventListener('click',async()=>{const file=$('themeFile').files[0];if(!file){notice('themeNotice','Choose a theme package first.','error');return}const button=$('uploadFile');busy(button,true,'Uploading');notice('themeNotice','Uploading and validating '+file.name+'...');try{await api('/api/theme/file',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:await file.arrayBuffer()});notice('themeNotice','Theme installed. Device is restarting.','ok')}catch(error){notice('themeNotice',error.message,'error');busy(button,false)}});
$('installUrl').addEventListener('click',async()=>{const url=$('themeUrl').value.trim();if(!url){notice('themeNotice','Enter a direct package URL.','error');return}const button=$('installUrl');busy(button,true,'Installing');notice('themeNotice','Downloading and validating the package...');try{await api('/api/theme/url',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({url})});notice('themeNotice','Theme installed. Device is restarting.','ok')}catch(error){notice('themeNotice',error.message,'error');busy(button,false)}});
async function maintenance(path,button,message){busy(button,true,'Working');try{await api(path,{method:'POST'});notice('settingsNotice',message,'ok')}catch(error){notice('settingsNotice',error.message,'error');busy(button,false)}}
$('wifiReset').addEventListener('click',()=>{if(confirm('Restart and enter Wi-Fi setup mode?'))maintenance('/api/wifi/reset',$('wifiReset'),'Restarting in Wi-Fi setup mode.')});
$('reboot').addEventListener('click',()=>{if(confirm('Restart the device now?'))maintenance('/api/reboot',$('reboot'),'Restarting device.')});
let mediaDir='/';let mediaPlaylist=[];let mediaEntries=[];
function formatTime(s){s=Math.max(0,Math.floor(Number(s)||0));return Math.floor(s/60)+':'+String(s%60).padStart(2,'0')}
function renderPlayer(data){if(data){playerState.playing=!!data.playing;playerState.path=data.path||'';playerState.name=data.name||'';playerState.elapsed=Number(data.elapsed)||0;playerState.duration=Number(data.duration)||0;playerState.track_index=Number(data.track_index)||0;playerState.track_count=Number(data.track_count)||0;playerState.controllable=data.controllable!==false}const locked=!playerState.controllable;$('playerTitle').textContent=playerState.name||'No track';$('playerMeta').textContent=!playerState.track_count?'No MP3 files':(locked?'Locked during AI chat':(playerState.playing?'Playing':'Paused')+' · '+playerState.track_index+' / '+playerState.track_count);$('playerElapsed').textContent=formatTime(playerState.elapsed);$('playerDuration').textContent=formatTime(playerState.duration);$('playerFill').style.width=(playerState.duration?Math.min(100,playerState.elapsed*100/playerState.duration):0)+'%';$('playerToggle').textContent=playerState.playing?'Pause':'Play';$('playerPrev').disabled=locked||!playerState.track_count;$('playerNext').disabled=locked||!playerState.track_count;$('playerToggle').disabled=locked||!playerState.track_count;document.querySelectorAll('.media-row').forEach(row=>row.classList.toggle('playing',row.dataset.path===playerState.path))}
async function refreshPlayer(){if(!token)return;try{renderPlayer(await api('/api/media/player'))}catch(error){}}
async function controlPlayer(action,path){try{const data=await api('/api/media/player',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action,path:path||''})});if(action==='play')data.playing=true;else if(action==='pause')data.playing=false;else if(action==='toggle')data.playing=!playerState.playing;if(path){data.path=path;data.name=path.split('/').pop()}renderPlayer(data);setTimeout(refreshPlayer,250)}catch(error){notice('mediaNotice',error.message,'error')}}
function formatSize(n){if(n<1024)return n+' B';if(n<1048576)return (n/1024).toFixed(1)+' KB';return (n/1048576).toFixed(1)+' MB'}
function folderFiles(){return mediaEntries.filter(entry=>!entry.dir).map(entry=>entry.path)}
async function loadMedia(){try{const data=await api('/api/media?dir='+encodeURIComponent(mediaDir)+'&sort='+encodeURIComponent($('mediaSort').value));mediaDir=data.dir||'/';mediaPlaylist=data.playlist||[];mediaEntries=data.entries||[];$('mediaPath').textContent=mediaDir;$('mediaUp').disabled=!data.parent;const list=$('mediaList');list.innerHTML='';if(!mediaEntries.length){list.innerHTML='<div class="media-empty">This folder is empty. Create a folder, download an MP3 URL, or upload files.</div>';return}const isFileDrag=event=>event.dataTransfer&&[...event.dataTransfer.types].includes('Files');mediaEntries.forEach(entry=>{const row=document.createElement('div');row.className='media-row'+(entry.dir?' dir':'')+(entry.path===playerState.path?' playing':'');row.dataset.path=entry.path;row.draggable=true;const name=document.createElement('div');name.className='media-name';name.textContent=(entry.dir?'Folder · ':'')+entry.name;const meta=document.createElement('div');meta.className='media-meta';meta.textContent=entry.dir?'Open':formatSize(entry.size);const actions=document.createElement('div');actions.className='row-actions';if(!entry.dir){const play=document.createElement('button');play.className='button';play.textContent='Play';play.addEventListener('click',event=>{event.stopPropagation();controlPlayer('play',entry.path)});const up=document.createElement('button');up.className='button';up.textContent='Up';up.addEventListener('click',event=>{event.stopPropagation();moveTrack(entry.path,-1)});const down=document.createElement('button');down.className='button';down.textContent='Down';down.addEventListener('click',event=>{event.stopPropagation();moveTrack(entry.path,1)});actions.append(play,up,down)}const rename=document.createElement('button');rename.className='button';rename.textContent='Rename';rename.addEventListener('click',event=>{event.stopPropagation();renameMedia(entry)});const remove=document.createElement('button');remove.className='button danger';remove.textContent='Remove';remove.addEventListener('click',event=>{event.stopPropagation();removeMedia(entry)});actions.append(rename,remove);row.append(name,meta,actions);if(entry.dir)row.addEventListener('click',()=>{mediaDir=entry.path;loadMedia()});else name.addEventListener('click',()=>controlPlayer('play',entry.path));row.addEventListener('dragstart',event=>{event.dataTransfer.setData('text/plain',(entry.dir?'dir:':'file:')+entry.path)});row.addEventListener('dragover',event=>{if(!isFileDrag(event))event.preventDefault()});row.addEventListener('drop',event=>{if(isFileDrag(event))return;event.preventDefault();event.stopPropagation();const from=event.dataTransfer.getData('text/plain');const path=from.replace(/^(dir|file):/,'');if(!path||path===entry.path)return;if(entry.dir)moveIntoFolder(path,entry.path);else if(from.startsWith('file:'))movePath(path,entry.path)});list.append(row)})}catch(error){notice('mediaNotice',error.message,'error')}}
async function saveOrder(order){await api('/api/media/order',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({paths:order})});$('mediaSort').value='playlist';await loadMedia()}
async function moveTrack(path,dir){const files=folderFiles();const index=files.indexOf(path);const next=index+dir;if(index<0||next<0||next>=files.length)return;const order=mediaPlaylist.slice();const fromIndex=order.indexOf(path);const toIndex=order.indexOf(files[next]);if(fromIndex<0||toIndex<0)return;const swap=order[fromIndex];order[fromIndex]=order[toIndex];order[toIndex]=swap;try{notice('mediaNotice','Saving playlist...');await saveOrder(order);notice('mediaNotice','Playlist updated.','ok')}catch(error){notice('mediaNotice',error.message,'error')}}
async function movePath(from,to){const order=mediaPlaylist.slice();const fromIndex=order.indexOf(from);let toIndex=order.indexOf(to);if(fromIndex<0||toIndex<0||fromIndex===toIndex)return;order.splice(fromIndex,1);if(fromIndex<toIndex)toIndex-=1;order.splice(toIndex,0,from);try{notice('mediaNotice','Saving playlist...');await saveOrder(order);notice('mediaNotice','Playlist updated.','ok')}catch(error){notice('mediaNotice',error.message,'error')}}
async function moveIntoFolder(path,dir){try{notice('mediaNotice','Moving...');await api('/api/media/move',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({path,dir})});notice('mediaNotice','Moved into folder.','ok');await loadMedia()}catch(error){notice('mediaNotice',error.message,'error')}}
async function renameMedia(entry){const name=prompt('Rename '+entry.name,entry.name);if(!name||name===entry.name)return;try{notice('mediaNotice','Renaming...');await api('/api/media/rename',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({path:entry.path,name})});notice('mediaNotice','Renamed to '+name+'.','ok');await loadMedia()}catch(error){notice('mediaNotice',error.message,'error')}}
async function removeMedia(entry){const message=entry.dir?'Remove folder '+entry.name+' and everything inside?':'Remove '+entry.name+'?';if(!confirm(message))return;try{notice('mediaNotice','Removing...');await api('/api/media/remove',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({path:entry.path})});notice('mediaNotice','Removed '+entry.name+'.','ok');await loadMedia()}catch(error){notice('mediaNotice',error.message,'error')}}
async function createFolder(){const name=$('folderName').value.trim();if(!name){notice('mediaNotice','Enter a folder name.','error');return}const button=$('mediaMkdir');busy(button,true,'Creating');try{await api('/api/media/mkdir',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({dir:mediaDir,name})});notice('mediaNotice','Created folder '+name+'.','ok');$('folderName').value='';await loadMedia()}catch(error){notice('mediaNotice',error.message,'error')}finally{busy(button,false)}}
async function uploadFiles(files){const tracks=[...files].filter(file=>file.name.toLowerCase().endsWith('.mp3'));if(!tracks.length){notice('mediaNotice','Choose MP3 files to upload.','error');return}const button=$('mediaUpload');busy(button,true,'Uploading');try{for(const file of tracks){await uploadOne(file)}notice('mediaNotice','Uploaded '+tracks.length+' file'+(tracks.length===1?'':'s')+'.','ok');$('mediaFiles').value='';await loadMedia()}catch(error){notice('mediaNotice',error.message,'error')}finally{busy(button,false)}}
function uploadOne(file){return new Promise((resolve,reject)=>{const xhr=new XMLHttpRequest();const started=Date.now();xhr.open('POST','/api/media/upload');xhr.setRequestHeader('X-Device-Token',token);xhr.setRequestHeader('Content-Type','application/octet-stream');xhr.setRequestHeader('X-Media-Dir',encodeURIComponent(mediaDir));xhr.setRequestHeader('X-Filename',encodeURIComponent(file.name));xhr.upload.onprogress=event=>{if(!event.lengthComputable)return;const pct=Math.round(event.loaded*100/event.total);const seconds=Math.max((Date.now()-started)/1000,0.1);const rate=Math.round(event.loaded/1024/seconds);notice('mediaNotice','Uploading '+file.name+' '+pct+'% ('+rate+' KB/s)')};xhr.onload=()=>{let data={};try{data=JSON.parse(xhr.responseText||'{}')}catch(error){reject(new Error('Invalid device response'));return}if(xhr.status>=200&&xhr.status<300&&data.success!==false){resolve(data);return}reject(new Error(data.error||'Request failed'))};xhr.onerror=()=>reject(new Error('Upload was interrupted'));xhr.send(file)})}
async function downloadUrl(){const url=$('mediaUrl').value.trim();if(!url){notice('mediaNotice','Enter a direct MP3 URL.','error');return}const name=$('mediaUrlName').value.trim();const button=$('mediaDownload');busy(button,true,'Downloading');notice('mediaNotice','Device is downloading the MP3...');try{await api('/api/media/download',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({url,dir:mediaDir,name})});notice('mediaNotice','Downloaded to this folder.','ok');$('mediaUrl').value='';$('mediaUrlName').value='';await loadMedia()}catch(error){notice('mediaNotice',error.message,'error')}finally{busy(button,false)}}
$('mediaUp').addEventListener('click',()=>{const parts=mediaDir.split('/').filter(Boolean);parts.pop();mediaDir=parts.length?'/'+parts.join('/'):'/';loadMedia()});
$('mediaSort').addEventListener('change',loadMedia);
$('mediaMkdir').addEventListener('click',createFolder);
$('folderName').addEventListener('keydown',event=>{if(event.key==='Enter')createFolder()});
$('mediaDownload').addEventListener('click',downloadUrl);
$('mediaUrl').addEventListener('keydown',event=>{if(event.key==='Enter')downloadUrl()});
$('mediaUpload').addEventListener('click',()=>uploadFiles($('mediaFiles').files));
$('playerPrev').addEventListener('click',()=>controlPlayer('previous'));
$('playerToggle').addEventListener('click',()=>controlPlayer(playerState.playing?'pause':'play'));
$('playerNext').addEventListener('click',()=>controlPlayer('next'));
$('mediaList').addEventListener('dragover',event=>event.preventDefault());
$('mediaList').addEventListener('drop',event=>{event.preventDefault();if(event.dataTransfer.files.length)uploadFiles(event.dataTransfer.files)});
load();
</script>
</body>
</html>)HTML";

std::string UrlDecode(const char* value) {
    std::string decoded;
    for (size_t i = 0; value[i] != '\0'; ++i) {
        if (value[i] == '%' && value[i + 1] != '\0' && value[i + 2] != '\0') {
            char hex[3] = {value[i + 1], value[i + 2], 0};
            decoded.push_back(static_cast<char>(strtol(hex, nullptr, 16)));
            i += 2;
        } else if (value[i] == '+') {
            decoded.push_back(' ');
        } else {
            decoded.push_back(value[i]);
        }
    }
    return decoded;
}

std::string FilenameFromUrl(const std::string& url) {
    std::string path = url;
    const auto query = path.find_first_of("?#");
    if (query != std::string::npos) {
        path.resize(query);
    }
    const auto slash = path.find_last_of('/');
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    return UrlDecode(name.c_str());
}

bool GetQueryValue(httpd_req_t* req, const char* key, std::string& value) {
    const size_t length = httpd_req_get_url_query_len(req);
    if (length == 0) {
        return false;
    }
    std::vector<char> query(length + 1, 0);
    if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK) {
        return false;
    }
    char param[1024];
    if (httpd_query_key_value(query.data(), key, param, sizeof(param)) != ESP_OK) {
        return false;
    }
    value = UrlDecode(param);
    return true;
}
bool GetHeaderValue(httpd_req_t* req, const char* key, std::string& value) {
    const size_t length = httpd_req_get_hdr_value_len(req, key);
    if (length == 0 || length > 1024) {
        return false;
    }
    std::vector<char> header(length + 1, 0);
    if (httpd_req_get_hdr_value_str(req, key, header.data(), header.size()) != ESP_OK) {
        return false;
    }
    value = UrlDecode(header.data());
    return true;
}

void DrainRequest(httpd_req_t* req) {
    char buffer[256];
    while (httpd_req_recv(req, buffer, sizeof(buffer)) > 0) {
    }
}

void TuneUploadSocket(httpd_req_t* req) {
    const int fd = httpd_req_to_sockfd(req);
    if (fd < 0) {
        return;
    }
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    int receive_buffer = 32 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer, sizeof(receive_buffer));
}

class UploadPerformance {
public:
    UploadPerformance() {
        Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    }
    ~UploadPerformance() {
        if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
            Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        }
    }
};
}  // namespace

Es3c28pWebServer::Es3c28pWebServer() {
    char token[17];
    snprintf(token, sizeof(token), "%08lx%08lx",
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random()));
    access_token_ = token;
}

Es3c28pWebServer::~Es3c28pWebServer() {
    Stop();
}

std::string Es3c28pWebServer::url() const {
    const auto ip = WifiManager::GetInstance().GetIpAddress();
    return ip.empty() ? std::string() : "http://" + ip + "/";
}

void Es3c28pWebServer::SetMusicPlayer(SdMusicPlayer* music_player) {
    music_player_ = music_player;
}

bool Es3c28pWebServer::Start() {
    if (server_ != nullptr) {
        return true;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;
    config.stack_size = 16384;
    config.recv_wait_timeout = 60;
    config.send_wait_timeout = 20;
    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to start device web server");
        server_ = nullptr;
        return false;
    }
    const httpd_uri_t handlers[] = {
        {.uri = "/", .method = HTTP_GET, .handler = HandleIndex, .user_ctx = this},
        {.uri = "/api/status", .method = HTTP_GET, .handler = HandleStatus, .user_ctx = this},
        {.uri = "/api/settings", .method = HTTP_POST, .handler = HandleSettings, .user_ctx = this},
        {.uri = "/api/theme/file", .method = HTTP_POST, .handler = HandleThemeFile, .user_ctx = this},
        {.uri = "/api/theme/url", .method = HTTP_POST, .handler = HandleThemeUrl, .user_ctx = this},
        {.uri = "/api/media", .method = HTTP_GET, .handler = HandleMedia, .user_ctx = this},
        {.uri = "/api/media/upload", .method = HTTP_POST, .handler = HandleMediaUpload, .user_ctx = this},
        {.uri = "/api/media/download", .method = HTTP_POST, .handler = HandleMediaDownload, .user_ctx = this},
        {.uri = "/api/media/remove", .method = HTTP_POST, .handler = HandleMediaRemove, .user_ctx = this},
        {.uri = "/api/media/mkdir", .method = HTTP_POST, .handler = HandleMediaMkdir, .user_ctx = this},
        {.uri = "/api/media/rename", .method = HTTP_POST, .handler = HandleMediaRename, .user_ctx = this},
        {.uri = "/api/media/move", .method = HTTP_POST, .handler = HandleMediaMove, .user_ctx = this},
        {.uri = "/api/media/order", .method = HTTP_POST, .handler = HandleMediaOrder, .user_ctx = this},
        {.uri = "/api/media/player", .method = HTTP_GET, .handler = HandleMediaPlayer, .user_ctx = this},
        {.uri = "/api/media/player", .method = HTTP_POST, .handler = HandleMediaPlayer, .user_ctx = this},
        {.uri = "/api/wifi/reset", .method = HTTP_POST, .handler = HandleWifiReset, .user_ctx = this},
        {.uri = "/api/reboot", .method = HTTP_POST, .handler = HandleReboot, .user_ctx = this},
#if CONFIG_ES3C28P_SCREENSHOT_API
        {.uri = "/api/screenshot", .method = HTTP_GET, .handler = HandleScreenshot, .user_ctx = this},
#endif
    };
    for (const auto& handler : handlers) {
        if (httpd_register_uri_handler(server_, &handler) != ESP_OK) {
            ESP_LOGE(kTag, "Failed to register %s", handler.uri);
            Stop();
            return false;
        }
    }
    ESP_LOGI(kTag, "Device web server started at %s", url().c_str());
    return true;
}

void Es3c28pWebServer::Stop() {
    if (server_ != nullptr) {
        httpd_stop(server_);
        server_ = nullptr;
    }
}

bool Es3c28pWebServer::Authorize(httpd_req_t* req) const {
    const size_t length = httpd_req_get_hdr_value_len(req, "X-Device-Token");
    if (length != access_token_.size()) {
        SendJson(req, "{\"success\":false,\"error\":\"Unauthorized request\"}",
            "403 Forbidden");
        return false;
    }
    std::vector<char> token(length + 1, 0);
    if (httpd_req_get_hdr_value_str(req, "X-Device-Token",
            token.data(), token.size()) != ESP_OK || access_token_ != token.data()) {
        SendJson(req, "{\"success\":false,\"error\":\"Unauthorized request\"}",
            "403 Forbidden");
        return false;
    }
    return true;
}

void Es3c28pWebServer::SendJson(httpd_req_t* req, const char* json,
    const char* status) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

void Es3c28pWebServer::SendError(httpd_req_t* req, const char* message,
    const char* status) {
    auto* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", false);
    cJSON_AddStringToObject(root, "error", message);
    char* json = cJSON_PrintUnformatted(root);
    SendJson(req, json, status);
    cJSON_free(json);
    cJSON_Delete(root);
}

SdMusicPlayer* Es3c28pWebServer::MusicPlayer(httpd_req_t* req) const {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (self->music_player_ == nullptr) {
        SendError(req, "Music player is unavailable", "503 Service Unavailable");
        return nullptr;
    }
    return self->music_player_;
}

bool Es3c28pWebServer::ReceiveJson(httpd_req_t* req, std::string& body,
    size_t max_length) {
    if (req->content_len == 0 || req->content_len > max_length) {
        return false;
    }
    body.resize(req->content_len);
    size_t received = 0;
    while (received < body.size()) {
        const int count = httpd_req_recv(req, body.data() + received,
            body.size() - received);
        if (count <= 0) {
            return false;
        }
        received += count;
    }
    return true;
}

esp_err_t Es3c28pWebServer::HandleIndex(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Content-Security-Policy",
        "default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'");
    return httpd_resp_send(req, kIndexHtml, sizeof(kIndexHtml) - 1);
}

esp_err_t Es3c28pWebServer::HandleStatus(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    auto& board = Board::GetInstance();
    auto& wifi = WifiManager::GetInstance();
    int battery = 0;
    bool charging = false;
    bool discharging = false;
    board.GetBatteryLevel(battery, charging, discharging);
    auto* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "token", self->access_token_.c_str());
    cJSON_AddStringToObject(root, "ip", wifi.GetIpAddress().c_str());
    cJSON_AddStringToObject(root, "ssid", wifi.GetSsid().c_str());
    cJSON_AddNumberToObject(root, "rssi", wifi.GetRssi());
    cJSON_AddStringToObject(root, "mac", SystemInfo::GetMacAddress().c_str());
    cJSON_AddStringToObject(root, "theme_name",
        Es3c28pThemePackage::GetInstance().tokens().name.c_str());
    cJSON_AddBoolToObject(root, "theme_package",
        Es3c28pThemePackage::GetInstance().package_valid());
    cJSON_AddNumberToObject(root, "volume", board.GetAudioCodec()->output_volume());
    cJSON_AddNumberToObject(root, "brightness", board.GetBacklight()->brightness());
    cJSON_AddNumberToObject(root, "battery_level", battery);
    cJSON_AddBoolToObject(root, "charging", charging);
    cJSON_AddBoolToObject(root, "sd_mounted",
        self->music_player_ != nullptr && self->music_player_->mounted());
    cJSON_AddNumberToObject(root, "track_count",
        self->music_player_ != nullptr ? self->music_player_->track_count() : 0);
    char* json = cJSON_PrintUnformatted(root);
    SendJson(req, json);
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t Es3c28pWebServer::HandleSettings(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;
    std::string body;
    if (!ReceiveJson(req, body)) {
        SendJson(req, "{\"success\":false,\"error\":\"Invalid settings payload\"}",
            "400 Bad Request");
        return ESP_OK;
    }
    auto* root = cJSON_ParseWithLength(body.data(), body.size());
    auto* volume = root ? cJSON_GetObjectItemCaseSensitive(root, "volume") : nullptr;
    auto* brightness = root ? cJSON_GetObjectItemCaseSensitive(root, "brightness") : nullptr;
    if (!cJSON_IsNumber(volume) || !cJSON_IsNumber(brightness)) {
        if (root) cJSON_Delete(root);
        SendJson(req, "{\"success\":false,\"error\":\"Volume and brightness are required\"}",
            "400 Bad Request");
        return ESP_OK;
    }
    auto& board = Board::GetInstance();
    board.GetAudioCodec()->SetOutputVolume(std::clamp(volume->valueint, 0, 100));
    board.GetBacklight()->SetBrightness(
        static_cast<uint8_t>(std::clamp(brightness->valueint, 5, 100)), true);
    board.GetDisplay()->UpdateStatusBar(true);
    cJSON_Delete(root);
    SendJson(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t Es3c28pWebServer::HandleThemeFile(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;
    const bool ok = Es3c28pThemePackage::GetInstance().InstallFromStream(
        req->content_len, [req](char* buffer, size_t size) {
            return httpd_req_recv(req, buffer, size);
        });
    if (!ok) {
        SendJson(req, "{\"success\":false,\"error\":\"Theme file failed CRC or manifest validation\"}",
            "400 Bad Request");
        return ESP_OK;
    }
    SendJson(req, "{\"success\":true,\"restarting\":true}");
    ScheduleRestart();
    return ESP_OK;
}

esp_err_t Es3c28pWebServer::HandleThemeUrl(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;
    std::string body;
    if (!ReceiveJson(req, body)) {
        SendJson(req, "{\"success\":false,\"error\":\"Invalid URL payload\"}",
            "400 Bad Request");
        return ESP_OK;
    }
    auto* root = cJSON_ParseWithLength(body.data(), body.size());
    auto* url = root ? cJSON_GetObjectItemCaseSensitive(root, "url") : nullptr;
    if (!cJSON_IsString(url) || url->valuestring == nullptr ||
        (strncmp(url->valuestring, "http://", 7) != 0 &&
         strncmp(url->valuestring, "https://", 8) != 0)) {
        if (root) cJSON_Delete(root);
        SendJson(req, "{\"success\":false,\"error\":\"Enter a direct HTTP or HTTPS URL\"}",
            "400 Bad Request");
        return ESP_OK;
    }
    const std::string package_url = url->valuestring;
    cJSON_Delete(root);
    if (!Es3c28pThemePackage::GetInstance().Download(package_url)) {
        SendJson(req, "{\"success\":false,\"error\":\"Theme download or validation failed\"}",
            "400 Bad Request");
        return ESP_OK;
    }
    SendJson(req, "{\"success\":true,\"restarting\":true}");
    ScheduleRestart();
    return ESP_OK;
}

esp_err_t Es3c28pWebServer::HandleMedia(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;
    auto* player = self->MusicPlayer(req);
    if (player == nullptr) return ESP_OK;
    std::string directory = "/";
    std::string sort = "name";
    GetQueryValue(req, "dir", directory);
    GetQueryValue(req, "sort", sort);
    std::vector<SdMediaEntry> entries;
    std::string current_directory;
    std::string parent;
    std::vector<std::string> playlist;
    std::string error;
    if (!player->ListMedia(directory, sort, entries, current_directory, parent,
            playlist, error)) {
        SendError(req, error.c_str());
        return ESP_OK;
    }
    auto* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddBoolToObject(root, "mounted", player->mounted());
    cJSON_AddStringToObject(root, "dir", current_directory.c_str());
    cJSON_AddStringToObject(root, "parent", parent.c_str());
    cJSON_AddStringToObject(root, "sort", sort.c_str());
    cJSON_AddNumberToObject(root, "track_count", player->track_count());
    auto* items = cJSON_AddArrayToObject(root, "entries");
    for (const auto& entry : entries) {
        auto* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", entry.name.c_str());
        cJSON_AddStringToObject(item, "path", entry.path.c_str());
        cJSON_AddBoolToObject(item, "dir", entry.directory);
        cJSON_AddNumberToObject(item, "size", static_cast<double>(entry.size));
        cJSON_AddItemToArray(items, item);
    }
    auto* order = cJSON_AddArrayToObject(root, "playlist");
    for (const auto& path : playlist) {
        cJSON_AddItemToArray(order, cJSON_CreateString(path.c_str()));
    }
    char* json = cJSON_PrintUnformatted(root);
    SendJson(req, json);
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t Es3c28pWebServer::HandleMediaUpload(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;
    auto* player = self->MusicPlayer(req);
    if (player == nullptr) return ESP_OK;
    std::string directory = "/";
    std::string filename;
    if (!GetHeaderValue(req, "X-Media-Dir", directory)) {
        GetQueryValue(req, "dir", directory);
    }
    if (!GetHeaderValue(req, "X-Filename", filename)) {
        GetQueryValue(req, "name", filename);
    }
    if (filename.empty()) {
        DrainRequest(req);
        SendError(req, "Choose an MP3 file to upload");
        return ESP_OK;
    }
    UploadPerformance faster_wifi;
    TuneUploadSocket(req);
    std::string error;
    const bool ok = player->UploadMedia(directory, filename, req->content_len,
        [req](char* buffer, size_t size) {
            return httpd_req_recv(req, buffer, size);
        }, error);
    if (!ok) {
        DrainRequest(req);
        SendError(req, error.c_str());
        return ESP_OK;
    }
    SendJson(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t Es3c28pWebServer::HandleMediaDownload(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;
    auto* player = self->MusicPlayer(req);
    if (player == nullptr) return ESP_OK;
    std::string body;
    if (!ReceiveJson(req, body, 2048)) {
        SendError(req, "Invalid download payload");
        return ESP_OK;
    }
    auto* root = cJSON_ParseWithLength(body.data(), body.size());
    auto* url = root ? cJSON_GetObjectItemCaseSensitive(root, "url") : nullptr;
    auto* dir = root ? cJSON_GetObjectItemCaseSensitive(root, "dir") : nullptr;
    auto* name = root ? cJSON_GetObjectItemCaseSensitive(root, "name") : nullptr;
    if (!cJSON_IsString(url) || url->valuestring == nullptr ||
        (strncmp(url->valuestring, "http://", 7) != 0 &&
         strncmp(url->valuestring, "https://", 8) != 0)) {
        if (root) cJSON_Delete(root);
        SendError(req, "Enter a direct HTTP or HTTPS MP3 URL");
        return ESP_OK;
    }
    const std::string media_url = url->valuestring;
    const std::string directory = cJSON_IsString(dir) && dir->valuestring
        ? dir->valuestring : "/";
    std::string filename = cJSON_IsString(name) && name->valuestring != nullptr
        ? name->valuestring : "";
    cJSON_Delete(root);
    if (filename.empty()) {
        filename = FilenameFromUrl(media_url);
    }
    if (filename.empty()) {
        SendError(req, "Enter a filename ending in .mp3, or use a URL that includes one");
        return ESP_OK;
    }

    UploadPerformance faster_wifi;
    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        SendError(req, "Wi-Fi is not connected");
        return ESP_OK;
    }
    auto http = network->CreateHttp(0);
    if (http == nullptr) {
        SendError(req, "The file could not be downloaded");
        return ESP_OK;
    }
    http->SetHeader("User-Agent", SystemInfo::GetUserAgent());
    http->SetHeader("Accept", "audio/mpeg,application/octet-stream,*/*");
    if (!http->Open("GET", media_url) || http->GetStatusCode() != 200) {
        SendError(req, "The file could not be downloaded");
        return ESP_OK;
    }

    std::string error;
    const bool ok = player->UploadMedia(directory, filename, http->GetBodyLength(),
        [&http](char* buffer, size_t size) {
            return http->Read(buffer, size);
        }, error);
    if (!ok) {
        SendError(req, error.c_str());
        return ESP_OK;
    }
    SendJson(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t Es3c28pWebServer::HandleMediaRemove(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;
    auto* player = self->MusicPlayer(req);
    if (player == nullptr) return ESP_OK;
    std::string body;
    if (!ReceiveJson(req, body)) {
        SendError(req, "Invalid remove payload");
        return ESP_OK;
    }
    auto* root = cJSON_ParseWithLength(body.data(), body.size());
    auto* path = root ? cJSON_GetObjectItemCaseSensitive(root, "path") : nullptr;
    if (!cJSON_IsString(path) || path->valuestring == nullptr || path->valuestring[0] == '\0') {
        if (root) cJSON_Delete(root);
        SendError(req, "Choose a file or folder to remove");
        return ESP_OK;
    }
    std::string error;
    const bool ok = player->RemoveMedia(path->valuestring, error);
    cJSON_Delete(root);
    if (!ok) {
        SendError(req, error.c_str());
        return ESP_OK;
    }
    SendJson(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t Es3c28pWebServer::HandleMediaMkdir(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;
    auto* player = self->MusicPlayer(req);
    if (player == nullptr) return ESP_OK;
    std::string body;
    if (!ReceiveJson(req, body)) {
        SendError(req, "Invalid folder payload");
        return ESP_OK;
    }
    auto* root = cJSON_ParseWithLength(body.data(), body.size());
    auto* dir = root ? cJSON_GetObjectItemCaseSensitive(root, "dir") : nullptr;
    auto* name = root ? cJSON_GetObjectItemCaseSensitive(root, "name") : nullptr;
    if (!cJSON_IsString(name) || name->valuestring == nullptr || name->valuestring[0] == '\0') {
        if (root) cJSON_Delete(root);
        SendError(req, "Enter a folder name");
        return ESP_OK;
    }
    const std::string directory = cJSON_IsString(dir) && dir->valuestring ? dir->valuestring : "/";
    std::string error;
    const bool ok = player->CreateFolder(directory, name->valuestring, error);
    cJSON_Delete(root);
    if (!ok) {
        SendError(req, error.c_str());
        return ESP_OK;
    }
    SendJson(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t Es3c28pWebServer::HandleMediaRename(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;
    auto* player = self->MusicPlayer(req);
    if (player == nullptr) return ESP_OK;
    std::string body;
    if (!ReceiveJson(req, body)) {
        SendError(req, "Invalid rename payload");
        return ESP_OK;
    }
    auto* root = cJSON_ParseWithLength(body.data(), body.size());
    auto* path = root ? cJSON_GetObjectItemCaseSensitive(root, "path") : nullptr;
    auto* name = root ? cJSON_GetObjectItemCaseSensitive(root, "name") : nullptr;
    if (!cJSON_IsString(path) || path->valuestring == nullptr ||
        !cJSON_IsString(name) || name->valuestring == nullptr) {
        if (root) cJSON_Delete(root);
        SendError(req, "Choose an item and a new name");
        return ESP_OK;
    }
    std::string error;
    const bool ok = player->RenameMedia(path->valuestring, name->valuestring, error);
    cJSON_Delete(root);
    if (!ok) {
        SendError(req, error.c_str());
        return ESP_OK;
    }
    SendJson(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t Es3c28pWebServer::HandleMediaMove(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;
    auto* player = self->MusicPlayer(req);
    if (player == nullptr) return ESP_OK;
    std::string body;
    if (!ReceiveJson(req, body)) {
        SendError(req, "Invalid move payload");
        return ESP_OK;
    }
    auto* root = cJSON_ParseWithLength(body.data(), body.size());
    auto* path = root ? cJSON_GetObjectItemCaseSensitive(root, "path") : nullptr;
    auto* dir = root ? cJSON_GetObjectItemCaseSensitive(root, "dir") : nullptr;
    if (!cJSON_IsString(path) || path->valuestring == nullptr ||
        !cJSON_IsString(dir) || dir->valuestring == nullptr) {
        if (root) cJSON_Delete(root);
        SendError(req, "Choose an item and a destination folder");
        return ESP_OK;
    }
    std::string error;
    const bool ok = player->MoveMedia(path->valuestring, dir->valuestring, error);
    cJSON_Delete(root);
    if (!ok) {
        SendError(req, error.c_str());
        return ESP_OK;
    }
    SendJson(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t Es3c28pWebServer::HandleMediaOrder(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;
    auto* player = self->MusicPlayer(req);
    if (player == nullptr) return ESP_OK;
    std::string body;
    if (!ReceiveJson(req, body, 65536)) {
        SendError(req, "Invalid playlist payload");
        return ESP_OK;
    }
    auto* root = cJSON_ParseWithLength(body.data(), body.size());
    auto* paths = root ? cJSON_GetObjectItemCaseSensitive(root, "paths") : nullptr;
    if (!cJSON_IsArray(paths)) {
        if (root) cJSON_Delete(root);
        SendError(req, "Playlist order is required");
        return ESP_OK;
    }
    std::vector<std::string> order;
    const int count = cJSON_GetArraySize(paths);
    order.reserve(count);
    for (int i = 0; i < count; ++i) {
        auto* item = cJSON_GetArrayItem(paths, i);
        if (!cJSON_IsString(item) || item->valuestring == nullptr) {
            cJSON_Delete(root);
            SendError(req, "The playlist contains an invalid track");
            return ESP_OK;
        }
        order.emplace_back(item->valuestring);
    }
    cJSON_Delete(root);
    std::string error;
    if (!player->SetPlaylistOrder(order, error)) {
        SendError(req, error.c_str());
        return ESP_OK;
    }
    SendJson(req, "{\"success\":true}");
    return ESP_OK;
}

namespace {
std::string PlaybackJson(SdMusicPlayer* player) {
    SdPlaybackStatus status;
    player->GetPlaybackStatus(status);
    auto* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddBoolToObject(root, "mounted", status.mounted);
    cJSON_AddBoolToObject(root, "playing", status.playing);
    cJSON_AddBoolToObject(root, "controllable", status.controllable);
    cJSON_AddStringToObject(root, "name", status.name.c_str());
    cJSON_AddStringToObject(root, "path", status.path.c_str());
    cJSON_AddNumberToObject(root, "track_index", static_cast<double>(status.track_index));
    cJSON_AddNumberToObject(root, "track_count", static_cast<double>(status.track_count));
    cJSON_AddNumberToObject(root, "elapsed", status.elapsed_seconds);
    cJSON_AddNumberToObject(root, "duration", status.duration_seconds);
    char* json = cJSON_PrintUnformatted(root);
    std::string body = json ? json : "{\"success\":false}";
    cJSON_free(json);
    cJSON_Delete(root);
    return body;
}
}  // namespace

esp_err_t Es3c28pWebServer::HandleMediaPlayer(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;
    auto* player = self->MusicPlayer(req);
    if (player == nullptr) return ESP_OK;
    if (req->method == HTTP_POST) {
        std::string body;
        if (!ReceiveJson(req, body)) {
            SendError(req, "Invalid player command");
            return ESP_OK;
        }
        auto* root = cJSON_ParseWithLength(body.data(), body.size());
        auto* action = root ? cJSON_GetObjectItemCaseSensitive(root, "action") : nullptr;
        auto* path = root ? cJSON_GetObjectItemCaseSensitive(root, "path") : nullptr;
        if (!cJSON_IsString(action) || action->valuestring == nullptr ||
            action->valuestring[0] == '\0') {
            if (root) cJSON_Delete(root);
            SendError(req, "Choose play, pause, toggle, next, or previous");
            return ESP_OK;
        }
        const std::string command = action->valuestring;
        const std::string track = cJSON_IsString(path) && path->valuestring
            ? path->valuestring : "";
        cJSON_Delete(root);
        std::string error;
        if (!player->ControlPlayback(command, track, error)) {
            SendError(req, error.c_str());
            return ESP_OK;
        }
    }
    SendJson(req, PlaybackJson(player).c_str());
    return ESP_OK;
}

esp_err_t Es3c28pWebServer::HandleWifiReset(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;
    Settings settings("wifi", true);
    settings.SetInt("force_ap", 1);
    SendJson(req, "{\"success\":true,\"restarting\":true}");
    ScheduleRestart();
    return ESP_OK;
}

esp_err_t Es3c28pWebServer::HandleReboot(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;
    SendJson(req, "{\"success\":true,\"restarting\":true}");
    ScheduleRestart();
    return ESP_OK;
}

#if CONFIG_ES3C28P_SCREENSHOT_API
esp_err_t Es3c28pWebServer::HandleScreenshot(httpd_req_t* req) {
    auto* self = static_cast<Es3c28pWebServer*>(req->user_ctx);
    if (!self->Authorize(req)) return ESP_OK;

    auto* display = dynamic_cast<LvglDisplay*>(Board::GetInstance().GetDisplay());
    if (display == nullptr) {
        SendError(req, "Display snapshot is unavailable", "503 Service Unavailable");
        return ESP_OK;
    }

    int quality = 80;
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char value[8];
        if (httpd_query_key_value(query, "quality", value, sizeof(value)) == ESP_OK) {
            quality = std::clamp(atoi(value), 1, 100);
        }
        char page[12];
        if (httpd_query_key_value(query, "page", page, sizeof(page)) == ESP_OK &&
            strcmp(page, "music") == 0) {
            if (auto* es_display = dynamic_cast<Es3c28pDisplay*>(display)) {
                es_display->ShowMusicPage(true);
            }
        }
    }

    std::string jpeg;
    if (!display->SnapshotToJpeg(jpeg, quality)) {
        SendError(req, "Failed to snapshot screen", "500 Internal Server Error");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, jpeg.data(), jpeg.size());
    return ESP_OK;
}
#endif

void Es3c28pWebServer::ScheduleRestart() {
    xTaskCreate([](void*) {
        vTaskDelay(pdMS_TO_TICKS(700));
        esp_restart();
    }, "web_restart", 2048, nullptr, 4, nullptr);
}
