#!/usr/bin/env node
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {spawn, spawnSync} from 'node:child_process';
import {setTimeout as delay} from 'node:timers/promises';
import {pathToFileURL} from 'node:url';
import {stateDigest} from './parity_state_canonical.mjs';
const args=process.argv.slice(2); const val=k=>{const i=args.indexOf(k); return i<0?undefined:args[i+1]};
const source=val('--source'), out=path.resolve(val('--output')), chrome=val('--chrome');
const importerCapture=val('--importer-capture');
const statePath=val('--state');
const analyzerFixturePath=val('--analyzer-fixture');
const strict=args.includes('--strict');
const requireCanvasInk=args.includes('--require-canvas-ink');
const requireImporterCanvasInk=args.includes('--require-importer-canvas-ink');
const plantNoInk=args.includes('--plant-no-ink');
if(!source||!out||!chrome) throw new Error('usage --source FILE --output DIR --chrome PATH [--state JSON] [--strict] [--require-canvas-ink] [--importer-capture DIR] [--require-importer-canvas-ink] [--plant-no-ink]');
if(analyzerFixturePath && !statePath) throw new Error('--analyzer-fixture requires --state');
if(requireImporterCanvasInk && !importerCapture) throw new Error('--require-importer-canvas-ink requires --importer-capture DIR');
fs.mkdirSync(out,{recursive:true});
const bytes=fs.readFileSync(source); const sha=x=>crypto.createHash('sha256').update(x).digest('hex');
let parityState=null;
let analyzerFixture={schema:'spectr-parity-analyzer-v1',version:1,epoch:1,sequences:[1,2],fftSize:1024,sampleRate:48000,floorDb:-96,ceilingDb:0,sourceChannels:2,visibleSamples:321,overviewSamples:121,minHz:20,maxHz:20000,trace:{baseDb:-92,peakDb:78,centre:0.25,phaseStep:0.1,width:0.06}};
if(statePath){
 parityState=JSON.parse(fs.readFileSync(statePath,'utf8'));
 if(parityState.schema!=='spectr-parity-state-v1'||parityState.version!==1) throw new Error('unsupported parity state schema/version');
 const declared=parityState.stateSha256;
 delete parityState.stateSha256;
 const computed=stateDigest(parityState);
 if(declared!==computed) throw new Error(`parity state digest mismatch: ${declared} != ${computed}`);
 const sourceStateSha=parityState.source?.sha256;
 if(sourceStateSha && sourceStateSha!==sha(bytes)) throw new Error(`parity state source mismatch: ${sourceStateSha} != ${sha(bytes)}`);
 parityState.stateSha256=declared;
 if(parityState.analyzer?.fixtureSha256) {
  if(!analyzerFixturePath) throw new Error('--state with analyzer fixture requires --analyzer-fixture');
  analyzerFixture=JSON.parse(fs.readFileSync(analyzerFixturePath,'utf8'));
  if(analyzerFixture.schema!=='spectr-parity-analyzer-v1'||analyzerFixture.version!==1)
   throw new Error('unsupported analyzer fixture schema/version');
  const fixtureSha=sha(fs.readFileSync(analyzerFixturePath));
  if(parityState.analyzer.fixtureSha256!==fixtureSha)
   throw new Error(`analyzer fixture mismatch: ${parityState.analyzer.fixtureSha256} != ${fixtureSha}`);
 }
}
const viewport=parityState?.viewport ?? {width:1320,height:860,deviceScaleFactor:1};
const viewportWidth=viewport.width;
const viewportHeight=viewport.height;
const viewportScale=viewport.deviceScaleFactor ?? 1;
// A bridge script must not precede a source document's doctype.  Chromium
// switches to quirks mode when anything (including a script) appears before
// the doctype token, which can change layout metrics in the comparison page.
// Keep the source bytes intact and insert the bridge immediately after a
// leading HTML doctype.  Documents without a leading doctype remain
// BackCompat, matching their original browser behavior.
const leadingDoctype=/^(?:\uFEFF)?[\t\n\f\r ]*<!doctype[\t\n\f\r ]+[^>]*>/i;
const injectBridge=(sourceBytes,bridgeSource)=>{
 const sourceText=sourceBytes.toString('utf8');
 const match=sourceText.match(leadingDoctype);
 const bridgeBytes=Buffer.from(bridgeSource);
 if(!match)return Buffer.concat([bridgeBytes,sourceBytes]);
 const doctypeEnd=Buffer.byteLength(match[0],'utf8');
 return Buffer.concat([sourceBytes.subarray(0,doctypeEnd),bridgeBytes,sourceBytes.subarray(doctypeEnd)]);
};
const bridge=`<script>window.__spectrPostsFixture=${JSON.stringify(analyzerFixture)};window.__spectrBrowserPosts=[];window.__spectrBrowserListeners=Object.create(null);window.pulp={on(type,cb){(window.__spectrBrowserListeners[type]??=new Set()).add(cb);return()=>window.__spectrBrowserListeners[type].delete(cb)},postMessage(type,payload){window.__spectrBrowserPosts.push({type,payload});for(const cb of window.__spectrBrowserListeners[type]||[])try{cb({type,payload})}catch(e){};if(type==='editor_ready'){queueMicrotask(()=>{const f=window.__spectrPostsFixture;const n=32;const trace=(count,phase=0)=>Array.from({length:count},(_,i)=>{const x=i/Math.max(1,count-1);return f.trace.baseDb+f.trace.peakDb*Math.exp(-Math.pow((x-(f.trace.centre+phase*f.trace.phaseStep))/f.trace.width,2))});const state={n_visible:n,gain_db:new Array(n).fill(0),muted:new Array(n).fill(false),min_hz:f.minHz,max_hz:f.maxHz,motion_mode:0,analyzer_mode:0,edit_mode:0,visualization_mode:2,revision:1,snapshots:{A:{populated:false},B:{populated:false}},patterns_json:JSON.stringify({format:'spectr.patterns',version:1,default_id:'factory:flat',patterns:[]})};for(const cb of window.__spectrBrowserListeners.processing_state_hydrate||[])try{cb({type:'processing_state_hydrate',payload:state})}catch(e){};const frame=(sequence,phase)=>({schema_version:1,epoch:f.epoch,sequence_number:sequence,dropped_frames:0,source_channels:f.sourceChannels,fft_size:f.fftSize,sample_rate:f.sampleRate,floor_db:f.floorDb,ceiling_db:f.ceilingDb,visible:{min_hz:f.minHz,max_hz:f.maxHz,magnitude_db:trace(f.visibleSamples,phase)},overview:{min_hz:f.minHz,max_hz:f.maxHz,magnitude_db:trace(f.overviewSamples,phase)}});let analyzerAccepted=false;for(const sequence of f.sequences)for(const cb of window.__spectrBrowserListeners.analyzer_frame||[])try{cb({type:'analyzer_frame',payload:frame(sequence,sequence-1)});const snap=window.SpectrAnalyzer?.debugSnapshot?.();if(snap?.epoch===f.epoch&&snap?.sequence_number===sequence)analyzerAccepted=sequence===f.sequences[1]}catch(e){};if(analyzerAccepted)window.__spectrParityReady__={contract:'spectr-parity-v1',analyzerSequence:f.sequences[1],analyzerAccepted:true}})}return Promise.resolve({ok:true,payload:{ok:true}})}};window.confirm=()=>true;</script>`;
const html=injectBridge(bytes,bridge);
const temp=path.join(out,'source-with-bridge.html'); fs.writeFileSync(temp,html);
const profile=fs.mkdtempSync(path.join(out,'chrome-profile-'));
const proc=spawn(chrome,['--headless=new','--disable-gpu','--disable-background-networking','--disable-component-update','--disable-domain-reliability','--disable-sync','--no-first-run','--no-default-browser-check','--allow-file-access-from-files','--run-all-compositor-stages-before-draw',`--window-size=${viewportWidth},${viewportHeight}`,`--force-device-scale-factor=${viewportScale}`,'--remote-debugging-address=127.0.0.1','--remote-debugging-port=0',`--user-data-dir=${profile}`,'about:blank'],{stdio:'ignore'});
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
 await cmd('Emulation.setDeviceMetricsOverride',{width:viewportWidth,height:viewportHeight,deviceScaleFactor:viewportScale,mobile:false,screenWidth:viewportWidth,screenHeight:viewportHeight});
 if(plantNoInk) await cmd('Page.addScriptToEvaluateOnNewDocument',{source:`(()=>{const names=['fill','stroke','fillRect','strokeRect','clearRect','fillText','strokeText','drawImage','putImageData'];for(const name of names){const proto=globalThis.CanvasRenderingContext2D?.prototype;if(proto&&typeof proto[name]==='function')proto[name]=()=>{};}})()`});
 await cmd('Page.navigate',{url:pathToFileURL(temp).href});
 const requireParityReady=Boolean(statePath && parityState?.analyzer?.fixtureSha256);
 const stop=Date.now()+30000; let ready;
 while(Date.now()<stop){ready=await evalv(`(()=>({ready:document.readyState,root:document.getElementById('root')?.children.length||0,canvas:document.querySelectorAll('canvas').length,body:document.body?.children.length||0,title:document.title,sourceEditorReady:(window.__spectrBrowserPosts||[]).some(p=>p.type==='editor_ready'),parityReady:window.__spectrParityReady__||null}))()`); if(ready.ready==='complete'&&(ready.root>0||ready.canvas>0||ready.body>0)&&(!requireParityReady||(ready.sourceEditorReady&&ready.parityReady?.contract==='spectr-parity-v1'&&ready.parityReady.analyzerSequence===2&&ready.parityReady.analyzerAccepted===true)))break; await delay(100)}
 await delay(500);
 const info=await evalv(`(()=>{const cs=[...document.querySelectorAll('canvas')].map((c,i)=>({i,width:c.width,height:c.height,css:[c.getBoundingClientRect().width,c.getBoundingClientRect().height]}));const root=document.getElementById('root');const txt=(document.body?.innerText||'').slice(0,2000);const attrs=[...document.querySelectorAll('[data-spectr-menu-root],[data-spectr-menu-trigger],[data-spectr-bank-ready]')].map(x=>({tag:x.tagName,id:x.id,attrs:[...x.attributes].reduce((o,a)=>(o[a.name]=a.value,o),{})}));return {readyState:document.readyState,title:document.title,compatMode:document.compatMode,rootChildren:root?.children.length||0,bodyChildren:document.body?.children.length||0,canvas:cs,attrs,bodyText:txt,posts:(window.__spectrBrowserPosts||[]).slice(0,10),htmlBytes:document.documentElement.outerHTML.length}})()`);
 const canvasInk=await evalv(`(()=>{let pixels=0,ink=0,max=0;for(const canvas of document.querySelectorAll('canvas')){const ctx=canvas.getContext('2d');if(!ctx)continue;let data;try{data=ctx.getImageData(0,0,canvas.width,canvas.height).data}catch{continue}pixels+=data.length/4;for(let i=0;i<data.length;i+=4){const value=data[i]+data[i+1]+data[i+2];if(value>max)max=value;if(data[i+3]>0&&value>18)ink++}}return {pixels,inkPixels:ink,inkFraction:pixels?ink/pixels:0,max}})()`);
 if(strict && (info.rootChildren < 1 || info.canvas.length < 1 || (requireParityReady && (!ready?.sourceEditorReady || ready?.parityReady?.contract !== 'spectr-parity-v1' || ready?.parityReady?.analyzerSequence !== 2 || ready?.parityReady?.analyzerAccepted !== true)) || consoleErrors.length || networkFailures.length)) throw Error(`strict render check failed: root=${info.rootChildren} canvas=${info.canvas.length} sourceEditorReady=${Boolean(ready?.sourceEditorReady)} console=${consoleErrors.length} network=${networkFailures.length}`);
 if(requireCanvasInk && canvasInk.inkPixels < 1) throw Error(`canvas ink check failed: canvases=${info.canvas.length} pixels=${canvasInk.pixels} inkPixels=${canvasInk.inkPixels}`);
 let importer;
 if(importerCapture){
  const importerProbe=spawnSync('python3',['-c',String.raw`import json,sys
from pathlib import Path
from PIL import Image
capture=json.loads(Path(sys.argv[1]).read_text())
expected=sys.argv[3]
actual=capture.get('provenance',{}).get('source',{}).get('sha256')
if actual != expected: raise SystemExit(f'importer source SHA mismatch: {actual} != {expected}')
image=Image.open(sys.argv[2]).convert('RGBA')
w,h=image.size
x0,x1=int(w*0.03),int(w*0.97)
y0,y1=int(h*0.10),int(h*0.85)
pixels=ink=peak=0
for r,g,b,a in image.crop((x0,y0,x1,y1)).getdata():
 pixels += 1
 value = r + g + b
 peak = max(peak, value)
 if a > 0 and value > 60: ink += 1
print(json.dumps({'sourceSha256':actual,'pngSize':{'width':w,'height':h},'region':{'x':x0,'y':y0,'width':x1-x0,'height':y1-y0},'canvasInk':{'pixels':pixels,'inkPixels':ink,'inkFraction':ink/pixels if pixels else 0,'max':peak,'threshold':60}}))
`,path.join(importerCapture,'capture.json'),path.join(importerCapture,'browser.png'),sha(bytes)],{encoding:'utf8'});
  if(importerProbe.status!==0) throw Error(importerProbe.stderr.trim()||'importer capture probe failed');
  importer=JSON.parse(importerProbe.stdout);
  if(requireImporterCanvasInk && importer.canvasInk.inkPixels < 1) throw Error(`importer canvas ink check failed: source=${importer.sourceSha256} pixels=${importer.canvasInk.pixels} inkPixels=${importer.canvasInk.inkPixels}`);
 }
 const shot=await cmd('Page.captureScreenshot',{format:'png',fromSurface:true}); const png=Buffer.from(shot.data,'base64'); const pngPath=path.join(out,'before.png'); fs.writeFileSync(pngPath,png);
 let menuAction=null; if(info.attrs.some(a=>a.attrs['data-spectr-menu-trigger'])){menuAction=await evalv(`(()=>{const n=document.querySelector('[data-spectr-menu-trigger]');if(!n)return null; n.click(); return {expanded:n.getAttribute('aria-expanded')};})()`);await delay(400);const open=await cmd('Page.captureScreenshot',{format:'png',fromSurface:true});const openBytes=Buffer.from(open.data,'base64');fs.writeFileSync(path.join(out,'menu-open.png'),openBytes);menuAction.openPngSha256=sha(openBytes);}
 const receipt={schema:'spectr-html-cdp-comparison-v1',source:path.resolve(source),sourceSha256:sha(bytes),sourceBytes:bytes.length,stateSha256:parityState?.stateSha256??null,parityState,chrome:spawnSync(chrome,['--version'],{encoding:'utf8'}).stdout.trim(),fixedViewport:{width:viewportWidth,height:viewportHeight,deviceScaleFactor:viewportScale},checks:{strict,requireCanvasInk,requireImporterCanvasInk,plantNoInk},positive:{ready,info,canvasInk,importer,consoleErrors,networkFailures,before:{path:pngPath,sha256:sha(png),bytes:png.length},menuAction}};
 fs.writeFileSync(path.join(out,'receipt.json'),JSON.stringify(receipt,null,2)+'\n'); console.log(JSON.stringify(receipt,null,2));
 await cmd('Browser.close').catch(()=>{});
}catch(e){console.error(e.stack||e);process.exitCode=1}
finally{try{socket?.close()}catch{};proc.kill('SIGTERM');try{fs.rmSync(profile,{recursive:true,force:true})}catch{}}
