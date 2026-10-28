// SPDX-License-Identifier: GPL-3.0-or-later
package local.noviiremote.pv;

/** Separate rePalm paravirtualized build, with its own persistent Palm session. */
public final class NoviiRemotePvActivity extends local.palmir.NativeActivity {
    @Override protected String remoteLibrary() { return "novii_pv"; }
    @Override protected String sessionFile() { return "session-pv-ir.img"; }
    @Override protected String logTag() { return "NoviiRemotePV"; }
}
