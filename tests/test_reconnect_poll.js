const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync('DeviceAuth.cpp', 'utf8');
const script = source.match(/<script>(async function poll\(\).*?)<\/script>/)[1];
let next, navigation, state = 'offline';
const context = vm.createContext({
  setTimeout: callback => { next = callback; },
  location: { replace: target => { navigation = target; } },
  fetch: async (url, options) => {
    assert.equal(url, '/result'); assert.equal(options.cache, 'no-store');
    if (state === 'offline') throw new Error('HTTPS temporarily stopped');
    return { ok: state !== 'error', headers: { get: () => state === 'pending' ? '1' : '0' } };
  }
});
(async () => {
  vm.runInContext(script, context);
  for (state of ['offline', 'pending', 'error', 'offline']) {
    const scheduled = next; next = undefined; await scheduled();
    assert.equal(navigation, undefined); assert.equal(typeof next, 'function');
  }
  state = 'finished'; const scheduled = next; next = undefined; await scheduled();
  assert.equal(navigation, '/result'); assert.equal(next, undefined);
  console.log('Reconnect page survives HTTPS suspension and navigates only after completion');
})().catch(error => { console.error(error); process.exitCode = 1; });
