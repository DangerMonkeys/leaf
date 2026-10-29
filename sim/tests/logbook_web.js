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
  line.startsWith('async function loadLogbook(') ||
  line.startsWith('function clearLogCard(')).join('\n') + '\n' +
  javascript.match(/async function loadLogEntry\(path\)\{[^\n]*/)[0];

async function run() {
  const elements = Object.fromEntries([
    'logCount', 'logLatest', 'logPage', 'logPrev', 'logNext', 'flightDay', 'flightDate',
    'flightTime', 'flightDuration', 'flightPilot', 'flightGlider', 'climbMax', 'sinkMax',
    'flightMetrics', 'trackInfo', 'deleteLog', 'logDetailMsg'
  ].map(id => [id, { style: {} }]));
  const requests = [];
  const responses = [];
  let yields = 0;
  const context = vm.createContext({
    $: id => elements[id],
    useUnits: () => {},
    msg: (id, text) => { elements[id].textContent = text; },
    resetDelete: () => {},
    renderAlt: () => {},
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
  assert.deepEqual(requests, ['/api/logbook', '/api/logbook', '/api/logbook']);
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
    '/api/logbook/entry?path=example.json', '/api/logbook/entry?path=example.json'
  ]);
  const navigation = {
    path: '/logbook/example.json', filename: 'example.json', position: 2, total: 3,
    previous_path: '/logbook/older.json', next_path: '/logbook/newer.json'
  };
  responses.push(response(503, { ...navigation, error: 'temporarily_unavailable',
    detail: 'Could not read flight. Please try again.' }));
  await context.loadLogEntry(navigation.path);
  assert.equal(elements.trackInfo.textContent, 'Flight unavailable: example.json');
  assert.equal(elements.logDetailMsg.textContent, 'Could not read flight. Please try again.');
  assert.equal(elements.deleteLog.disabled, true);
  assert.equal(elements.logPage.textContent, '2/3');
  assert.equal(elements.logPrev.disabled, false);
  assert.equal(elements.logNext.disabled, false);

  responses.push(response(422, { ...navigation, error: 'repair_needed',
    detail: 'Flight needs repair. Run Repair Logs on the device.' }));
  await context.loadLogEntry(navigation.path);
  assert.equal(elements.trackInfo.textContent, 'Repair needed: example.json');
  assert.equal(elements.logDetailMsg.textContent, 'Flight needs repair. Run Repair Logs on the device.');
  assert.equal(elements.deleteLog.disabled, false);

  responses.push(response(503, { detail: 'Logbook unavailable.' }));
  await context.loadLogEntry(navigation.path);
  assert.equal(elements.trackInfo.textContent, 'Flight unavailable');
  assert.equal(elements.deleteLog.disabled, true);

  assert(!javascript.includes('repair=1'), 'Nominal UI must never request repair');
  console.log('Embedded logbook JavaScript: syntax, read-only scan polling, deletion refresh, temporary read failures and confirmed repair states passed.');
}
run().catch(error => { console.error(error); process.exitCode = 1; });
