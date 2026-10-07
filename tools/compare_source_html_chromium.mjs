#!/usr/bin/env node
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {spawn, spawnSync} from 'node:child_process';
import {setTimeout as delay} from 'node:timers/promises';
import {pathToFileURL} from 'node:url';
const args=process.argv.slice(2); const val=k=>{const i=args.indexOf(k); return i<0?undefined:args[i+1]};
const source=val('--source'), out=path.resolve(val('--output')), chrome=val('--chrome');
const strict=args.includes('--strict');
const requireCanvasInk=args.includes('--require-canvas-ink');
const plantNoInk=args.includes('--plant-no-ink');
if(!source||!out||!chrome) throw new Error('usage --source FILE --output DIR --chrome PATH [--strict] [--require-canvas-ink] [--plant-no-ink]');
fs.mkdirSync(out,{recursive:true});
const bytes=fs.readFileSync(source); const sha=x=>crypto.createHash('sha256').update(x).digest('hex');
const bridge=`<script>window.__spectrBrowserPosts=[];window.__spectrBrowserListeners=Object.create(null);window.pulp={on(type,cb){(window.__spectrBrowserListeners[type]??=new Set()).add(cb);return()=>window.__spectrBrowserListeners[type].delete(cb)},postMessage(type,payload){window.__spectrBrowserPosts.push({type,payload});for(const cb of window.__spectrBrowserListeners[type]||[])try{cb({type,payload})}catch(e){};if(type==='editor_ready'){queueMicrotask(()=>{const n=32;const trace=count=>Array.from({length:count},(_,i)=>{const x=i/Math.max(1,count-1);const left=Math.exp(-((x-.22)**2)/.008)*28;const middle=Math.exp(-((x-.52)**2)/.018)*38;const right=Math.exp(-((x-.82)**2)/.012)*23;return -96+left+middle+right});const state={n_visible:n,gain_db:new Array(n).fill(0),muted:new Array(n).fill(false),min_hz:20,max_hz:20000,motion_mode:0,analyzer_mode:0,edit_mode:0,visualization_mode:2,revision:1,snapshots:{A:{populated:false},B:{populated:false}},patterns_json:JSON.stringify({format:'spectr.patterns',version:1,default_id:'factory:flat',patterns:[]})};for(const cb of window.__spectrBrowserListeners.processing_state_hydrate||[])try{cb({type:'processing_state_hydrate',payload:state})}catch(e){};const analyzer={schema_version:1,epoch:0,sequence_number:0,dropped_frames:0,source_channels:2,fft_size:512,sample_rate:48000,floor_db:-120,ceiling_db:24,visible:{min_hz:20,max_hz:20000,magnitude_db:trace(321)},overview:{min_hz:20,max_hz:20000,magnitude_db:trace(121)}};for(const cb of window.__spectrBrowserListeners.analyzer_frame||[])try{cb({type:'analyzer_frame',payload:analyzer})}catch(e){}})}return Promise.resolve({ok:true,payload:{ok:true}})}};window.confirm=()=>true;</script>`;
const html=Buffer.concat([Buffer.from(bridge),bytes]);
const temp=path.join(out,'source-with-bridge.html'); fs.writeFileSync(temp,html);
const profile=fs.mkdtempSync(path.join(out,'chrome-profile-'));
const proc=spawn(chrome,['--headless=new','--disable-gpu','--disable-background-networking','--disable-component-update','--disable-domain-reliability','--disable-sync','--no-first-run','--no-default-browser-check','--allow-file-access-from-files','--run-all-compositor-stages-before-draw','--window-size=1320,860','--force-device-scale-factor=1','--remote-debugging-address=127.0.0.1','--remote-debugging-port=0',`--user-data-dir=${profile}`,'about:blank'],{stdio:'ignore'});
let socket;
try{
 let page; const deadline=Date.now()+15000;
 while(!page&&Date.now()<deadline){try{const active=fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8').split('\n');const port=Number(active[0]);if(port){const pages=await(await fetch(`http://127.0.0.1:${port}/json`)).json();page=pages.find(x=>x.type==='page')}}catch{} if(!page)await delay(50)}
 if(!page?.webSocketDebuggerUrl)throw Error('DevTools endpoint did not start');
 socket=new WebSocket(page.webSocketDebuggerUrl); await new Promise((res,rej)=>{const t=setTimeout(()=>rej(Error('ws timeout')),10000);socket.addEventListener('open',()=>{clearTimeout(t);res()},{once:true});socket.addEventListener('error',e=>{clearTimeout(t);rej(e)},{once:true})});
 let id=1;const pending=new Map(),consoleErrors=[],networkFailures=[];
 socket.addEventListener('message',ev=>{const m=JSON.parse(ev.data); if(m.method==='Runtime.consoleAPICalled'&&['error','assert'].includes(m.params.type))consoleErrors.push(m.params.args.map(a=>a.value??a.description??'').join(' ')); if(m.method==='Runtime.exceptionThrown')consoleErrors.push(m.params.exceptionDetails?.text||'uncaught exception');if(m.method==='Network.loadingFailed')networkFailures.push(`${m.params.errorText}: ${m.params.url}`);if(m.id&&pending.has(m.id)){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(Error(JSON.stringify(m.error))):p.resolve(m.result)}});
 const cmd=(method,params={})=>new Promise((res,rej)=>{const i=id++,t=setTimeout(()=>{pending.delete(i);rej(Error(`timeout ${method}`))},15000);pending.set(i,{resolve:x=>{clearTimeout(t);res(x)},reject:x=>{clearTimeout(t);rej(x)}});socket.send(JSON.stringify({id:i,method,params}))});
 const evalv=async expression=>{const r=await cmd('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});if(r.exceptionDetails)throw Error(r.exceptionDetails.exception?.description||r.exceptionDetails.text);return r.result.value};
 await cmd('Runtime.enable'); await cmd('Network.enable'); await cmd('Page.enable');
 if(plantNoInk) await cmd('Page.addScriptToEvaluateOnNewDocument',{source:`(()=>{const names=['fill','stroke','fillRect','strokeRect','clearRect','fillText','strokeText','drawImage','putImageData'];for(const name of names){const proto=globalThis.CanvasRenderingContext2D?.prototype;if(proto&&typeof proto[name]==='function')proto[name]=()=>{};}})()`});
 await cmd('Page.navigate',{url:pathToFileURL(temp).href});
 const stop=Date.now()+30000; let ready;
 while(Date.now()<stop){ready=await evalv(`(()=>({ready:document.readyState,root:document.getElementById('root')?.children.length||0,canvas:document.querySelectorAll('canvas').length,body:document.body?.children.length||0,title:document.title}))()`); if(ready.ready==='complete'&&(ready.root>0||ready.canvas>0||ready.body>0))break; await delay(100)}
 await delay(500);
 const info=await evalv(`(()=>{const cs=[...document.querySelectorAll('canvas')].map((c,i)=>({i,width:c.width,height:c.height,css:[c.getBoundingClientRect().width,c.getBoundingClientRect().height]}));const root=document.getElementById('root');const txt=(document.body?.innerText||'').slice(0,2000);const attrs=[...document.querySelectorAll('[data-spectr-menu-root],[data-spectr-menu-trigger],[data-spectr-bank-ready]')].map(x=>({tag:x.tagName,id:x.id,attrs:[...x.attributes].reduce((o,a)=>(o[a.name]=a.value,o),{})}));return {readyState:document.readyState,title:document.title,rootChildren:root?.children.length||0,bodyChildren:document.body?.children.length||0,canvas:cs,attrs,bodyText:txt,posts:(window.__spectrBrowserPosts||[]).slice(0,10),htmlBytes:document.documentElement.outerHTML.length}})()`);
 const canvasInk=await evalv(`(()=>{let pixels=0,ink=0,max=0;for(const canvas of document.querySelectorAll('canvas')){const ctx=canvas.getContext('2d');if(!ctx)continue;let data;try{data=ctx.getImageData(0,0,canvas.width,canvas.height).data}catch{continue}pixels+=data.length/4;for(let i=0;i<data.length;i+=4){const value=data[i]+data[i+1]+data[i+2];if(value>max)max=value;if(data[i+3]>0&&value>18)ink++}}return {pixels,inkPixels:ink,inkFraction:pixels?ink/pixels:0,max}})()`);
 if(strict && (info.rootChildren < 1 || info.canvas.length < 1 || consoleErrors.length || networkFailures.length)) throw Error(`strict render check failed: root=${info.rootChildren} canvas=${info.canvas.length} console=${consoleErrors.length} network=${networkFailures.length}`);
 if(requireCanvasInk && canvasInk.inkPixels < 1) throw Error(`canvas ink check failed: canvases=${info.canvas.length} pixels=${canvasInk.pixels} inkPixels=${canvasInk.inkPixels}`);
 const shot=await cmd('Page.captureScreenshot',{format:'png',fromSurface:true}); const png=Buffer.from(shot.data,'base64'); const pngPath=path.join(out,'before.png'); fs.writeFileSync(pngPath,png);
 let menuAction=null; if(info.attrs.some(a=>a.attrs['data-spectr-menu-trigger'])){menuAction=await evalv(`(()=>{const n=document.querySelector('[data-spectr-menu-trigger]');if(!n)return null; n.click(); return {expanded:n.getAttribute('aria-expanded')};})()`);await delay(400);const open=await cmd('Page.captureScreenshot',{format:'png',fromSurface:true});const openBytes=Buffer.from(open.data,'base64');fs.writeFileSync(path.join(out,'menu-open.png'),openBytes);menuAction.openPngSha256=sha(openBytes);}
 const receipt={schema:'spectr-html-cdp-comparison-v1',source:path.resolve(source),sourceSha256:sha(bytes),sourceBytes:bytes.length,chrome:spawnSync(chrome,['--version'],{encoding:'utf8'}).stdout.trim(),fixedViewport:{width:1320,height:860,deviceScaleFactor:1},checks:{strict,requireCanvasInk,plantNoInk},positive:{ready,info,canvasInk,consoleErrors,networkFailures,before:{path:pngPath,sha256:sha(png),bytes:png.length},menuAction}};
 fs.writeFileSync(path.join(out,'receipt.json'),JSON.stringify(receipt,null,2)+'\n'); console.log(JSON.stringify(receipt,null,2));
 await cmd('Browser.close').catch(()=>{});
}catch(e){console.error(e.stack||e);process.exitCode=1}
finally{try{socket?.close()}catch{};proc.kill('SIGTERM');try{fs.rmSync(profile,{recursive:true,force:true})}catch{}}
