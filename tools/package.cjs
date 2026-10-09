'use strict';
const path = require('node:path');
const fs = require('node:fs/promises');
async function main() {
  const {packager} = await import('@electron/packager');
  const {flipFuses,FuseVersion,FuseV1Options} = await import('@electron/fuses');
  const root=path.resolve(__dirname,'..');
  const paths = await packager({dir:root,name:'Causalis',platform:'win32',arch:'x64',electronVersion:'44.7.0',out:path.join(root,'dist'),overwrite:true,asar:false,prune:true,ignore:[/^\/\.github(?:\/|$)/,/^\/tests(?:\/|$)/,/^\/tools(?:\/|$)/,/^\/test-results(?:\/|$)/,/^\/vendor(?:\/|$)/,/^\/build(?:\/|$)/,/^\/dist(?:\/|$)/,/^\/package-lock\.json$/],appVersion:'0.3.0',win32metadata:{CompanyName:'Causalis',FileDescription:'Causalis Chromium Browser',ProductName:'Causalis'}});
  for (const directory of paths) {
    await flipFuses(path.join(directory,'Causalis.exe'),{version:FuseVersion.V1,[FuseV1Options.RunAsNode]:false,[FuseV1Options.EnableCookieEncryption]:true,[FuseV1Options.EnableNodeOptionsEnvironmentVariable]:false,[FuseV1Options.EnableNodeCliInspectArguments]:false,[FuseV1Options.GrantFileProtocolExtraPrivileges]:false});
    await fs.copyFile(path.join(root,'README.md'),path.join(directory,'README.md'));
    console.log('Packaged:',directory);
  }
}
main().catch(error=>{console.error(error.message);process.exitCode=1;});
