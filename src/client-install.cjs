'use strict';
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const {Readable, Transform} = require('node:stream');
const {pipeline} = require('node:stream/promises');
const CLIENT = Object.freeze({version:'2026.9.1', url:'https://github.com/bitwarden/clients/releases/download/cli-v2026.9.1/bw-oss-windows-2026.9.1.zip',sha256:'c39d346239d926ad7955d1ee7acddf557ff216f3186b5383f40acc402f1adefe',sourceCommit:'8246ae9c9a484a0a69f8b27203034555fb872523'});
const destinationRoot = root => path.join(root,'client');
const clientDestination = root => path.join(destinationRoot(root),'bw.exe');
function readClientHash(root) {
  try {
    const file = path.join(destinationRoot(root),'client.json');
    if (fs.statSync(file).size > 4096) return null;
    const metadata = JSON.parse(fs.readFileSync(file,'utf8'));
    return metadata.version === CLIENT.version && metadata.downloadSha256 === CLIENT.sha256 && /^[a-f0-9]{64}$/.test(metadata.sha256) ? metadata.sha256 : null;
  } catch {return null;}
}
let pending;
async function installClient(root) {
  if (process.platform !== 'win32') throw new Error('Der automatische Client-Download ist für Windows verfügbar.');
  if (pending) return pending;
  pending = install(root);
  try {return await pending;} finally {pending=null;}
}
async function install(root) {
  const base = destinationRoot(root);
  await fsp.mkdir(base,{recursive:true});
  const stage = await fsp.mkdtemp(path.join(base,'.install-'));
  const archive = path.join(stage,'client.zip');
  const extracted = path.join(stage,'unpacked');
  const abort = new AbortController();
  const timeout = setTimeout(()=>abort.abort(),90000);
  let phase = 'download';
  try {
    const response = await fetch(CLIENT.url,{signal:abort.signal});
    if (!response.ok || !response.body) throw new Error('Download failed');
    let bytes=0;
    const hash=crypto.createHash('sha256');
    const limit = new Transform({transform(chunk,_encoding,done){
      bytes += chunk.length;
      if (bytes > 64 * 1024 * 1024) return done(new Error('Download limit exceeded'));
      hash.update(chunk); done(null,chunk);
    }});
    await pipeline(Readable.fromWeb(response.body),limit,fs.createWriteStream(archive,{flags:'wx'}));
    phase = 'checksum';
    if (hash.digest('hex') !== CLIENT.sha256) throw new Error('Checksum mismatch');
    clearTimeout(timeout);
    phase = 'extract';
    const {extract} = await import('@electron-internal/extract-zip');
    await extract(archive,{dir:extracted});
    phase = 'install';
    const executable = path.join(extracted,'bw.exe');
    const stat = await fsp.lstat(executable);
    if (!stat.isFile() || stat.isSymbolicLink() || stat.size > 192 * 1024 * 1024 || stat.size < 2) throw new Error('Invalid executable');
    const data=await fsp.readFile(executable);
    if (data[0] !== 0x4d || data[1] !== 0x5a) throw new Error('Invalid executable');
    const sha256=crypto.createHash('sha256').update(data).digest('hex');
    const target=clientDestination(root);
    await fsp.copyFile(executable,target);
    await fsp.writeFile(path.join(base,'client.json'),JSON.stringify({version:CLIENT.version,sha256,downloadSha256:CLIENT.sha256,sourceCommit:CLIENT.sourceCommit},null,2));
    return {path:target,sha256,version:CLIENT.version};
  } catch (error) {
    const failure = new Error('Der geprüfte Bitwarden-Client konnte nicht installiert werden. Bitte erneut versuchen.');
    failure.diagnostics = {phase,code:String(error.code || error.name || 'INSTALL_FAILED'),detail:String(error.message || '').slice(0,1000)};
    throw failure;
  } finally {
    clearTimeout(timeout);
    abort.abort();
    await fsp.rm(stage,{recursive:true,force:true}).catch(()=>{});
  }
}
module.exports={CLIENT,installClient,clientDestination,readClientHash};
