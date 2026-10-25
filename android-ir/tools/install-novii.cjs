// Create a separate session with Novii and its English low-resolution assets.
const fs = require('node:fs');
const path = require('node:path');
async function main() {
    const [core, sourceImage, databases, destination] = process.argv.slice(2);
    if (!destination) throw Error('Usage: install-novii.cjs CORE PALM_IMAGE DATABASE_DIRECTORY OUTPUT_IMAGE');
    const M = await require(path.resolve(core, 'src/cloudpilot/cloudpilot_web.js'))({
        wasmBinary: fs.readFileSync(path.join(core, 'src/cloudpilot/cloudpilot_web.wasm')),
    });
    const cp = new M.Cloudpilot();
    const put = bytes => {
        const ptr = cp.Malloc(bytes.length);
        M.HEAPU8.set(bytes, M.getPointer(ptr));
        return ptr;
    };
    const bytes = fs.readFileSync(sourceImage), ptr = put(bytes), image = new M.SessionImage();
    if (!image.Deserialize(ptr, bytes.length)) throw Error('Invalid Palm session');
    if (image.GetDeviceId() !== 'PalmM130') throw Error('This adapter requires a Palm m130 session');
    // InitializeSession may grow the WASM heap; keep the ROM in its own allocation.
    const rom = Buffer.from(M.HEAPU8.slice(M.getPointer(image.GetRomImage()),
        M.getPointer(image.GetRomImage()) + image.GetRomImageSize()));
    const romPtr = put(rom);
    if (!cp.InitializeSession(romPtr, rom.length, image.GetDeviceId()) ||
        !cp.ImportMemoryImage(image.GetMemoryImage(), image.GetMemoryImageSize()) ||
        !cp.LoadState(image.GetSavestate(), image.GetSavestateSize())) throw Error('Palm restore failed');
    M.destroy(image); cp.Free(ptr);
    cp.SetClipboardIntegration(false); cp.SetNetworkRedirection(false); cp.SetIrBridgeEnabled(true);
    const run = frames => {
        for (let frame = 0; frame < frames; frame++) {
            if (cp.IsSuspended()) throw Error('Unexpected Palm suspension while preparing Novii');
            cp.RunEmulation(Math.round(cp.GetCyclesPerSecond() / 60));
        }
    };
    for (const file of fs.readdirSync(databases).filter(name => /\.(prc|pdb)$/i.test(name))) {
        const data = fs.readFileSync(path.join(databases, file)), p = put(data);
        const result = cp.InstallDb(p, data.length); cp.Free(p);
        if (result !== 1) throw Error(`Could not install ${file}: ${result}`);
        run(30);
    }
    if (!cp.LaunchAppByName('NoviiRemoteDeluxe')) throw Error('Could not launch Novii');
    run(120);
    if (!cp.SaveState()) throw Error('Could not save Novii session');
    const output = new M.SessionImage();
    output.SetDeviceId('PalmM130');
    output.SetRomImage(cp.GetRomPtr(), cp.GetRomSize());
    output.SetMemoryImage(cp.GetMemoryPtr(), cp.GetMemorySize());
    output.SetSavestate(cp.GetSavestatePtr(), cp.GetSavestateSize());
    if (!output.Serialize()) throw Error('Could not serialize Novii session');
    const start = M.getPointer(output.GetSerializedImage());
    fs.mkdirSync(path.dirname(destination), {recursive: true});
    fs.writeFileSync(destination, M.HEAPU8.slice(start, start + output.GetSerializedImageSize()));
    console.log('Created separate Novii session:', destination);
}
main().catch(error => { console.error(error.message); process.exitCode = 1; });
