from pathlib import Path
import sys,subprocess,json,hashlib,shutil,tarfile,os
src=Path(sys.argv[1]);out=Path(sys.argv[2]);arch=sys.argv[3];sha=sys.argv[4];sdksha=sys.argv[5]
assert arch in ('aarch64','x86_64')
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=src,text=True).strip()==sha
assert not subprocess.check_output(['git','status','--porcelain'],cwd=src,text=True).strip()
cache={line.split('=',1)[0].split(':',1)[0]:line.split('=',1)[1] for line in (src/'build/CMakeCache.txt').read_text().splitlines() if '=' in line and not line.startswith(('#','//'))}
assert cache['SPECTR_SOURCE_GIT_SHA']==sha and cache['PULP_SDK_SOURCE_GIT_SHA']==sdksha
assert cache['CMAKE_BUILD_TYPE']=='Release' and cache['SPECTR_SOURCE_GIT_DIRTY']=='FALSE'
assert cache['PULP_SDK_DISTRIBUTION_ELIGIBLE']=='TRUE' and cache['PULP_SDK_PROVENANCE_KIND']=='release'
for key in ('SPECTR_ENABLE_TEST_SEAMS','SPECTR_ENABLE_PERF_FIXTURES','SPECTR_SHARED_PRODUCT_ACCEPTANCE','SPECTR_SHARED_NATIVE_HOST_PROBE'):assert cache.get(key,'OFF') in ('OFF','FALSE','0')
out.mkdir(exist_ok=False);stage=out/'Spectr-v1.0.11';stage.mkdir()
inputs={'Spectr':'Spectr','Spectr.clap':'CLAP/Spectr.clap',f'Spectr.vst3/Contents/{arch}-linux/Spectr.so':'VST3/libSpectr.so'}
modules={}
for dst,relative in inputs.items():
 p=src/'build'/relative;data=p.read_bytes();assert data[:5]==b'\x7fELF\x02' and int.from_bytes(data[18:20],'little')==({'aarch64':183,'x86_64':62}[arch]);q=stage/dst;q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q);q.chmod(0o755);modules[dst]={'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)}
common='''#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SPECTR_VST3_DIR="${SPECTR_VST3_DIR:-$HOME/.vst3}"
SPECTR_CLAP_DIR="${SPECTR_CLAP_DIR:-$HOME/.clap}"
SPECTR_BIN_DIR="${SPECTR_BIN_DIR:-$HOME/.local/bin}"
'''
install=common+f'''cd "$ROOT"
sha256sum -c SHA256SUMS
install -d "$SPECTR_VST3_DIR/Spectr.vst3/Contents/{arch}-linux" "$SPECTR_CLAP_DIR" "$SPECTR_BIN_DIR"
install -m 755 Spectr "$SPECTR_BIN_DIR/Spectr"
install -m 755 Spectr.clap "$SPECTR_CLAP_DIR/Spectr.clap"
install -m 755 Spectr.vst3/Contents/{arch}-linux/Spectr.so "$SPECTR_VST3_DIR/Spectr.vst3/Contents/{arch}-linux/Spectr.so"
echo 'Spectr 1.0.11 installed.'
'''
uninstall=common+f'''# Only remove this version's exact payload; preserve changed/newer files.
remove_matching() {{
  if [[ -f "$2" ]] && cmp -s -- "$1" "$2"; then rm -- "$2"; fi
}}
remove_matching "$ROOT/Spectr" "$SPECTR_BIN_DIR/Spectr"
remove_matching "$ROOT/Spectr.clap" "$SPECTR_CLAP_DIR/Spectr.clap"
remove_matching "$ROOT/Spectr.vst3/Contents/{arch}-linux/Spectr.so" "$SPECTR_VST3_DIR/Spectr.vst3/Contents/{arch}-linux/Spectr.so"
rmdir -- "$SPECTR_VST3_DIR/Spectr.vst3/Contents/{arch}-linux" "$SPECTR_VST3_DIR/Spectr.vst3/Contents" "$SPECTR_VST3_DIR/Spectr.vst3" 2>/dev/null || true
echo 'Matching Spectr 1.0.11 files removed.'
'''
for name,value in [('install.sh',install),('uninstall.sh',uninstall)]:p=stage/name;p.write_text(value);p.chmod(0o755)
(stage/'README.md').write_text(f'''# Spectr 1.0.11 for Linux {arch}\n\nRun `./install.sh` as your normal user to install the standalone, VST3 and CLAP into ~/.local/bin, ~/.vst3 and ~/.clap. Run Spectr from ~/.local/bin/Spectr. Rescan plugins in your host. Runtime libraries required: Wayland/X11, Vulkan for hosted editors, ALSA/PipeWire audio and standard C++/glibc libraries.\n\nOverride destinations with SPECTR_VST3_DIR, SPECTR_CLAP_DIR and SPECTR_BIN_DIR; use the same values for `./uninstall.sh`. Keep this extracted directory for uninstall. The uninstaller preserves files that differ from this package.\n\nSource: https://github.com/danielraffel/spectr/commit/{sha}\nFramework: official Pulp SDK 1.149.0, source {sdksha}.\n''')
(stage/'BUILD-MANIFEST.txt').write_text(f'version=1.0.11\nsource_git_sha={sha}\nbuild_type=Release\narchitecture={arch}\npulp_sdk_source_git_sha={sdksha}\npulp_sdk_version=1.149.0\n')
(stage/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(stage).as_posix()+'\n' for p in sorted(stage.rglob('*')) if p.is_file()))
smoke=out/'install-smoke';env=dict(os.environ,SPECTR_VST3_DIR=str(smoke/'vst3'),SPECTR_CLAP_DIR=str(smoke/'clap'),SPECTR_BIN_DIR=str(smoke/'bin'))
subprocess.run([str(stage/'install.sh')],env=env,check=True)
installed={'Spectr':smoke/'bin/Spectr','Spectr.clap':smoke/'clap/Spectr.clap',f'Spectr.vst3/Contents/{arch}-linux/Spectr.so':smoke/f'vst3/Spectr.vst3/Contents/{arch}-linux/Spectr.so'}
for name,p in installed.items():assert hashlib.sha256(p.read_bytes()).hexdigest()==modules[name]['sha256']
subprocess.run([str(stage/'uninstall.sh')],env=env,check=True)
assert all(not p.exists() for p in installed.values())
# Reinstall for installed module runtime qualification.
subprocess.run([str(stage/'install.sh')],env=env,check=True)
archive=out/f'Spectr-v1.0.11-linux-{arch}.tar.xz'
with tarfile.open(archive,'w:xz',preset=3) as tar:tar.add(stage,arcname=stage.name)
receipt={'source':sha,'sdk_source':sdksha,'architecture':arch,'modules':modules,'archive':str(archive),'archive_bytes':archive.stat().st_size,'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'install_uninstall_smoke':True,'installed':{k:str(v) for k,v in installed.items()},'runtime_qualified':False}
(out/'package-receipt.json').write_text(json.dumps(receipt,indent=2))
print(json.dumps(receipt,indent=2))
