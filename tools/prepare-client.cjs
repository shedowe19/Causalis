'use strict';
const path = require('node:path');
const fs = require('node:fs');
const {installClient} = require('../src/client-install.cjs');
installClient(path.resolve(__dirname,'../vendor')).then(result=>console.log('Verified official Bitwarden CLI prepared:',result.version,result.path)).catch(error=>{
  const directory=path.resolve(__dirname,'../test-results');
  fs.mkdirSync(directory,{recursive:true});
  fs.writeFileSync(path.join(directory,'client-prepare.json'),JSON.stringify({passed:false,diagnostics:error.diagnostics || null},null,2));
  console.error(error.message);
  console.error(JSON.stringify(error.diagnostics || {}));
  process.exitCode=1;
});
