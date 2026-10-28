const fs = require('node:fs');
const path = require('node:path');
async function openE2(core, options) {
    const M = await require(path.resolve(core, 'src/uarm/uarm_web.js'))({
        wasmBinary: fs.readFileSync(path.join(core, 'src/uarm/uarm_web.wasm')),
    });
    const bridge = new M.Bridge(), cp = new M.Uarm(), addr = p => M.getPointer(p);
    const put = bytes => { const p = bridge.Malloc(bytes.length); M.HEAPU8.set(bytes, addr(p)); return p; };
    const clone = (p, n) => put(M.HEAPU8.slice(addr(p), addr(p) + n));
    if (options.image) {
        const data = fs.readFileSync(options.image), p = put(data), image = new M.SessionFile5();
        if (!image.Deserialize(data.length, p)) throw Error('Invalid E2 session');
        cp.SetRamSize(image.GetRamSize()); cp.SetDisplayMode(image.GetDisplayMode());
        const nor = clone(image.GetNor(), image.GetNorSize());
        cp.SetNand(image.GetNandSize(), clone(image.GetNand(), image.GetNandSize()));
        cp.SetMemory(image.GetMemorySize(), clone(image.GetMemory(), image.GetMemorySize()));
        cp.SetSavestate(image.GetSavestateSize(), clone(image.GetSavestate(), image.GetSavestateSize()));
        if (!cp.Launch(image.GetNorSize(), nor)) throw Error('E2 restore failed');
        M.destroy(image); bridge.Free(p);
    } else if (options.pvRom) {
        const zlib = require('node:zlib');
        const input = fs.readFileSync(options.pvRom);
        const rom = input[0] === 0x1f && input[1] === 0x8b ? zlib.gunzipSync(input) : input;
        cp.SetDisplayMode(6); // rePalm uses a 320x480 display with built-in DIA.
        if (!cp.Launch(rom.length, put(rom)) || cp.GetDeviceType() !== 2)
            throw Error('Expected a rePalm paravirtualized ROM');
    } else {
        const nor = fs.readFileSync(options.nor), nand = fs.readFileSync(options.nand);
        cp.SetNand(nand.length, put(nand));
        if (!cp.Launch(nor.length, put(nor))) throw Error('E2 boot failed');
    }
    cp.SetIrBridgeEnabled(true); cp.SetPcmOutputEnabled(false);
    const run = (frames = 30) => {
        for (let i = 0; i < frames; i++) {
            if (cp.IsIrLearnPending()) break;
            cp.RunCycles(2000000);
        }
    };
    const tap = (x, y) => { cp.PenDown(x, y); run(3); cp.PenUp(); run(); };
    const save = destination => {
        if (cp.IsIrLearnPending()) throw Error('Finish or cancel learning before saving');
        cp.Save(); const image = new M.SessionFile5();
        image.SetDeviceType(cp.GetDeviceType()); image.SetDisplayMode(cp.GetDeviceType() === 2 ? 6 : 5); image.SetRamSize(cp.GetRamSize());
        image.SetNor(cp.GetRomDataSize(), cp.GetRomData()); image.SetNand(cp.GetNandDataSize(), cp.GetNandData());
        image.SetMemory(cp.GetMemoryDataSize(), cp.GetMemoryData()); image.SetSavestate(cp.GetSavestateSize(), cp.GetSavestateData());
        if (!image.Serialize()) throw Error('Could not serialize E2 session');
        fs.mkdirSync(path.dirname(destination), {recursive: true});
        fs.writeFileSync(destination, M.HEAPU8.slice(addr(image.GetSerializedSession()),
            addr(image.GetSerializedSession()) + image.GetSerializedSessionSize()));
        M.destroy(image);
    };
    return {M, cp, bridge, addr, put, run, tap, save};
}
module.exports = {openE2};
