const fs = require('node:fs');
const path = require('node:path');
const {openE2} = require('./e2-session.cjs');
(async () => {
    const args = process.argv.slice(2), pv = args[0] === '--pv';
    if (pv) args.shift();
    const [core, nor, nand, databases, destination] = pv
        ? [args[0], args[1], null, args[2], args[3]] : args;
    if (!destination) throw Error('Usage: install-novii-e2.cjs CORE NOR NAND DATABASES OUTPUT\n   or: install-novii-e2.cjs --pv CORE PV_ROM DATABASES OUTPUT');
    const {cp, put, bridge, run, tap, save} = await openE2(core, pv ? {pvRom: nor} : {nor, nand});
    for (let frame = 0; frame < 3000 && !cp.IsUiInitialized(); frame++) run(1);
    if (!cp.IsUiInitialized()) throw Error('Palm OS did not initialize its UI');
    run(200); // Allow NVFS and the Palm startup application to finish.
    if (pv) {
        run(3000); // rePalm's setup starts after the UI-initialized notification.
        // This ROM starts in digitizer setup with its DIA open, leaving
        // a 320x320 application area. Calibrate there, then exit setup.
        tap(293,293); tap(25,25); tap(160,160); tap(35,306);
        // Preferences uses a native event loop. The installer needs the
        // launcher's 68k EvtGetEvent boundary for safe injected calls.
        tap(20,340);
        run(100);
    }
    for (const file of fs.readdirSync(databases).filter(name => /\.(prc|pdb)$/i.test(name))) {
        const data = fs.readFileSync(path.join(databases, file)), ptr = put(data);
        let result;
        for (let attempt = 0; attempt < 10; attempt++) {
            result = cp.InstallDatabase(data.length, ptr);
            if (result !== -7) break; // Guest is temporarily busy.
            run(50);
        }
        bridge.Free(ptr);
        if (result !== 0) throw Error(`Could not install ${file}: ${result}`);
        run(30);
    }
    if (!cp.LaunchAppByName('NoviiRemoteDeluxe')) throw Error('Could not launch Novii');
    run(100); save(destination);
    console.log(`Created Novii ${pv ? 'rePalm PV' : 'Tungsten E2'} session:`, destination);
})().catch(error => {console.error(error); process.exitCode = 1;});
