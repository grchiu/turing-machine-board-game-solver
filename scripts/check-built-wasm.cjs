// Exercise the shipped WASM without a browser, network, credentials or private data.
const path = require('node:path');
const mod = require(path.resolve(__dirname, '../frontend/build/wasm/wasmWrapper.js'));
const timer = setTimeout(() => { console.error('WASM initialization timed out'); process.exit(1); }, 10000);
function check() {
  clearTimeout(timer);
  if (typeof mod._solve_wasm !== 'function' || typeof mod._search_standard_wasm !== 'function' || !(mod.HEAPU8 instanceof Uint8Array)) process.exit(1);
  // Existing native fixture #A46 NXF: the known solution must remain possible.
  mod.HEAPU8.set([0, 4, 2, 5, 8, 12, 0], 0);
  mod._solve_wasm(0, 16);
  const count = mod.HEAPU8[16];
  const codes = [];
  for (let i = 0; i < count; i++) codes.push([...mod.HEAPU8.subarray(17 + i * 3, 20 + i * 3)].join(''));
  if (!count || count > 125 || !codes.includes('515')) throw new Error('WASM fixture solution missing');
  console.log('Built WASM initialized and solved the native fixture with its known solution present.');
}
if (mod.calledRun) check(); else mod.onRuntimeInitialized = check;
