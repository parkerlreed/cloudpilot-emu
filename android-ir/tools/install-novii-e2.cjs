const fs = require('node:fs');
const path = require('node:path');
const {openE2} = require('./e2-session.cjs');
(async () => {
    const [core, nor, nand, databases, destination] = process.argv.slice(2);
    if (!destination) throw Error('Usage: install-novii-e2.cjs CORE NOR NAND DATABASES OUTPUT');
    const {cp, put, bridge, run, save} = await openE2(core, {nor, nand});
    for (let frame = 0; frame < 3000 && !cp.IsUiInitialized(); frame++) run(1);
    if (!cp.IsUiInitialized()) throw Error('E2 did not initialize its UI');
    run(200); // Allow NVFS and the Palm startup application to finish.
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
    console.log('Created Novii Tungsten E2 session:', destination);
})().catch(error => {console.error(error); process.exitCode = 1;});
