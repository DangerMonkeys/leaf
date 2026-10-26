// Exercise the shipped embedded JavaScript without an ESP32 or a network service.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../../src/vario/comms/webserver.cpp'), 'utf8');
const html = source.match(/R"leafapp\(([\s\S]*?)\)leafapp"/)[1];
const javascript = html.match(/<script>([\s\S]*?)<\/script>/)[1];
new vm.Script(javascript); // Includes the deletion handler and the rest of the app.
const functions = javascript.split('\n').filter(line =>
  line.startsWith('async function fetchLogbook(') ||
  line.startsWith('async function loadLogbook(')).join('\n');

async function run() {
  const elements = { logCount: {}, logLatest: {} };
  const requests = [];
  const responses = [];
  let yields = 0;
  const context = vm.createContext({
    $: id => elements[id],
    useUnits: () => {},
    logDateTime: time => time,
    unitPrefs: {},
    setTimeout: resolve => { ++yields; resolve(); },
    fetch: async url => {
      requests.push(url);
      assert(responses.length, 'unexpected extra request');
      return responses.shift();
    }
  });
  vm.runInContext(functions, context);
  const response = (status, data = {}) => ({ status, ok: status === 200, json: async () => data });

  responses.push(response(202), response(202), response(200, { count: 2 }));
  await context.loadLogbook();
  assert.deepEqual(requests, ['/api/logbook?repair=1', '/api/logbook', '/api/logbook']);
  assert.equal(yields, 2);
  assert.equal(elements.logCount.textContent, 2);

  requests.length = 0;
  responses.push(response(200, { count: 1 }));
  await context.loadLogbook(false); // Partial deletion refresh must not restart repair.
  assert.deepEqual(requests, ['/api/logbook']);
  assert.equal(elements.logCount.textContent, 1);

  responses.push(response(503));
  await context.loadLogbook();
  assert.equal(elements.logLatest.textContent, 'Unavailable');

  responses.push(response(200, { count: 0 }));
  await context.loadLogbook(false);
  assert.equal(elements.logLatest.textContent, 'No flights');

  requests.length = 0;
  responses.push(response(202), response(200));
  await context.fetchLogbook('/api/logbook/entry?path=example.json', true);
  assert.deepEqual(requests, [
    '/api/logbook/entry?path=example.json&repair=1', '/api/logbook/entry?path=example.json'
  ]);
  console.log('Embedded logbook JavaScript: syntax, repair polling, deletion refresh and unavailable state passed.');
}
run().catch(error => { console.error(error); process.exitCode = 1; });
