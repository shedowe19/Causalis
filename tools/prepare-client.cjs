'use strict';
const path = require('node:path');
const {installClient} = require('../src/client-install.cjs');
installClient(path.resolve(__dirname,'../vendor')).then(result=>console.log('Verified official Bitwarden CLI prepared:',result.version,result.path)).catch(error=>{console.error(error.message);process.exitCode=1;});
