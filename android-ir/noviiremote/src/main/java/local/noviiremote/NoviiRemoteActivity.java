// SPDX-License-Identifier: GPL-3.0-or-later
package local.noviiremote;

/** Independent NoviiRemote application using the shared Palm/USB runtime. */
public final class NoviiRemoteActivity extends local.palmir.NativeActivity {
    @Override protected String remoteLibrary() { return "novii"; }
    @Override protected String sessionFile() { return "session-e2.img"; }
    @Override protected String logTag() { return "NoviiRemote"; }
}
