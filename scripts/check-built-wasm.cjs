// Exercise the shipped WASM without a browser, network, credentials or private data.
const path = require('node:path');
const mod = require(path.resolve(__dirname, '../frontend/build/wasm/wasmWrapper.js'));
const timer = setTimeout(() => { console.error('WASM initialization timed out'); process.exit(1); }, 10000);
function check() {
  clearTimeout(timer);
  if (typeof mod._solve_wasm !== 'function' || typeof mod._search_standard_wasm !== 'function' || !(mod.HEAPU8 instanceof Uint8Array)) process.exit(1);
  console.log('Built WASM initialized: solver/search exports and memory available.');
}
if (mod.calledRun) check(); else mod.onRuntimeInitialized = check;
