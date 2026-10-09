'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const {UI_URL, workspaceId, text, trustedEvent, exactHttpsOrigin} = require('../src/policy.cjs');
test('vault IPC requires exact trusted contents and the main frame', () => {
  const mainFrame = {url:UI_URL, routingId:3, processId:42};
  const contents = {mainFrame,isDestroyed:()=>false};
  assert.equal(trustedEvent({sender:contents,senderFrame:{...mainFrame}},contents),true);
  for (const senderFrame of [null,{...mainFrame,url:'https://example.com/'},{...mainFrame,routingId:4},{...mainFrame,processId:43},{...mainFrame,url:UI_URL+'?attack=1'}]) {
    assert.equal(trustedEvent({sender:contents,senderFrame},contents),false);
  }
  assert.equal(trustedEvent({sender:{},senderFrame:mainFrame},contents),false);
  assert.equal(trustedEvent({sender:contents,senderFrame:mainFrame},{...contents,isDestroyed:()=>true}),false);
});
test('only complete HTTPS origins can become credential targets', () => {
  assert.equal(exactHttpsOrigin('https://EXAMPLE.COM:443/login?q=1'),'https://example.com');
  assert.equal(exactHttpsOrigin('https://example.com:8443/'),'https://example.com:8443');
  for (const value of ['http://example.com','https://user:secret@example.com','file:///a','javascript:alert(1)','data:text/html,x','bogus']) assert.equal(exactHttpsOrigin(value),null);
});
test('workspace and bounded input validation reject foreign keys and oversized values', () => {
  for (const value of ['personal','work','homelab','private']) assert.equal(workspaceId(value),value);
  for (const value of ['../personal','persist:personal',null,'incognito']) assert.throws(()=>workspaceId(value));
  assert.equal(text('pass$word\nwith spaces',100,true),'pass$word\nwith spaces');
  for (const value of [null,{},'x\0y','12345']) assert.throws(()=>text(value,4));
});
