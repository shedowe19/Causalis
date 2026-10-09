'use strict';
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const root = path.resolve(__dirname,'..');
let count=0;
function walk(directory) {
  for (const entry of fs.readdirSync(directory,{withFileTypes:true})) {
    if (entry.name === 'node_modules' || entry.name === 'vendor' || entry.name === 'dist') continue;
    const file=path.join(directory,entry.name);
    if (entry.isDirectory()) walk(file);
    else if (/\.(cjs|js)$/.test(entry.name)) {execFileSync(process.execPath,['--check',file],{stdio:'inherit'});++count;}
  }
}
walk(root);
console.log(`${count} JavaScript files passed syntax checks.`);
