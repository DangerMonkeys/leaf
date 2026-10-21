// Exercise the web app's embedded developer script without a browser dependency.
// Run from the repository root: node sim/tests/developer_card.cjs
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const source = fs.readFileSync('src/vario/comms/webserver.cpp', 'utf8');
const script = source.match(/R"leafdev\(<script>([\s\S]*?)<\/script>\)leafdev"/)[1];
const elements = new Map();
function element() {
  return {
    style: {}, checked: false, children: [],
    setAttribute(key, value) { this[key] = value; if (key === 'id') elements.set(value, this); },
    appendChild(child) { this.children.push(child); },
  };
}
elements.set('mainView', element());
let session = { udp_sensor_injection: false };
let reject = false;
const posts = [];
vm.runInNewContext(script, {
  document: { createElement: element, getElementById: id => elements.get(id) },
  fetch: async (url, options) => {
    if (options?.method === 'POST') {
      const body = JSON.parse(options.body);
      posts.push(body);
      if (!reject) session = body;
    }
    return { ok: !reject, json: async () => url === '/api/debug/session' ? session : {} };
  },
});
const flush = () => new Promise(resolve => setImmediate(resolve));

(async () => {
  await flush();
  const injection = elements.get('devUdpInjection');
  assert(injection && elements.has('developerCard'));
  assert.equal(injection.checked, false);
  injection.checked = true;
  await injection.onchange();
  await flush();
  assert.equal(posts.at(-1).udp_sensor_injection, true);
  assert.equal(injection.checked, true);
  injection.checked = false;
  await injection.onchange();
  await flush();
  assert.equal(posts.at(-1).udp_sensor_injection, false);
  assert.equal(injection.checked, false);

  // A stale developer page must show failure when the device rejects its POST.
  reject = true;
  injection.checked = true;
  await injection.onchange();
  assert.equal(elements.get('devMsg').textContent, 'Unable to save developer settings.');
  console.log('Developer card injection load, toggle and rejected-save checks passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
