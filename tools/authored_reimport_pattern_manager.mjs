#!/usr/bin/env node
/**
 * Bounded authored re-import experiment for the PatternManager subtree.
 *
 * The selected closure is PatternManager -> ListHeader, MBtn,
 * PatternDeleteDialog, PatternDetail, and PatternRow (with MiniPreview as a
 * leaf dependency).  The runner compiles explicit TSX modules, replaces only
 * those declarations in a copy of the materialized runtime, and proves that
 * the original and imported trees have the same deterministic structure and
 * state transitions.  It never edits editor.html, the checked-in runtime, or
 * any patch script.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import vm from 'node:vm';
import { spawn, spawnSync } from 'node:child_process';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '..');
const COMPONENTS = ['ListHeader', 'MBtn', 'MiniPreview', 'PatternDeleteDialog', 'PatternDetail', 'PatternManager', 'PatternRow'];
const LOAD_ORDER = ['ListHeader', 'MBtn', 'MiniPreview', 'PatternDeleteDialog', 'PatternDetail', 'PatternRow', 'PatternManager'];
const EXPECTED_DEPS = {
  ListHeader: [], MBtn: [], MiniPreview: [],
  PatternDeleteDialog: ['MBtn'], PatternDetail: ['MBtn', 'MiniPreview'],
  PatternManager: ['ListHeader', 'MBtn', 'PatternDeleteDialog', 'PatternDetail', 'PatternRow'],
  PatternRow: ['MiniPreview'],
};
const SCHEMA = 'spectr-authored-reimport-pattern-manager-v1';
let ts;
try {
  ts = createRequire(import.meta.url)(path.join(here, 'wp1-parser', 'node_modules', 'typescript'));
} catch (error) {
  console.error(`PatternManager re-import runner requires the pinned WP-1 toolchain; run npm ci --ignore-scripts --prefix ${path.join(here, 'wp1-parser')} (${error.message})`);
  process.exit(2);
}

function fail(message) { throw new Error(`authored PatternManager re-import failed: ${message}`); }
function assert(condition, message) { if (!condition) fail(message); }
function sha256(value) { return crypto.createHash('sha256').update(value).digest('hex'); }
function read(file) { try { return fs.readFileSync(file); } catch (error) { fail(`cannot read ${file}: ${error.message}`); } }
function write(file, value) { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, value); }
function parseArgs(argv) {
  const out = {};
  for (let i = 0; i < argv.length; ++i) {
    const arg = argv[i];
    if (['artifact', 'manifest', 'modules', 'out', 'chrome'].includes(arg.slice(2)) && arg.startsWith('--')) out[arg.slice(2)] = argv[++i];
    else if (arg === '--help') out.help = true;
    else fail(`unknown argument ${arg}`);
  }
  return out;
}
function usage() { console.log('usage: node tools/authored_reimport_pattern_manager.mjs --artifact FILE --manifest FILE --modules DIR --out DIR --chrome PATH'); }

function htmlScripts(html) {
  const scripts = [];
  const re = /<script\b([^>]*)>([\s\S]*?)<\/script\s*>/gi;
  let match;
  while ((match = re.exec(html))) {
    const type = (match[1].match(/\btype\s*=\s*["']([^"']+)["']/i)?.[1] || 'text/javascript').toLowerCase();
    if (type === 'application/json' || type === 'importmap') continue;
    if (!/^(?:text|application)\/(?:java|ecma)script$/.test(type) && type !== 'module') continue;
    const bodyStart = match.index + match[0].indexOf(match[2]);
    scripts.push({ source: match[2], bodyStart, bodyEnd: bodyStart + match[2].length });
  }
  return scripts;
}

function reactMock() {
  return {
    createElement(type, props, ...children) {
      const next = { ...(props || {}) };
      const flat = [];
      for (const child of children.flat(Infinity)) if (child !== null && child !== undefined && child !== false) flat.push(child);
      if (flat.length) next.children = flat;
      return { type, props: next };
    },
    Fragment: Symbol('Fragment'),
  };
}

function fixturePatterns() {
  return [
    { id: 'factory:flat', name: 'Flat', source: 'factory', gains: [0.1, -0.2, 0.3, -Infinity] },
    { id: 'factory:tilt', name: 'Tilt', source: 'factory', gains: [-0.2, 0.1, 0.4, 0.2] },
  ];
}
function fixtureUserPatterns() {
  return [{ id: 'user:one', name: 'User One', source: 'user', gains: [0.25, -0.5, -Infinity, 0.75], createdAt: '2026-01-02T00:00:00Z', updatedAt: '2026-01-03T00:00:00Z' }];
}

function makeContext({ stateful = false } = {}) {
  const listeners = new Map();
  const document = {
    activeElement: null,
    querySelector: () => null,
    querySelectorAll: () => [],
    addEventListener: (name, callback) => { listeners.set(name, callback); },
    removeEventListener: name => { listeners.delete(name); },
  };
  const factory = fixturePatterns();
  const window = {
    Spectr: {
      FACTORY_PATTERNS: factory,
      resolveGains: (pattern, n) => (pattern?.gains || []).slice(0, n),
      factoryGains: id => factory.find(item => item.id === id)?.gains || [],
      fromCanonical: gains => [...gains], toCanonical: gains => [...gains],
      makeUserPattern: (name, gains) => ({ id: `user:${name}`, name, source: 'user', gains }),
      exportEnvelope: patterns => ({ format: 'spectr.patterns', version: 1, patterns }),
      parseEnvelope: value => ({ patterns: Array.isArray(value?.patterns) ? value.patterns : [], errors: [] }),
    },
  };
  const context = {
    React: reactMock(), window, document, globalThis: null,
    usePM: initial => [initial, () => {}], usePR: initial => ({ current: initial }),
    usePE: () => {}, useMemoPM: effect => effect(), spectrInputValue: event => event?.target?.value,
    iconBtn: { background: 'transparent', border: 'none', color: '#fff' },
    navigator: { clipboard: { writeText: async () => {}, readText: async () => '' } },
    Blob: function Blob() {}, FileReader: function FileReader() {}, URL: { createObjectURL: () => 'blob:fixture', revokeObjectURL: () => {} },
    Array, Date, File, JSON, Math, Number, Set, String, Symbol, TypeError, undefined, Infinity,
  };
  context.globalThis = context;
  if (stateful) {
    const slots = [];
    const refs = [];
    let cursor = 0;
    context.__beginRender = () => { cursor = 0; };
    context.usePM = initial => {
      const index = cursor++;
      if (!(index in slots)) slots[index] = initial;
      return [slots[index], next => { slots[index] = typeof next === 'function' ? next(slots[index]) : next; }];
    };
    context.usePR = initial => {
      const index = cursor++;
      if (!(index in refs)) refs[index] = { current: initial };
      return refs[index];
    };
  }
  return context;
}

function compileTsx(source, name) {
  const result = ts.transpileModule(source, {
    fileName: `${name}.tsx`, reportDiagnostics: true,
    compilerOptions: { target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.None, jsx: ts.JsxEmit.React, jsxFactory: 'React.createElement', jsxFragmentFactory: 'React.Fragment' },
  });
  const errors = (result.diagnostics || []).filter(item => item.category === ts.DiagnosticCategory.Error);
  assert(!errors.length, `${name} TSX diagnostics: ${errors.map(item => ts.flattenDiagnosticMessageText(item.messageText, '\n')).join('; ')}`);
  assert(result.outputText.includes(`function ${name}`), `${name} TSX compile lost declaration`);
  return result.outputText;
}
function compileModule(source, name, dependencies) {
  const imports = dependencies.map(dep => `import { ${dep} } from './${dep}.cjs';`).join('\n');
  const result = ts.transpileModule(`${imports}\nexport ${source}`, {
    fileName: `${name}.tsx`, reportDiagnostics: true,
    compilerOptions: { target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.CommonJS, jsx: ts.JsxEmit.React, jsxFactory: 'React.createElement', jsxFragmentFactory: 'React.Fragment' },
  });
  const errors = (result.diagnostics || []).filter(item => item.category === ts.DiagnosticCategory.Error);
  assert(!errors.length, `${name} module diagnostics: ${errors.map(item => ts.flattenDiagnosticMessageText(item.messageText, '\n')).join('; ')}`);
  assert(result.outputText.includes(`exports.${name}`), `${name} module lost explicit export`);
  for (const dependency of dependencies) assert(result.outputText.includes(`require("./${dependency}.cjs")`), `${name} module lost import ${dependency}`);
  return result.outputText;
}
function runCommonJs(code, context, requireImpl) {
  const module = { exports: {} };
  vm.runInNewContext(code, { ...context, module, exports: module.exports, require: requireImpl });
  return module.exports;
}
function loadGraph(modules, context = makeContext()) {
  const loaded = {};
  for (const name of LOAD_ORDER) {
    const dependencies = EXPECTED_DEPS[name];
    for (const dependency of dependencies) assert(loaded[dependency], `${name} dependency ${dependency} was not loaded first`);
    loaded[name] = runCommonJs(modules[name], context, id => {
      const match = /^\.\/(.+)\.cjs$/.exec(id);
      assert(match && loaded[match[1]], `${name} imported unknown module ${id}`);
      return loaded[match[1]];
    });
    assert(typeof loaded[name][name] === 'function', `CommonJS export missing ${name}`);
  }
  return loaded;
}
function evaluateOriginal(sources, context = makeContext()) {
  const code = `${COMPONENTS.map(name => sources[name]).join('\n')}\nthis.__api = { ${COMPONENTS.join(', ')} };`;
  vm.runInNewContext(code, context);
  for (const name of COMPONENTS) assert(typeof context.__api[name] === 'function', `original export missing ${name}`);
  return context.__api;
}
function normalize(value) {
  if (typeof value === 'function') return `[function:${value.name || 'anonymous'}]`;
  if (typeof value === 'symbol') return `[symbol:${value.description || ''}]`;
  if (Array.isArray(value)) return value.map(normalize);
  if (value && typeof value === 'object') {
    const output = {};
    for (const key of Object.keys(value).sort()) output[key] = normalize(value[key]);
    return output;
  }
  return value;
}
function findNodes(value, predicate, out = []) {
  if (!value || typeof value !== 'object') return out;
  if (predicate(value)) out.push(value);
  if (Array.isArray(value)) { for (const child of value) findNodes(child, predicate, out); return out; }
  for (const child of Object.values(value)) findNodes(child, predicate, out);
  return out;
}
function expand(value, seen = new Set()) {
  if (value === null || value === undefined || typeof value === 'string' || typeof value === 'number' || typeof value === 'boolean') return value;
  if (Array.isArray(value)) return value.map(child => expand(child, seen));
  if (typeof value !== 'object') return value;
  if (typeof value.type === 'function') {
    assert(!seen.has(value.type), `component recursion while expanding ${value.type.name}`);
    const next = new Set(seen); next.add(value.type);
    return expand(value.type(value.props || {}), next);
  }
  const props = {};
  for (const [key, item] of Object.entries(value.props || {})) props[key] = key === 'children' ? expand(item, seen) : item;
  return { type: typeof value.type === 'symbol' ? '[fragment]' : value.type, props };
}
function patternProps(calls) {
  const factory = fixturePatterns();
  const users = fixtureUserPatterns();
  return {
    open: true, onClose: () => { calls.close++; }, userPatterns: users, setUserPatterns: value => { calls.userPatterns = value; },
    defaultId: 'factory:flat', setDefaultId: id => { calls.default = id; }, N: 4,
    onApply: pattern => { calls.apply = pattern?.id || null; }, currentGains: [0.3, -0.1, 0.2, 0.4],
    onStatus: value => { calls.status.push(value); }, onRequestSave: () => { calls.save++; },
    onRenamePattern: (id, name) => { calls.rename = [id, name]; }, onDeletePattern: id => { calls.delete = id; },
    onDuplicatePattern: id => { calls.duplicate = id; }, onSetDefaultPattern: id => { calls.default = id; },
    __factory: factory,
  };
}
function renderState(api, context, calls) {
  context.__beginRender();
  return api.PatternManager(patternProps(calls));
}
function componentNode(root, name) {
  return findNodes(root, node => typeof node.type === 'function' && node.type.name === name)[0] || null;
}
function managerContract(originalSources, importedModules) {
  const stages = [];
  const run = (label) => {
    const context = makeContext({ stateful: true });
    const loaded = label === 'original' ? evaluateOriginal(originalSources, context) : loadGraph(importedModules, context);
    const api = label === 'original' ? loaded : Object.fromEntries(COMPONENTS.map(name => [name, loaded[name][name]]));
    const calls = { close: 0, save: 0, status: [], apply: null };
    const initial = renderState(api, context, calls);
    const initialExpanded = normalize(expand(initial));
    const closedContext = makeContext({ stateful: true });
    const closedCalls = { close: 0, save: 0, status: [], apply: null };
    closedContext.__beginRender();
    const closed = api.PatternManager({ ...patternProps(closedCalls), open: false });
    assert(closed === null, `${label} closed manager did not return null`);
    const save = componentNode(initial, 'MBtn');
    assert(save?.props?.action === 'save-current', `${label} missing SAVE CURRENT action`);
    save.props.onClick({});
    assert(calls.save === 1, `${label} save callback did not fire`);
    const row = findNodes(initial, node => node.type === api.PatternRow && node.props?.pattern?.id === 'user:one')[0];
    assert(row, `${label} missing user row`);
    row.props.onClick({});
    const selected = renderState(api, context, calls);
    assert(componentNode(selected, 'PatternDetail'), `${label} row selection did not expose PatternDetail`);
    const detail = componentNode(selected, 'PatternDetail');
    detail.props.onDelete({});
    const pending = renderState(api, context, calls);
    assert(componentNode(pending, 'PatternDeleteDialog'), `${label} delete action did not expose confirmation dialog`);
    stages.push({ label, initial: initialExpanded, selected: normalize(expand(selected)), pending: normalize(expand(pending)) });
    return { stages: stages.at(-1), calls };
  };
  const one = run('original');
  const two = run('imported');
  for (const key of ['initial', 'selected', 'pending']) {
    assert(JSON.stringify(one.stages[key]) === JSON.stringify(two.stages[key]), `original/imported ${key} tree differs`);
  }
  assert(two.calls.save === 1, 'imported save callback contract failed');
  assert(two.calls.status.length === 0, 'unexpected status callback during deterministic probe');
  return { stages: { initial_sha256: sha256(Buffer.from(JSON.stringify(one.stages.initial))), selected_sha256: sha256(Buffer.from(JSON.stringify(one.stages.selected))), pending_sha256: sha256(Buffer.from(JSON.stringify(one.stages.pending))) }, original: one.calls, imported: two.calls };
}

function replaceArtifact(artifact, scripts, manifestByName, compiled) {
  const byScript = new Map();
  for (const name of COMPONENTS) {
    const component = manifestByName.get(name), script = component && scripts[component.script_index];
    assert(component && script, `${name} manifest/source entry missing`);
    const list = byScript.get(component.script_index) || [];
    list.push({ start: component.start, end: component.end, text: compiled[name] }); byScript.set(component.script_index, list);
  }
  let html = artifact.html;
  const spans = [];
  for (const [scriptIndex, list] of byScript) {
    let source = scripts[scriptIndex].source;
    for (const item of [...list].sort((a, b) => b.start - a.start)) source = source.slice(0, item.start) + item.text + source.slice(item.end);
    spans.push({ start: scripts[scriptIndex].bodyStart, end: scripts[scriptIndex].bodyEnd, text: source });
  }
  for (const span of spans.sort((a, b) => b.start - a.start)) html = html.slice(0, span.start) + span.text + html.slice(span.end);
  return Buffer.from(`${JSON.stringify({ ...artifact, html })}\n`);
}
function browserFixture(originalSources, modules) {
  const escape = source => source.replace(/<\/script/gi, '<\\/script');
  const sourceBlock = COMPONENTS.map(name => escape(originalSources[name])).join('\n');
  const moduleBlock = LOAD_ORDER.map(name => `const MODULE_${name}=loadLocal(${JSON.stringify(modules[name])}, id => modules[id.replace(/^\\.\\//,'').replace(/\\.cjs$/,'')]); modules['${name}'] = MODULE_${name};`).join('\n');
  return `<!doctype html><html><body><div id="original"></div><div id="reimport"></div><pre id="result"></pre><script>
window.__spectrBrowserErrors=[]; window.addEventListener('error', event => window.__spectrBrowserErrors.push(String(event.message||event.error||'browser error'))); window.addEventListener('unhandledrejection', event => window.__spectrBrowserErrors.push(String(event.reason||'unhandled rejection')));
const FACTORY=[{id:'factory:flat',name:'Flat',source:'factory',gains:[.1,-.2,.3,-Infinity]},{id:'factory:tilt',name:'Tilt',source:'factory',gains:[-.2,.1,.4,.2]}];
function commonContext(){ const hooks=[]; const refs=[]; let cursor=0; let onUpdate=()=>{}; const React={createElement(type,props,...children){const p={...(props||{})};const flat=children.flat(Infinity).filter(x=>x!==null&&x!==undefined&&x!==false);if(flat.length)p.children=flat;return{type,props:p};},Fragment:Symbol('Fragment')}; const documentLike={activeElement:null,querySelector:()=>null,querySelectorAll:()=>[],addEventListener:()=>{},removeEventListener:()=>{}}; const windowLike={Spectr:{FACTORY_PATTERNS:FACTORY,resolveGains:(p,n)=>(p?.gains||[]).slice(0,n),factoryGains:id=>FACTORY.find(p=>p.id===id)?.gains||[],fromCanonical:g=>[...g],toCanonical:g=>[...g],makeUserPattern:(name,gains)=>({id:'user:'+name,name,source:'user',gains}),exportEnvelope:p=>({format:'spectr.patterns',version:1,patterns:p}),parseEnvelope:v=>({patterns:Array.isArray(v?.patterns)?v.patterns:[],errors:[]})}}; const runtime={React,document:documentLike,window:windowLike,globalThis:null,iconBtn:{background:'transparent',border:'none',color:'#fff'},spectrInputValue:e=>e?.target?.value,debug:[],state:()=>hooks.slice()}; runtime.globalThis=runtime; runtime.usePM=initial=>{const i=cursor++;if(!(i in hooks))hooks[i]=initial;return[hooks[i],next=>{hooks[i]=typeof next==='function'?next(hooks[i]):next;runtime.debug.push({i,value:hooks[i]});onUpdate();}]}; runtime.usePR=initial=>{const i=cursor++;if(!(i in refs))refs[i]={current:initial};return refs[i]}; runtime.usePE=()=>{}; runtime.useMemoPM=effect=>effect(); runtime.begin=()=>{cursor=0}; runtime.setUpdate=f=>{onUpdate=f}; return runtime; }
function makeOriginal(runtime){const {React,usePM,usePR,usePE,useMemoPM,spectrInputValue,iconBtn}=runtime; const window=runtime.window,document=runtime.document; return (function(){${sourceBlock}
return {${COMPONENTS.join(',')}};})();}
function load(code,req){const module={exports:{}};const exports=module.exports;const require=req;eval(code);return module.exports;}
function makeImported(runtime){const {React,usePM,usePR,usePE,useMemoPM,spectrInputValue,iconBtn}=runtime; const window=runtime.window,document=runtime.document; const modules={}; function loadLocal(code,req){const module={exports:{}};const exports=module.exports;const require=req;eval(code);return module.exports;} ${moduleBlock}; return {${COMPONENTS.map(name => `...modules['${name}']`).join(',')}}; }
function mount(value,parent){if(value===null||value===undefined||value===false)return;if(typeof value==='string'||typeof value==='number'){parent.appendChild(document.createTextNode(String(value)));return;}if(Array.isArray(value)){value.forEach(child=>mount(child,parent));return;}if(typeof value.type==='function'){mount(value.type(value.props||{}),parent);return;}if(typeof value.type==='symbol'){mount(value.props?.children||[],parent);return;}const element=document.createElement(value.type);for(const[key,item]of Object.entries(value.props||{})){if(key==='children'||key==='key'||key==='ref'||key.startsWith('on'))continue;if(key==='style'&&item&&typeof item==='object')Object.assign(element.style,item);else if(item!==undefined)element.setAttribute(key,String(item));}if(value.props?.className)element.className=value.props.className;if(typeof value.props?.onClick==='function')element.addEventListener('click',value.props.onClick);if(typeof value.props?.onChange==='function')element.addEventListener('change',value.props.onChange);if(typeof value.props?.onBlur==='function')element.addEventListener('blur',value.props.onBlur);mount(value.props?.children||[],element);parent.appendChild(element);}
function mountApp(target,api,runtime){ const calls={}; const users=[{id:'user:one',name:'User One',source:'user',gains:[.25,-.5,-Infinity,.75],createdAt:'2026-01-02T00:00:00Z',updatedAt:'2026-01-03T00:00:00Z'}]; const props={open:true,onClose:()=>{calls.close=(calls.close||0)+1},userPatterns:users,setUserPatterns:()=>{},defaultId:'factory:flat',setDefaultId:()=>{},N:4,onApply:p=>{calls.apply=p?.id||null},currentGains:[.3,-.1,.2,.4],onStatus:()=>{},onRequestSave:()=>{calls.save=(calls.save||0)+1},onRenamePattern:()=>{},onDeletePattern:()=>{},onDuplicatePattern:()=>{},onSetDefaultPattern:()=>{}}; let rendering=false; const summarize=value=>{if(value==null||typeof value==='string'||typeof value==='number'||typeof value==='boolean')return value;if(Array.isArray(value))return value.map(summarize);if(typeof value.type==='function')return {component:value.type.name,props:Object.fromEntries(Object.entries(value.props||{}).filter(([key])=>!key.startsWith('on')).map(([key,item])=>[key,summarize(item)]))};return {type:value.type,props:Object.fromEntries(Object.entries(value.props||{}).filter(([key])=>!key.startsWith('on')).map(([key,item])=>[key,summarize(item)]))};}; const render=()=>{if(rendering)return;rendering=true;runtime.begin();target.replaceChildren();runtime.lastTree=api.PatternManager(props);runtime.lastSummary=summarize(runtime.lastTree);mount(runtime.lastTree,target);rendering=false;}; runtime.setUpdate(render); render(); return {runtime,calls,render};}
const originalRuntime=commonContext(); const importedRuntime=commonContext(); const original=makeOriginal(originalRuntime); const imported=makeImported(importedRuntime); const one=mountApp(document.querySelector('#original'),original,originalRuntime); const two=mountApp(document.querySelector('#reimport'),imported,importedRuntime); window.__patternManagerDebug={one,two,originalRuntime,importedRuntime};
const normalizeHtml=node=>node.innerHTML; const initialEqual=normalizeHtml(document.querySelector('#original'))===normalizeHtml(document.querySelector('#reimport')); if(!initialEqual)throw new Error('original/imported initial DOM differs');
const originalRow=document.querySelector('#original [data-spectr-pattern-id="user:one"]'); const importedRow=document.querySelector('#reimport [data-spectr-pattern-id="user:one"]'); if(!originalRow||!importedRow)throw new Error('user row missing'); originalRow.click(); importedRow.click(); if(!document.querySelector('#original [data-spectr-manager-detail]')||!document.querySelector('#reimport [data-spectr-manager-detail]'))throw new Error('row selection did not render detail'); if(normalizeHtml(document.querySelector('#original'))!==normalizeHtml(document.querySelector('#reimport')))throw new Error('original/imported selected DOM differs');
const originalDelete=document.querySelector('#original [data-spectr-manager-action="delete"]'); const importedDelete=document.querySelector('#reimport [data-spectr-manager-action="delete"]'); if(!originalDelete||!importedDelete)throw new Error('delete action missing'); originalDelete.click(); importedDelete.click(); if(!document.querySelector('#original [data-spectr-delete-dialog]')||!document.querySelector('#reimport [data-spectr-delete-dialog]'))throw new Error('delete dialog did not render'); if(normalizeHtml(document.querySelector('#original'))!==normalizeHtml(document.querySelector('#reimport')))throw new Error('original/imported pending-delete DOM differs'); document.documentElement.dataset.spectrPatternManagerReimport='OK'; document.querySelector('#result').textContent=JSON.stringify({initialEqual,selected:true,pendingDelete:true});
</script></body></html>`;
}

function reservePort() { return new Promise((resolve, reject) => { const server = net.createServer(); server.once('error', reject); server.listen(0, '127.0.0.1', () => { const port = server.address().port; server.close(() => resolve(port)); }); }); }
async function runBrowser(file, chrome, marker, label) {
  const body = read(file);
  const server = http.createServer((req, res) => { if (req.url !== '/fixture.html') { res.writeHead(404); res.end(); return; } res.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'content-length': body.length }); res.end(body); });
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  const address = server.address(); const debugPort = await reservePort(); const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-pattern-manager-')); let child; let socket; let stderr = '';
  try {
    child = spawn(path.resolve(chrome), ['--headless=new','--disable-gpu','--disable-background-networking','--disable-component-update','--disable-sync','--no-first-run','--no-default-browser-check','--remote-debugging-address=127.0.0.1',`--remote-debugging-port=${debugPort}`,`--user-data-dir=${profile}`,'--window-size=1280,900','about:blank'], { stdio: ['ignore','ignore','pipe'] });
    child.stderr.on('data', chunk => { stderr += chunk; });
    const deadline = Date.now() + 15000; let websocketUrl;
    while (!websocketUrl && Date.now() < deadline) { try { const pages = await (await fetch(`http://127.0.0.1:${debugPort}/json`)).json(); websocketUrl = pages.find(page => page.type === 'page')?.webSocketDebuggerUrl; } catch {} if (!websocketUrl) await new Promise(resolve => setTimeout(resolve, 50)); }
    assert(websocketUrl, `${label} Chrome DevTools endpoint did not start: ${stderr.trim()}`); assert(typeof WebSocket === 'function', 'Node WebSocket implementation is unavailable');
    socket = new WebSocket(websocketUrl); await new Promise((resolve, reject) => { socket.addEventListener('open', resolve, { once: true }); socket.addEventListener('error', reject, { once: true }); });
    let nextId = 1; const pending = new Map(); socket.addEventListener('message', event => { const message = JSON.parse(event.data); if (!message.id || !pending.has(message.id)) return; const waiter = pending.get(message.id); pending.delete(message.id); if (message.error) waiter.reject(new Error(JSON.stringify(message.error))); else waiter.resolve(message.result); });
    const command = (method, params = {}) => new Promise((resolve, reject) => { const id = nextId++; pending.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params })); });
    const evaluate = async expression => { const result = await command('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true }); if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text || 'browser evaluation failed'); return result.result.value; };
    await command('Page.enable'); await command('Runtime.enable'); await command('Network.enable'); await command('Network.setBlockedURLs', {urls:['https://*']}); const url = `http://127.0.0.1:${address.port}/fixture.html`; await command('Page.navigate', { url });
    const pageDeadline = Date.now() + 15000; while (Date.now() < pageDeadline && !(await evaluate(`location.href === ${JSON.stringify(url)} && document.readyState === 'complete' && document.documentElement.dataset.spectrPatternManagerReimport === ${JSON.stringify(marker)}`))) await new Promise(resolve => setTimeout(resolve, 50));
    const marked = await evaluate(`location.href === ${JSON.stringify(url)} && document.documentElement.dataset.spectrPatternManagerReimport === ${JSON.stringify(marker)}`);
    if (!marked) {
      const diagnostic = await evaluate(`({ readyState:document.readyState, location:location.href, patternManagerType:typeof window.PatternManager, reactType:typeof window.React, marker: document.documentElement.dataset.spectrPatternManagerReimport || null, errors: window.__spectrBrowserErrors || [], result: document.querySelector('#result')?.textContent || '', body: document.body?.innerText?.slice(-1200) || '', debug: window.__patternManagerDebug ? { original: window.__patternManagerDebug.originalRuntime.debug, imported: window.__patternManagerDebug.importedRuntime.debug, originalState: window.__patternManagerDebug.originalRuntime.state(), importedState: window.__patternManagerDebug.importedRuntime.state(), originalSummary: window.__patternManagerDebug.originalRuntime.lastSummary, importedSummary: window.__patternManagerDebug.importedRuntime.lastSummary, originalDetail: !!document.querySelector('#original [data-spectr-manager-detail]'), importedDetail: !!document.querySelector('#reimport [data-spectr-manager-detail]') } : null })`);
      fail(`${label} browser marker missing: ${JSON.stringify(diagnostic)}`);
    }
    const errors = await evaluate('window.__spectrBrowserErrors || []'); assert(Array.isArray(errors) && errors.length === 0, `${label} browser errors: ${JSON.stringify(errors)}`); return { passed: true, transport: 'cdp-http-loopback', stderr: stderr.trim() };
  } finally {
    try { socket?.close(); } catch {}
    if (child && child.exitCode === null) { child.kill('SIGTERM'); await new Promise(resolve => setTimeout(resolve, 200)); if (child.exitCode === null) child.kill('SIGKILL'); }
    await new Promise(resolve => server.close(resolve));
    try { fs.rmSync(profile, { recursive: true, force: true }); } catch {}
  }
}

function fullRuntimeHtml(html) {
  const bootstrap = `<script>window.__spectrBrowserErrors=[];window.__spectrFullRuntimeErrors=window.__spectrBrowserErrors;window.addEventListener('error',event=>window.__spectrFullRuntimeErrors.push(String(event.message||event.error||'runtime error')));window.React={createElement:(type,props,...children)=>({type,props:{...(props||{}),...(children.length?{children}: {})}}),Fragment:Symbol('Fragment'),memo:component=>component,useState:initial=>[typeof initial==='function'?initial():initial,()=>{}],useEffect:()=>{},useRef:initial=>({current:initial}),useMemo:effect=>effect(),useCallback:effect=>effect};window.Spectr={FACTORY_PATTERNS:[{id:'factory:flat',name:'Flat',source:'factory',gains:[.1,-.2,.3,-Infinity}],resolveGains:(p,n)=>(p?.gains||[]).slice(0,n),factoryGains:()=>[],fromCanonical:g=>g,toCanonical:g=>g,makeUserPattern:()=>({}),exportEnvelope:p=>p,parseEnvelope:()=>({patterns:[],errors:[]})};window.ReactDOM={createRoot:()=>({render:()=>{}})};</script>`;
  const probe = `<script>try{if(typeof window.PatternManager!=='function')throw new Error('patched runtime did not register PatternManager');document.documentElement.dataset.spectrPatternManagerReimport='OK'}catch(error){window.__spectrBrowserErrors=window.__spectrBrowserErrors||[];window.__spectrBrowserErrors.push(String(error&&error.message||error));throw error}</script>`;
  const withBootstrap = html.replace('<head>', `<head>${bootstrap}`).replace('<body>', '<body><div id="root"></div>').replace('</body>', `${probe}</body>`);
  assert(withBootstrap !== html, 'patched artifact has no HTML head/body'); return withBootstrap.replace(/[ \t]+(?=\r?$)/gm, '');
}
async function main(argv) {
  const args = parseArgs(argv); if (args.help) { usage(); return; }
  for (const key of ['artifact', 'manifest', 'modules', 'out', 'chrome']) assert(args[key], `--${key} is required`);
  const artifactPath = path.resolve(args.artifact), manifestPath = path.resolve(args.manifest), modulesDir = path.resolve(args.modules), outDir = path.resolve(args.out);
  const artifactBytes = read(artifactPath), artifact = JSON.parse(artifactBytes), manifest = JSON.parse(read(manifestPath));
  const editorPath = path.join(repo, 'resources', 'editor.html'); const editorBefore = read(editorPath);
  assert(artifact && typeof artifact.html === 'string', 'artifact has no html string'); assert(manifest.schema === 'spectr-owned-component-dependency-v1', 'unexpected manifest schema'); assert(manifest.source?.sha256 === sha256(artifactBytes) && manifest.source?.bytes === artifactBytes.length, 'manifest source provenance does not match artifact');
  assert(manifest.component_count === COMPONENTS.length, `expected ${COMPONENTS.length}-component closure, got ${manifest.component_count}`);
  const manifestByName = new Map(manifest.components.map(component => [component.name, component]));
  const scripts = htmlScripts(artifact.html), original = {}, tsx = {}, direct = {}, modules = {}, records = [];
  for (const name of COMPONENTS) {
    const component = manifestByName.get(name), script = component && scripts[component.script_index]; assert(component && script, `${name} manifest/source entry missing`);
    const expectedIds = EXPECTED_DEPS[name].map(dep => manifestByName.get(dep)?.id); assert(JSON.stringify(component.dependencies) === JSON.stringify(expectedIds), `${name} dependency manifest drift`);
    const source = script.source.slice(component.start, component.end); assert(Buffer.byteLength(source) === component.bytes && sha256(Buffer.from(source)) === component.source_sha256, `${name} source hash mismatch`);
    const tsxSource = read(path.join(modulesDir, `${name}.tsx`)).toString('utf8'); const directSource = compileTsx(tsxSource, name); original[name] = source; tsx[name] = tsxSource; direct[name] = directSource;
    records.push({ name, source_bytes: Buffer.byteLength(source), source_sha256: sha256(Buffer.from(source)), tsx_bytes: Buffer.byteLength(tsxSource), tsx_sha256: sha256(Buffer.from(tsxSource)), direct_bytes: Buffer.byteLength(directSource), direct_sha256: sha256(Buffer.from(directSource)), dependencies: [...component.dependencies], captures: [...component.captures], unresolved: [...component.unresolved], external_bindings: [...component.external_bindings] });
  }
  for (const name of COMPONENTS) modules[name] = compileModule(tsx[name], name, EXPECTED_DEPS[name]);
  const originalApi = evaluateOriginal(original); const imported = loadGraph(modules);
  const contract = managerContract(original, modules);
  const originalExpanded = normalize(expand(originalApi.PatternManager(patternProps({ close: 0, save: 0, status: [] })))); const importedExpanded = normalize(expand(imported.PatternManager.PatternManager(patternProps({ close: 0, save: 0, status: [] })))); assert(JSON.stringify(originalExpanded) === JSON.stringify(importedExpanded), 'expanded original/imported manager tree differs');
  const negative = [];
  for (const [name, needle, replacement] of [['MBtn', 'height: 26', 'height: 27'], ['MiniPreview', 'strokeWidth: 0.5', 'strokeWidth: 1']]) {
    const mutated = modules[name].replace(needle, replacement); assert(mutated !== modules[name], `${name} negative-control mutation needle missing`); const mutatedModules = { ...modules, [name]: mutated }; const mutatedGraph = loadGraph(mutatedModules); const mutatedExpanded = normalize(expand(mutatedGraph.PatternManager.PatternManager(patternProps({ close: 0, save: 0, status: [] })))); assert(JSON.stringify(mutatedExpanded) !== JSON.stringify(importedExpanded), `${name} mutation unexpectedly matched`); negative.push({ name, mutation: `${needle} -> ${replacement}`, detected: true });
  }
  const patchedBytes = replaceArtifact(artifact, scripts, manifestByName, direct); const patchedArtifact = JSON.parse(patchedBytes); const patchedScripts = htmlScripts(patchedArtifact.html); assert(patchedScripts.length === scripts.length, 'patched artifact changed executable script count'); for (const [index, script] of patchedScripts.entries()) { try { new vm.Script(script.source, { filename: `patched-runtime-script-${index}.js` }); } catch (error) { fail(`patched artifact script ${index} is not parseable: ${error.message}`); } }
  const browserPath = path.join(outDir, 'browser-fixture.html'); const fullRuntimePath = path.join(outDir, 'full-patched-runtime.html'); write(path.join(outDir, 'reimported.runtime.json'), patchedBytes); for (const name of COMPONENTS) { write(path.join(outDir, 'compiled', `${name}.js`), direct[name]); write(path.join(outDir, 'modules', `${name}.cjs`), modules[name]); } write(browserPath, browserFixture(original, modules)); write(fullRuntimePath, fullRuntimeHtml(patchedArtifact.html));
  const browser = await runBrowser(browserPath, args.chrome, 'OK', 'PatternManager fixture'); const patchedBrowser = await runBrowser(fullRuntimePath, args.chrome, 'OK', 'PatternManager in artifact'); const editorAfter = read(editorPath); assert(sha256(editorBefore) === sha256(editorAfter), 'runner modified editor.html');
  const receipt = { schema: SCHEMA, version: 1, base: { artifact_path: path.relative(repo, artifactPath), artifact_bytes: artifactBytes.length, artifact_sha256: sha256(artifactBytes), manifest_path: path.relative(repo, manifestPath), manifest_sha256: sha256(read(manifestPath)), source_commit: spawnSync('git', ['rev-parse', 'HEAD'], { cwd: repo, encoding: 'utf8' }).stdout.trim(), editor_html: { path: path.relative(repo, editorPath), bytes: editorAfter.length, sha256: sha256(editorAfter) }, patch_script_count: fs.readdirSync(path.join(repo, 'tools')).filter(name => /^patch_materialized_.*\.py$/.test(name)).length }, selected_components: records, modules: Object.fromEntries(COMPONENTS.map(name => [name, { bytes: Buffer.byteLength(modules[name]), sha256: sha256(Buffer.from(modules[name])), imports: EXPECTED_DEPS[name].map(dep => `./${dep}.cjs`), exports: [name] }])), output: { artifact_path: path.relative(repo, path.join(outDir, 'reimported.runtime.json')), artifact_bytes: patchedBytes.length, artifact_sha256: sha256(patchedBytes), fixture_path: path.relative(repo, browserPath), fixture_sha256: sha256(read(browserPath)), full_runtime_path: path.relative(repo, fullRuntimePath), full_runtime_sha256: sha256(read(fullRuntimePath)) }, checks: { explicit_import_export: true, imported_identity: true, vdom_state_callback_contracts: true, expanded_tree_identity: true, patched_artifact_scripts_parsed: true, browser: browser.passed === true, patched_component_in_artifact_browser: patchedBrowser.passed === true, browser_transport: { fixture: browser.transport, patched_component: patchedBrowser.transport }, negative_controls: negative }, contract, scope: { editor_html: { before_sha256: sha256(editorBefore), after_sha256: sha256(editorAfter), unchanged: true }, vellum: { status: 'not_applicable', reason: 'Spectr-only runner has no Vellum path or process' }, patch_scripts_retired: false, full_editor_parity: false, native_parity: false, app_mount: false, artifact_browser: 'patched PatternManager declaration registered in the full artifact browser shell; ReactDOM/App execution is intentionally not asserted because the shell does not expose the complete host facade' }, kill_criteria: ['Any dependency edge is unresolved or imported through a hidden global.', 'Original/imported initial, selected, or pending-delete trees diverge.', 'A planted module mutation is not detected.', 'Chromium does not match original/imported DOM and state transitions.', 'The real importer needs editor.html or Vellum changes before this boundary is reproducible.'] };
  write(path.join(outDir, 'receipt.json'), `${JSON.stringify(receipt, null, 2)}\n`); process.stdout.write(`${JSON.stringify(receipt, null, 2)}\n`);
}
main(process.argv.slice(2)).catch(error => { console.error(error.stack || error.message); process.exit(1); });
