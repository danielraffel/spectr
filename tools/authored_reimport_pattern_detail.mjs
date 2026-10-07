#!/usr/bin/env node
/**
 * Bounded authored re-import experiment for PatternDetail.
 *
 * The closure is PatternDetail -> MBtn and PatternDetail -> MiniPreview.  The
 * runner verifies explicit CommonJS edges, VDOM identity, SVG output, state
 * and callback contracts, a browser fixture, and two independent mutation
 * controls.  It materializes a copy of the frozen runtime artifact only; it
 * never edits editor.html, the checked-in runtime, or Vellum.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import net from 'node:net';
import path from 'node:path';
import os from 'node:os';
import process from 'node:process';
import vm from 'node:vm';
import { spawn, spawnSync } from 'node:child_process';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '..');
let ts;
try {
  ts = createRequire(import.meta.url)(path.join(here, 'wp1-parser', 'node_modules', 'typescript'));
} catch (error) {
  console.error(`PatternDetail re-import runner requires the pinned WP-1 toolchain; run npm ci --prefix ${path.join(here, 'wp1-parser')} (${error.message})`);
  process.exit(2);
}
const COMPONENTS = ['MBtn', 'MiniPreview', 'PatternDetail'];
const SCHEMA = 'spectr-authored-reimport-pattern-detail-v1';

function fail(message) { throw new Error(`authored PatternDetail re-import failed: ${message}`); }
function assert(condition, message) { if (!condition) fail(message); }
function sha256(value) { return crypto.createHash('sha256').update(value).digest('hex'); }
function read(file) { try { return fs.readFileSync(file); } catch (error) { fail(`cannot read ${file}: ${error.message}`); } }
function write(file, value) { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, value); }
function sourceCommit() {
  const result = spawnSync('git', ['rev-parse', 'HEAD'], { cwd: repo, encoding: 'utf8' });
  assert(result.status === 0 && /^[0-9a-f]{40}$/.test(result.stdout.trim()), `cannot resolve source commit: ${(result.stderr || '').trim()}`);
  return result.stdout.trim();
}

function parseArgs(argv) {
  const args = {};
  for (let i = 0; i < argv.length; ++i) {
    const arg = argv[i];
    if (['artifact', 'manifest', 'modules', 'out', 'chrome'].some(key => arg === `--${key}`)) args[arg.slice(2)] = argv[++i];
    else if (arg === '--help') args.help = true;
    else fail(`unknown argument ${arg}`);
  }
  return args;
}
function usage() {
  console.log('usage: node tools/authored_reimport_pattern_detail.mjs --artifact FILE --manifest FILE --modules DIR --out DIR --chrome PATH');
}

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

function baseContext({ stateful = false } = {}) {
  const document = { querySelector: () => null, addEventListener: () => {}, removeEventListener: () => {} };
  const window = { Spectr: { resolveGains: (pattern, n) => (pattern.gains || []).slice(0, n) } };
  const context = {
    React: reactMock(), usePM: initial => [initial, () => {}], usePE: effect => { effect(); },
    useMemoPM: effect => effect(), spectrInputValue: event => event?.target?.value,
    iconBtn: { background: 'transparent', border: 'none', color: '#fff' },
    document, window, String, Number, Math, Infinity, undefined, globalThis: null,
  };
  if (stateful) {
    let hookCursor = 0;
    let effectCount = 0;
    const hookState = [];
    context.__beginRender = () => { hookCursor = 0; };
    context.usePM = initial => {
      const index = hookCursor++;
      if (!(index in hookState)) hookState[index] = initial;
      return [hookState[index], next => { hookState[index] = typeof next === 'function' ? next(hookState[index]) : next; }];
    };
    context.usePE = effect => { if (effectCount++ === 0) effect(); };
  }
  context.globalThis = context;
  return context;
}

function compileTsx(source, name) {
  const result = ts.transpileModule(source, {
    fileName: `${name}.tsx`, reportDiagnostics: true,
    compilerOptions: {
      target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.None,
      jsx: ts.JsxEmit.React, jsxFactory: 'React.createElement', jsxFragmentFactory: 'React.Fragment',
    },
  });
  const errors = (result.diagnostics || []).filter(item => item.category === ts.DiagnosticCategory.Error);
  assert(!errors.length, `${name} TSX diagnostics: ${errors.map(item => ts.flattenDiagnosticMessageText(item.messageText, '\n')).join('; ')}`);
  assert(result.outputText.includes(`function ${name}`), `${name} TSX compile lost declaration`);
  return result.outputText;
}

function compileModule(source, name, dependencies) {
  const imports = dependencies.map(dependency => `import { ${dependency} } from './${dependency}.cjs';`).join('\n');
  const wrapped = `${imports}\nexport ${source}`;
  const result = ts.transpileModule(wrapped, {
    fileName: `${name}.tsx`, reportDiagnostics: true,
    compilerOptions: {
      target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.CommonJS,
      jsx: ts.JsxEmit.React, jsxFactory: 'React.createElement', jsxFragmentFactory: 'React.Fragment',
    },
  });
  const errors = (result.diagnostics || []).filter(item => item.category === ts.DiagnosticCategory.Error);
  assert(!errors.length, `${name} module diagnostics: ${errors.map(item => ts.flattenDiagnosticMessageText(item.messageText, '\n')).join('; ')}`);
  assert(result.outputText.includes(`exports.${name}`), `${name} module lost explicit export`);
  for (const dependency of dependencies)
    assert(result.outputText.includes(`require("./${dependency}.cjs")`), `${name} module lost import ${dependency}`);
  return result.outputText;
}

function runCommonJs(code, context, requireImpl) {
  const module = { exports: {} };
  vm.runInNewContext(code, { ...context, module, exports: module.exports, require: requireImpl });
  return module.exports;
}

function loadGraph(modules, context = baseContext()) {
  const mBtn = runCommonJs(modules.MBtn, context, () => fail('MBtn has no imports'));
  const mini = runCommonJs(modules.MiniPreview, context, () => fail('MiniPreview has no imports'));
  const detail = runCommonJs(modules.PatternDetail, context, id => {
    if (id === './MBtn.cjs') return mBtn;
    if (id === './MiniPreview.cjs') return mini;
    return fail(`unexpected module import ${id}`);
  });
  for (const [name, api] of Object.entries({ MBtn: mBtn, MiniPreview: mini, PatternDetail: detail }))
    assert(typeof api[name] === 'function', `CommonJS export missing ${name}`);
  return { mBtn, mini, detail, context };
}

function evaluateOriginal(sources, context = baseContext()) {
  vm.runInNewContext(`${sources.MBtn}\n${sources.MiniPreview}\n${sources.PatternDetail}\nthis.__api = { MBtn, MiniPreview, PatternDetail };`, context);
  return context.__api;
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

function normalize(value) {
  if (typeof value === 'function') return '[function]';
  if (Array.isArray(value)) return value.map(normalize);
  if (value && typeof value === 'object') {
    const output = {};
    for (const key of Object.keys(value).sort()) output[key] = normalize(value[key]);
    return output;
  }
  return value;
}

function findByAttribute(node, attribute, expected) {
  if (!node || typeof node !== 'object') return null;
  if (node.props && node.props[attribute] === expected) return node;
  const children = node.props?.children || [];
  for (const child of Array.isArray(children) ? children : [children]) {
    const found = findByAttribute(child, attribute, expected);
    if (found) return found;
  }
  return null;
}

function findByType(node, type, output = []) {
  if (!node || typeof node !== 'object') return output;
  if (node.type === type) output.push(node);
  const children = node.props?.children || [];
  for (const child of Array.isArray(children) ? children : [children]) findByType(child, type, output);
  return output;
}

function pattern() {
  return { id: 'user:pattern-detail', name: 'User Pattern', source: 'user', gains: [0.25, -0.5, -Infinity, 0.75], createdAt: '2026-01-02T00:00:00Z', updatedAt: '2026-01-03T00:00:00Z' };
}

function contract(api) {
  const calls = { apply: 0, rename: 0, duplicate: 0, delete: 0, overwrite: 0, setDefault: 0, exportFile: 0, exportClip: 0 };
  const props = {
    pattern: pattern(), N: 4, isDefault: false,
    onApply: () => calls.apply++, onRename: () => calls.rename++, onDuplicate: () => calls.duplicate++,
    onDelete: () => calls.delete++, onOverwrite: () => calls.overwrite++, onSetDefault: () => calls.setDefault++,
    onExport: kind => { if (kind === 'file') calls.exportFile++; else if (kind === 'clipboard') calls.exportClip++; },
  };
  const raw = api.PatternDetail(props);
  assert(raw && raw.type === 'div', 'PatternDetail did not render its detail root');
  const expanded = expand(raw);
  assert(findByAttribute(expanded, 'data-spectr-manager-preview', true), 'detail preview disappeared');
  const svg = findByType(expanded, 'svg')[0];
  assert(svg && findByType(svg, 'rect').length === 4, 'MiniPreview SVG did not render all gain bars');
  for (const action of ['apply', 'set-default', 'duplicate', 'delete', 'export-file', 'export-clip'])
    assert(findByAttribute(expanded, 'data-spectr-manager-action', action), `missing action ${action}`);
  const apply = findByAttribute(expanded, 'data-spectr-manager-action', 'apply');
  const setDefault = findByAttribute(expanded, 'data-spectr-manager-action', 'set-default');
  const duplicate = findByAttribute(expanded, 'data-spectr-manager-action', 'duplicate');
  const del = findByAttribute(expanded, 'data-spectr-manager-action', 'delete');
  const exportFile = findByAttribute(expanded, 'data-spectr-manager-action', 'export-file');
  const exportClip = findByAttribute(expanded, 'data-spectr-manager-action', 'export-clip');
  const overwrite = findByType(raw, api.MBtn).find(node => node.props?.children?.[0] === 'UPDATE FROM CURRENT');
  assert(overwrite, 'user detail lost overwrite button');
  for (const button of [apply, setDefault, duplicate, del, overwrite, exportFile, exportClip]) button.props.onClick({});
  assert(JSON.stringify(calls) === JSON.stringify({ apply: 1, rename: 0, duplicate: 1, delete: 1, overwrite: 1, setDefault: 1, exportFile: 1, exportClip: 1 }), 'action callbacks diverged');
  const factory = expand(api.PatternDetail({ ...props, pattern: { ...pattern(), source: 'factory' }, isDefault: true }));
  assert(!findByAttribute(factory, 'data-spectr-manager-action', 'delete'), 'factory detail exposed delete action');
  assert(!findByAttribute(factory, 'data-spectr-manager-action', 'rename-start'), 'factory detail exposed rename action');
  return expanded;
}

function renameContract(sources, modules) {
  function props(calls) {
    return { pattern: pattern(), N: 4, isDefault: false, onApply: () => {}, onRename: value => calls.push(value), onDuplicate: () => {}, onDelete: () => {}, onOverwrite: () => {}, onSetDefault: () => {}, onExport: () => {} };
  }
  const originalContext = baseContext({ stateful: true });
  const originalApi = evaluateOriginal(sources, originalContext);
  const originalCalls = [];
  originalContext.__beginRender();
  const originalInitial = originalApi.PatternDetail(props(originalCalls));
  const originalRename = findByAttribute(originalInitial, 'data-spectr-manager-action', 'rename-start');
  assert(originalRename, 'original detail lost rename start action');
  originalRename.props.onClick({});
  originalContext.__beginRender();
  const originalEditing = originalApi.PatternDetail(props(originalCalls));
  let originalInput = findByAttribute(originalEditing, 'data-spectr-manager-rename', true);
  assert(originalInput, 'original detail did not enter rename state');
  originalInput.props.onChange({ target: { value: 'Renamed Pattern' } });
  originalContext.__beginRender();
  originalInput = findByAttribute(originalApi.PatternDetail(props(originalCalls)), 'data-spectr-manager-rename', true);
  assert(originalInput, 'original detail lost rename input after edit');
  originalInput.props.onBlur({});
  assert(originalCalls.length === 1 && originalCalls[0] === 'Renamed Pattern', 'original rename callback contract failed');

  const importedContext = baseContext({ stateful: true });
  const imported = loadGraph(modules, importedContext);
  const importedCalls = [];
  importedContext.__beginRender();
  const importedInitial = imported.detail.PatternDetail(props(importedCalls));
  const importedRename = findByAttribute(importedInitial, 'data-spectr-manager-action', 'rename-start');
  assert(importedRename, 'imported detail lost rename start action');
  importedRename.props.onClick({});
  importedContext.__beginRender();
  const importedEditing = imported.detail.PatternDetail(props(importedCalls));
  let importedInput = findByAttribute(importedEditing, 'data-spectr-manager-rename', true);
  assert(importedInput, 'imported detail did not enter rename state');
  importedInput.props.onChange({ target: { value: 'Renamed Pattern' } });
  importedContext.__beginRender();
  importedInput = findByAttribute(imported.detail.PatternDetail(props(importedCalls)), 'data-spectr-manager-rename', true);
  assert(importedInput, 'imported detail lost rename input after edit');
  importedInput.props.onBlur({});
  assert(importedCalls.length === 1 && importedCalls[0] === 'Renamed Pattern', 'imported rename callback contract failed');
  return { original: originalCalls, imported: importedCalls };
}

function replaceArtifact(artifact, scripts, manifestByName, entries, compiled) {
  const byScript = new Map();
  for (const name of entries) {
    const component = manifestByName.get(name), script = component && scripts[component.script_index];
    assert(component && script, `${name} manifest/source entry missing`);
    const list = byScript.get(component.script_index) || [];
    list.push({ start: component.start, end: component.end, text: compiled.direct[name], name });
    byScript.set(component.script_index, list);
  }
  let html = artifact.html;
  const spans = [];
  for (const [scriptIndex, list] of byScript) {
    let source = scripts[scriptIndex].source;
    for (const item of [...list].sort((a, b) => b.start - a.start)) {
      assert(source.slice(item.start, item.end).includes(`function ${item.name}`), `${item.name} range mismatch`);
      source = source.slice(0, item.start) + item.text + source.slice(item.end);
    }
    spans.push({ start: scripts[scriptIndex].bodyStart, end: scripts[scriptIndex].bodyEnd, text: source });
  }
  for (const span of spans.sort((a, b) => b.start - a.start)) html = html.slice(0, span.start) + span.text + html.slice(span.end);
  return Buffer.from(`${JSON.stringify({ ...artifact, html })}\n`);
}

function fixtureHtml(original, modules, patched) {
  const escape = source => source.replace(/<\/script/gi, '<\\/script');
  return `<!doctype html><html><body><div id="original"></div><div id="reimport"></div><div id="patched"></div><pre id="result"></pre><script>
window.__spectrBrowserErrors=[];window.addEventListener('error',event=>window.__spectrBrowserErrors.push(String(event.message||event.error||'browser error')));window.addEventListener('unhandledrejection',event=>window.__spectrBrowserErrors.push(String(event.reason||'unhandled rejection')));
const React={createElement(type,props,...children){const p={...(props||{})};const flat=children.flat(Infinity).filter(x=>x!==null&&x!==undefined&&x!==false);if(flat.length)p.children=flat;return{type,props:p};},Fragment:Symbol('Fragment')};
let browserRenameStartCount=0;const browserHookState=new Map();let browserHookKey=null,browserHookCursor=0;
const beginBrowserRender=key=>{browserHookKey=key;browserHookCursor=0;};
const usePM=initial=>{const slots=browserHookState.get(browserHookKey)||[];browserHookState.set(browserHookKey,slots);const index=browserHookCursor++;if(!(index in slots))slots[index]=initial;return[slots[index],next=>{const value=typeof next==='function'?next(slots[index]):next;if(index===0&&value===true)browserRenameStartCount++;slots[index]=value;}];};
const usePE=()=>{},useMemoPM=effect=>effect(),spectrInputValue=e=>e?.target?.value;
const iconBtn={background:'transparent',border:'none',color:'#fff'};
window.Spectr={resolveGains:(pattern,n)=>(pattern.gains||[]).slice(0,n)};
const ORIGINAL=(()=>{${escape(original.MBtn)}
${escape(original.MiniPreview)}
${escape(original.PatternDetail)}
return{MBtn,MiniPreview,PatternDetail};})();
function loadModule(code,req){const module={exports:{}};const exports=module.exports;const require=req;eval(code);return module.exports;}
const REIMPORT_MBTN=loadModule(${JSON.stringify(modules.MBtn)},()=>{throw new Error('MBtn import');});
const REIMPORT_MINI=loadModule(${JSON.stringify(modules.MiniPreview)},()=>{throw new Error('MiniPreview import');});
const REIMPORT_DETAIL=loadModule(${JSON.stringify(modules.PatternDetail)},id=>{if(id==='./MBtn.cjs')return REIMPORT_MBTN;if(id==='./MiniPreview.cjs')return REIMPORT_MINI;throw new Error('bad import '+id);});
const PATCHED=(()=>{${escape(patched.MBtn)}
${escape(patched.MiniPreview)}
${escape(patched.PatternDetail)}
return{MBtn,MiniPreview,PatternDetail};})();
function mount(value,parent){if(value===null||value===undefined||value===false)return;if(typeof value==='string'||typeof value==='number'){parent.appendChild(document.createTextNode(String(value)));return;}if(typeof value.type==='function'){mount(value.type(value.props||{}),parent);return;}if(typeof value.type==='symbol'){for(const child of(value.props?.children||[]))mount(child,parent);return;}const element=document.createElement(value.type);for(const[key,item]of Object.entries(value.props||{})){if(key==='children'||key==='key'||key==='className'||key.startsWith('on'))continue;if(key==='style'&&item&&typeof item==='object')Object.assign(element.style,item);else if(item!==undefined)element.setAttribute(key,String(item));}if(value.props?.className)element.className=value.props.className;if(typeof value.props?.onClick==='function')element.addEventListener('click',value.props.onClick);if(typeof value.props?.onChange==='function')element.addEventListener('change',value.props.onChange);if(typeof value.props?.onBlur==='function')element.addEventListener('blur',value.props.onBlur);if(typeof value.props?.onKeyDown==='function')element.addEventListener('keydown',value.props.onKeyDown);for(const child of(value.props?.children||[]))mount(child,element);parent.appendChild(element);}
function renderOne(id,component,props){const parent=document.getElementById(id);parent.replaceChildren();beginBrowserRender(id);mount(component(props),parent);browserHookKey=null;}
const originalCalls={apply:0,rename:0,renameValue:null,overwrite:0},reimportCalls={apply:0,rename:0,renameValue:null,overwrite:0},patchedCalls={apply:0,rename:0,renameValue:null,overwrite:0};
const baseProps={pattern:{id:'user:pattern-detail',name:'User Pattern',source:'user',gains:[0.25,-0.5,-Infinity,0.75],createdAt:'2026-01-02T00:00:00Z',updatedAt:'2026-01-03T00:00:00Z'},N:4,isDefault:false,onDuplicate:()=>{},onDelete:()=>{},onSetDefault:()=>{},onExport:()=>{}};
const props={...baseProps,onApply:()=>originalCalls.apply++,onRename:value=>{originalCalls.rename++;originalCalls.renameValue=value;},onOverwrite:()=>originalCalls.overwrite++};
const reimportProps={...baseProps,onApply:()=>reimportCalls.apply++,onRename:value=>{reimportCalls.rename++;reimportCalls.renameValue=value;},onOverwrite:()=>reimportCalls.overwrite++};
const patchedProps={...baseProps,onApply:()=>patchedCalls.apply++,onRename:value=>{patchedCalls.rename++;patchedCalls.renameValue=value;},onOverwrite:()=>patchedCalls.overwrite++};
renderOne('original',ORIGINAL.PatternDetail,props);renderOne('reimport',REIMPORT_DETAIL.PatternDetail,reimportProps);renderOne('patched',PATCHED.PatternDetail,patchedProps);
const originalHtml=document.getElementById('original').innerHTML,reimportHtml=document.getElementById('reimport').innerHTML,patchedHtml=document.getElementById('patched').innerHTML;
if(originalHtml!==reimportHtml){document.getElementById('result').textContent='DOM_DIFF '+originalHtml.length+' '+reimportHtml.length;throw new Error('PatternDetail DOM differs after explicit imports');}
if(originalHtml!==patchedHtml){document.getElementById('result').textContent='PATCHED_DOM_DIFF '+originalHtml.length+' '+patchedHtml.length;throw new Error('patched runtime DOM differs from original');}
if(document.querySelectorAll('#original [data-spectr-manager-action]').length!==7||document.querySelectorAll('#reimport svg rect').length!==4||document.querySelectorAll('#patched svg rect').length!==4){document.getElementById('result').textContent='STRUCTURE_DIFF';throw new Error('PatternDetail browser structure changed');}
for(const [id,calls,component,componentProps] of [['original',originalCalls,ORIGINAL.PatternDetail,props],['reimport',reimportCalls,REIMPORT_DETAIL.PatternDetail,reimportProps],['patched',patchedCalls,PATCHED.PatternDetail,patchedProps]]){let root=document.getElementById(id);root.querySelector('[data-spectr-manager-action="apply"]').click();Array.from(root.querySelectorAll('button')).find(button=>button.textContent==='UPDATE FROM CURRENT').click();root.querySelector('[data-spectr-manager-action="rename-start"]').click();renderOne(id,component,componentProps);root=document.getElementById(id);let input=root.querySelector('[data-spectr-manager-rename]');if(!input)throw new Error('browser rename input missing after state update for '+id);input.value='Renamed Browser Pattern';input.dispatchEvent(new Event('change',{bubbles:true}));renderOne(id,component,componentProps);input=document.getElementById(id).querySelector('[data-spectr-manager-rename]');if(!input)throw new Error('browser rename input missing after edit for '+id);input.dispatchEvent(new Event('blur',{bubbles:true}));if(calls.apply!==1||calls.overwrite!==1||calls.rename!==1||calls.renameValue!=='Renamed Browser Pattern'){document.getElementById('result').textContent=JSON.stringify({id,calls,browserRenameStartCount});throw new Error('browser action or rename callback diverged for '+id);}}
if(browserRenameStartCount!==3)throw new Error('browser rename-start handlers did not execute');
document.documentElement.dataset.spectrAuthoredReimport='OK';document.getElementById('result').textContent='SPECTR_AUTHORED_REIMPORT_PATTERN_DETAIL_BROWSER_OK';
</script></body></html>`;
}

function reservePort() {
  return new Promise((resolve, reject) => {
    const socket = net.createServer();
    socket.once('error', reject);
    socket.listen(0, '127.0.0.1', () => {
      const address = socket.address();
      assert(address && typeof address === 'object', 'could not reserve a browser debugging port');
      const port = address.port;
      socket.close(error => error ? reject(error) : resolve(port));
    });
  });
}

function browserMarkerExpression(marker) {
  const match = /^([A-Za-z0-9_-]+)="([^"]*)"$/.exec(marker);
  assert(match, `invalid browser marker ${marker}`);
  return `document.documentElement.getAttribute(${JSON.stringify(match[1])}) === ${JSON.stringify(match[2])}`;
}

async function runHttpBrowser(file, chrome, marker, label) {
  // Serve the fixture over a loopback HTTP origin and inspect it through CDP.
  // This keeps arbitrary artifact input outside Chrome's file-origin privileges
  // and lets us terminate the browser explicitly after the marker appears.
  const payload = read(file);
  const server = http.createServer((request, response) => {
    if (request.url !== '/artifact.html') { response.writeHead(404); response.end(); return; }
    response.writeHead(200, {
      'content-type': 'text/html; charset=utf-8',
      'content-length': payload.length,
      connection: 'close',
    });
    response.end(payload);
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const address = server.address();
  assert(address && typeof address === 'object', 'browser fixture server did not expose an address');
  const debugPort = await reservePort();
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-authored-reimport-'));
  let child;
  let childClosed = false;
  let childExit = null;
  let childCloseResolve;
  const childClose = new Promise(resolve => { childCloseResolve = resolve; });
  let socket;
  let stderr = '';
  const finishServer = () => new Promise(resolve => {
    if (server.closeAllConnections) server.closeAllConnections();
    server.close(() => resolve());
  });
  const finishChild = async () => {
    if (!child || childClosed) return;
    child.kill('SIGTERM');
    await Promise.race([childClose, new Promise(resolve => setTimeout(resolve, 2000))]);
    if (!childClosed) child.kill('SIGKILL');
    await Promise.race([childClose, new Promise(resolve => setTimeout(resolve, 2000))]);
  };
  try {
    child = spawn(path.resolve(chrome), [
      '--headless=new', '--disable-gpu', '--disable-background-networking',
      '--disable-component-update', '--disable-domain-reliability', '--disable-sync',
      '--no-first-run', '--no-default-browser-check',
      '--remote-debugging-address=127.0.0.1', `--remote-debugging-port=${debugPort}`,
      `--user-data-dir=${profile}`, '--window-size=1280,900', 'about:blank',
    ], { stdio: ['ignore', 'ignore', 'pipe'] });
    child.stderr.on('data', chunk => { stderr += chunk; });
    child.once('error', error => {
      childClosed = true;
      childExit = error;
      childCloseResolve();
    });
    child.once('close', (code, signal) => {
      childClosed = true;
      childExit = { code, signal };
      childCloseResolve();
    });
    const deadline = Date.now() + 15000;
    let websocketUrl;
    while (!websocketUrl && Date.now() < deadline) {
      if (childClosed) break;
      try {
        const pages = await (await fetch(`http://127.0.0.1:${debugPort}/json`)).json();
        websocketUrl = pages.find(page => page.type === 'page')?.webSocketDebuggerUrl;
      } catch {}
      if (!websocketUrl) await new Promise(resolve => setTimeout(resolve, 50));
    }
    assert(websocketUrl, `${label} Chrome DevTools endpoint did not start${childExit ? ` (${JSON.stringify(childExit)})` : ''}: ${stderr.trim()}`);
    assert(typeof WebSocket === 'function', 'Node WebSocket implementation is unavailable for browser probe');
    socket = new WebSocket(websocketUrl);
    await new Promise((resolve, reject) => {
      socket.addEventListener('open', resolve, { once: true });
      socket.addEventListener('error', reject, { once: true });
    });
    let nextId = 1;
    const pending = new Map();
    socket.addEventListener('message', event => {
      const message = JSON.parse(event.data);
      if (!message.id || !pending.has(message.id)) return;
      const waiter = pending.get(message.id);
      pending.delete(message.id);
      if (message.error) waiter.reject(new Error(JSON.stringify(message.error)));
      else waiter.resolve(message.result);
    });
    const command = (method, params = {}) => new Promise((resolve, reject) => {
      const id = nextId++;
      pending.set(id, { resolve, reject });
      socket.send(JSON.stringify({ id, method, params }));
    });
    const evaluate = async expression => {
      const result = await command('Runtime.evaluate', {
        expression, awaitPromise: true, returnByValue: true,
      });
      if (result.exceptionDetails) {
        throw new Error(result.exceptionDetails.exception?.description
          || result.exceptionDetails.text || 'browser evaluation failed');
      }
      return result.result.value;
    };
    await command('Page.enable');
    await command('Runtime.enable');
    const url = `http://127.0.0.1:${address.port}/artifact.html`;
    await command('Page.navigate', { url });
    const pageDeadline = Date.now() + 15000;
    while (Date.now() < pageDeadline) {
      if (await evaluate(`location.href === ${JSON.stringify(url)} && document.readyState === 'complete'`)) break;
      await new Promise(resolve => setTimeout(resolve, 50));
    }
    assert(await evaluate(`location.href === ${JSON.stringify(url)} && document.readyState === 'complete'`), `${label} document did not load`);
    const expected = browserMarkerExpression(marker);
    const markerDeadline = Date.now() + 15000;
    while (Date.now() < markerDeadline && !(await evaluate(expected)))
      await new Promise(resolve => setTimeout(resolve, 50));
    if (!await evaluate(expected)) {
      let diagnostic = '';
      try {
        diagnostic = JSON.stringify(await evaluate(`({ errors: window.__spectrFullRuntimeErrors || window.__spectrBrowserErrors || [], result: document.querySelector('#result')?.textContent || '', body: document.body?.innerText?.slice(-1000) || '' })`));
      } catch (error) { diagnostic = error.message; }
      throw new Error(`${label} browser probe failed: ${diagnostic}`);
    }
    return { marker, passed: true, stderr: stderr.trim(), transport: 'cdp-http-loopback' };
  } finally {
    try { socket?.close(); } catch {}
    await finishChild();
    await finishServer();
    try { fs.rmSync(profile, { recursive: true, force: true }); }
    catch (error) { if (error?.code !== 'ENOTEMPTY') throw error; }
  }
}

function runBrowser(file, chrome) {
  return runHttpBrowser(file, chrome, 'data-spectr-authored-reimport="OK"', 'PatternDetail fixture');
}

// The materialized artifact intentionally does not bundle ReactDOM. This probe
// executes the selected patched declarations in the artifact's real script
// order and walks PatternDetail's returned tree. It is a component-in-artifact
// check, not an App mount or full-editor parity claim.
function fullRuntimeHtml(html) {
  const bootstrap = `<script>
window.__spectrFullRuntimeErrors=[];window.addEventListener('error',event=>window.__spectrFullRuntimeErrors.push(String(event.message||event.error||'runtime error')));
window.React={createElement:(type,props,...children)=>({type,props:{...(props||{}),...(children.length?{children}: {})}}),Fragment:Symbol('Fragment'),memo:component=>component,useState:initial=>[typeof initial==='function'?initial():initial,()=>{}],useEffect:()=>{},useRef:initial=>({current:initial}),useMemo:effect=>effect(),useCallback:effect=>effect};
window.Spectr={FACTORY_PATTERNS:[],resolveGains:(pattern,n)=>(pattern?.gains||[]).slice(0,n),loadStore:()=>[],loadDefaultId:()=>null,saveStore:()=>{},saveDefaultId:()=>{}};
window.ReactDOM={createRoot:()=>({render:()=>{}})};
</script>`;
  const probe = `<script>
try {
  if (typeof window.PatternDetail !== 'function' || typeof window.MBtn !== 'function' || typeof window.MiniPreview !== 'function') throw new Error('patched runtime did not register selected component declarations');
  const probePattern={id:'full-runtime-probe',name:'Full Runtime Probe',source:'user',gains:[0.25,-0.5,-Infinity,0.75],createdAt:'2026-01-02T00:00:00Z',updatedAt:'2026-01-03T00:00:00Z'};
  const probeValue=window.PatternDetail({pattern:probePattern,N:4,isDefault:false,onApply:()=>{},onRename:()=>{},onDuplicate:()=>{},onDelete:()=>{},onOverwrite:()=>{},onSetDefault:()=>{},onExport:()=>{}});
  const walk=(value,seen=new Set())=>{if(value===null||value===undefined||value===false||typeof value==='string'||typeof value==='number')return;if(Array.isArray(value)){for(const child of value)walk(child,seen);return;}if(typeof value.type==='function'){if(seen.has(value.type))throw new Error('component recursion');const next=new Set(seen);next.add(value.type);walk(value.type(value.props||{}),next);return;}for(const child of(value.props?.children||[]))walk(child,seen);};
  walk(probeValue); if(window.__spectrFullRuntimeErrors.length)throw new Error(window.__spectrFullRuntimeErrors.join(';')); document.documentElement.dataset.spectrPatchedComponentRuntime='OK';
} catch(error) { window.__spectrFullRuntimeErrors.push(String(error&&error.message||error)); throw error; }
</script>`;
  const withBootstrap = html.replace('<head>', `<head>${bootstrap}`).replace('<body>', '<body><div id="root"></div>').replace('</body>', `${probe}</body>`);
  assert(withBootstrap !== html, 'patched artifact has no HTML head/body for full-runtime browser probe');
  // Materialized source can contain cosmetic trailing spaces. Normalize only
  // line endings in this generated browser fixture so the evidence artifact is
  // clean-output lintable without changing executable content.
  return withBootstrap.replace(/[ \t]+(?=\r?$)/gm, '');
}

function runFullRuntimeBrowser(file, chrome) {
  return runHttpBrowser(file, chrome, 'data-spectr-patched-component-runtime="OK"', 'patched component in artifact');
}

async function main(argv) {
  const args = parseArgs(argv);
  if (args.help) { usage(); return; }
  for (const key of ['artifact', 'manifest', 'modules', 'out', 'chrome']) assert(args[key], `--${key} is required`);
  const artifactPath = path.resolve(args.artifact), manifestPath = path.resolve(args.manifest), modulesDir = path.resolve(args.modules), outDir = path.resolve(args.out);
  const artifactBytes = read(artifactPath), artifact = JSON.parse(artifactBytes), manifest = JSON.parse(read(manifestPath));
  const editorPath = path.join(repo, 'resources', 'editor.html');
  const editorBefore = read(editorPath);
  assert(artifact && typeof artifact.html === 'string', 'artifact has no html string');
  assert(manifest.schema === 'spectr-owned-component-dependency-v1', 'unexpected manifest schema');
  assert(manifest.source?.sha256 === sha256(artifactBytes) && manifest.source?.bytes === artifactBytes.length, 'manifest source provenance does not match artifact');
  const manifestByName = new Map(manifest.components.map(component => [component.name, component]));
  const expectedDeps = { MBtn: [], MiniPreview: [], PatternDetail: ['MBtn', 'MiniPreview'] };
  const scripts = htmlScripts(artifact.html), original = {}, tsx = {}, direct = {}, modules = {}, records = [];
  for (const name of COMPONENTS) {
    const component = manifestByName.get(name), script = component && scripts[component.script_index];
    assert(component && script, `${name} manifest/source entry missing`);
    const expectedDependencyIds = expectedDeps[name].map(dependency => manifestByName.get(dependency)?.id);
    assert(expectedDependencyIds.every(Boolean), `${name} dependency manifest is missing a referenced component`);
    assert(component.dependencies.every(id => /^component:[A-Za-z0-9_$]+:[0-9a-f]{64}$/.test(id)), `${name} dependency manifest contains a malformed identity`);
    assert(JSON.stringify(component.dependencies) === JSON.stringify(expectedDependencyIds), `${name} dependency manifest drift`);
    const source = script.source.slice(component.start, component.end);
    assert(Buffer.byteLength(source) === component.bytes && sha256(Buffer.from(source)) === component.source_sha256, `${name} source hash mismatch`);
    const tsxSource = read(path.join(modulesDir, `${name}.tsx`)).toString('utf8');
    const directSource = compileTsx(tsxSource, name);
    original[name] = source; tsx[name] = tsxSource; direct[name] = directSource;
    records.push({ name, source_bytes: Buffer.byteLength(source), source_sha256: sha256(Buffer.from(source)), tsx_bytes: Buffer.byteLength(tsxSource), tsx_sha256: sha256(Buffer.from(tsxSource)), direct_bytes: Buffer.byteLength(directSource), direct_sha256: sha256(Buffer.from(directSource)), dependencies: [...component.dependencies], captures: [...component.captures], unresolved: [...component.unresolved], external_bindings: [...component.external_bindings] });
  }
  modules.MBtn = compileModule(tsx.MBtn, 'MBtn', []);
  modules.MiniPreview = compileModule(tsx.MiniPreview, 'MiniPreview', []);
  modules.PatternDetail = compileModule(tsx.PatternDetail, 'PatternDetail', ['MBtn', 'MiniPreview']);
  const originalApi = evaluateOriginal(original);
  const imported = loadGraph(modules);
  const originalExpanded = contract(originalApi);
  const importedExpanded = contract({ MBtn: imported.mBtn.MBtn, MiniPreview: imported.mini.MiniPreview, PatternDetail: imported.detail.PatternDetail });
  const rename = renameContract(original, modules);
  assert(JSON.stringify(normalize(originalExpanded)) === JSON.stringify(normalize(importedExpanded)), 'explicit import/export VDOM differs from original');
  const rawImported = imported.detail.PatternDetail({ pattern: pattern(), N: 4, isDefault: false, onApply: () => {}, onRename: () => {}, onDuplicate: () => {}, onDelete: () => {}, onOverwrite: () => {}, onSetDefault: () => {}, onExport: () => {} });
  assert(findByType(rawImported, imported.mBtn.MBtn).length === 7, 'PatternDetail did not retain all imported MBtn identities');
  assert(findByType(rawImported, imported.mini.MiniPreview).length === 1, 'PatternDetail did not retain imported MiniPreview identity');
  const negative = [];
  for (const [name, needle, replacement] of [['MBtn', 'height: 26', 'height: 27'], ['MiniPreview', 'strokeWidth: 0.5', 'strokeWidth: 1']]) {
    const mutated = modules[name].replace(needle, replacement);
    assert(mutated !== modules[name], `${name} negative-control mutation needle missing`);
    const mutatedModules = { ...modules, [name]: mutated };
    let detected = false;
    try {
      const graph = loadGraph(mutatedModules);
      const mutatedExpanded = contract({ MBtn: graph.mBtn.MBtn, MiniPreview: graph.mini.MiniPreview, PatternDetail: graph.detail.PatternDetail });
      assert(JSON.stringify(normalize(mutatedExpanded)) === JSON.stringify(normalize(originalExpanded)), `${name} mutation unexpectedly matched`);
    } catch { detected = true; }
    assert(detected, `${name} mutation was not detected`);
    negative.push({ name, mutation: `${needle} -> ${replacement}`, detected });
  }
  const patchedBytes = replaceArtifact(artifact, scripts, manifestByName, COMPONENTS, { direct });
  const patchedArtifact = JSON.parse(patchedBytes);
  const patchedScripts = htmlScripts(patchedArtifact.html);
  assert(patchedScripts.length === scripts.length, 'patched artifact changed executable script count');
  for (const [index, script] of patchedScripts.entries()) {
    try { new vm.Script(script.source, { filename: `patched-runtime-script-${index}.js` }); }
    catch (error) { fail(`patched artifact script ${index} is not parseable: ${error.message}`); }
  }
  const runtimeContext = {
    React: { useState: initial => [initial, () => {}], useEffect: () => {}, useRef: initial => ({ current: initial }), useMemo: effect => effect() },
    window: {}, document: { querySelector: () => null, querySelectorAll: () => [], addEventListener: () => {}, removeEventListener: () => {} },
    console, globalThis: null,
  };
  runtimeContext.globalThis = runtimeContext;
  const runtimeScriptIndex = manifestByName.get('PatternDetail').script_index;
  try { vm.runInNewContext(patchedScripts[runtimeScriptIndex].source, runtimeContext, { filename: `patched-runtime-script-${runtimeScriptIndex}.js` }); }
  catch (error) { fail(`patched runtime component script ${runtimeScriptIndex} failed in runtime context: ${error.message}`); }
  assert(typeof runtimeContext.window.PatternManager === 'function', 'patched runtime component script did not register PatternManager');
  const patchedRuntime = {};
  for (const name of COMPONENTS) {
    const script = patchedScripts[manifestByName.get(name).script_index];
    assert(script?.source.includes(direct[name]), `${name} direct output is absent from patched artifact`);
    const sourceFile = ts.createSourceFile(`${name}.js`, script.source, ts.ScriptTarget.Latest, true, ts.ScriptKind.JS);
    let declaration = null;
    function visit(node) {
      if (declaration || !ts.isFunctionDeclaration(node) || node.name?.text !== name) {
        if (!declaration) ts.forEachChild(node, visit);
        return;
      }
      declaration = node;
    }
    ts.forEachChild(sourceFile, visit);
    assert(declaration, `${name} declaration missing from patched artifact`);
    patchedRuntime[name] = script.source.slice(declaration.getStart(sourceFile), declaration.end);
  }
  const browserPath = path.join(outDir, 'browser-fixture.html');
  const fullRuntimePath = path.join(outDir, 'full-patched-runtime.html');
  write(path.join(outDir, 'reimported.runtime.json'), patchedBytes);
  for (const name of COMPONENTS) write(path.join(outDir, 'compiled', `${name}.js`), direct[name]);
  for (const name of ['MBtn', 'MiniPreview', 'PatternDetail']) write(path.join(outDir, 'modules', `${name}.cjs`), modules[name]);
  write(browserPath, fixtureHtml(original, modules, patchedRuntime));
  write(fullRuntimePath, fullRuntimeHtml(patchedArtifact.html));
  const browser = await runBrowser(browserPath, args.chrome);
  const fullRuntimeBrowser = await runFullRuntimeBrowser(fullRuntimePath, args.chrome);
  const editorAfter = read(editorPath);
  assert(sha256(editorBefore) === sha256(editorAfter), 'runner modified editor.html during the experiment');
  const receipt = {
    schema: SCHEMA, version: 1,
    base: { artifact_path: path.relative(repo, artifactPath), artifact_bytes: artifactBytes.length, artifact_sha256: sha256(artifactBytes), manifest_path: path.relative(repo, manifestPath), manifest_sha256: sha256(read(manifestPath)), source_commit: sourceCommit(), editor_html: { path: path.relative(repo, editorPath), bytes: read(editorPath).length, sha256: sha256(read(editorPath)) }, patch_script_count: fs.readdirSync(path.join(repo, 'tools')).filter(name => /^patch_materialized_.*\.py$/.test(name)).length },
    selected_components: records,
    modules: Object.fromEntries(Object.entries(modules).map(([name, code]) => [name, { bytes: Buffer.byteLength(code), sha256: sha256(Buffer.from(code)), imports: name === 'PatternDetail' ? ['./MBtn.cjs', './MiniPreview.cjs'] : [], exports: [name] }])),
    output: { artifact_path: path.relative(repo, path.join(outDir, 'reimported.runtime.json')), artifact_bytes: patchedBytes.length, artifact_sha256: sha256(patchedBytes), fixture_path: path.relative(repo, browserPath), fixture_sha256: sha256(read(browserPath)), full_runtime_path: path.relative(repo, fullRuntimePath), full_runtime_sha256: sha256(read(fullRuntimePath)) },
    checks: { explicit_import_export: true, imported_identity: true, vdom_svg_callbacks: true, action_callbacks: true, rename_callbacks: rename.original.length === 1 && rename.imported.length === 1, patched_artifact_scripts_parsed: true, patched_runtime_component_script_executed: true, patched_component_declarations_executed_in_browser: true, browser: browser.passed === true, patched_component_in_artifact_browser: fullRuntimeBrowser.passed === true, browser_transport: { fixture: browser.transport, patched_component: fullRuntimeBrowser.transport }, negative_controls: negative },
    scope: { editor_html: { before_sha256: sha256(editorBefore), after_sha256: sha256(editorAfter), unchanged: true }, vellum: { status: 'not_applicable', reason: 'this runner has no Vellum path or process' }, patch_scripts_retired: false, full_editor_parity: false, native_parity: false, artifact_browser: 'selected patched component executed in the full artifact script context; App mount is intentionally not asserted because the materialized artifact does not bundle ReactDOM' },
    kill_criteria: ['Any dependency edge is unresolved or imported through a hidden global.', 'A module identity edge changes during compilation or loading.', 'Nested VDOM, SVG, state, or callback behavior diverges.', 'Either planted MBtn or MiniPreview mutation is not detected.', 'Chromium does not match DOM structure and marker.', 'The real importer needs editor.html or Vellum changes before this boundary is reproducible.'],
  };
  write(path.join(outDir, 'receipt.json'), `${JSON.stringify(receipt, null, 2)}\n`);
  process.stdout.write(`${JSON.stringify(receipt, null, 2)}\n`);
}

main(process.argv.slice(2)).catch(error => { console.error(error.stack || error.message); process.exit(1); });
