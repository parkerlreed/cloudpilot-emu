// Requires a locally prepared E2 session with Novii at its welcome screen.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const {openE2} = require('../tools/e2-session.cjs');
(async () => {
    const [core, image, captureFile] = process.argv.slice(2);
    if (!image || !captureFile) throw Error('Usage: novii-e2-roundtrip.cjs CORE IMAGE CAPTURE_JSON');
    const {M, cp, bridge, addr, run, tap} = await openE2(core, {image});
    const transmissions = [];
    const collect = () => {
        while (cp.GetIrTransmitSize()) {
            const start = addr(cp.GetIrTransmitData()) >>> 2;
            transmissions.push({frequency: cp.GetIrTransmitFrequency(),
                timings: Array.from(M.HEAPU32.slice(start, start + cp.GetIrTransmitSize()))});
            cp.PopIrTransmit();
        }
    };
    run(1); tap(260,108); tap(30,10); tap(36,120); collect();
    assert(transmissions.length, 'Supplied code must transmit through the OS 5 bridge');
    tap(20,36); tap(40,70); tap(184,266); tap(80,296); tap(86,118);
    assert(cp.IsIrLearnPending());
    const timings = JSON.parse(fs.readFileSync(captureFile)).pattern_us;
    const p = bridge.Malloc(timings.length * 4);
    M.HEAPU32.set(timings, addr(p) >>> 2);
    assert(cp.CompleteIrLearn(p, timings.length)); bridge.Free(p); run();
    assert(!cp.IsIrLearnPending());
    tap(20,36); tap(270,296); tap(86,118); collect();
    const learned = transmissions.at(-1);
    assert.deepEqual(learned.timings, timings);
    assert(learned.frequency >= 37500 && learned.frequency <= 39000);
    tap(20,36); tap(40,70); tap(184,266); tap(80,296); tap(86,118);
    assert(cp.IsIrLearnPending()); cp.CancelIrLearn(); run(); assert(!cp.IsIrLearnPending());
    console.log(`E2 Novii built-in transmit, native capture, exact ${timings.length}-duration replay (${learned.frequency} Hz), and cancellation passed.`);
})().catch(error => {console.error(error); process.exitCode = 1;});
