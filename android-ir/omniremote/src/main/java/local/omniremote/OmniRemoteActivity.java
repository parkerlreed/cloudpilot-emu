// SPDX-License-Identifier: GPL-3.0-or-later
package local.omniremote;

/** Independent OmniRemote application using the shared Palm/USB runtime. */
public final class OmniRemoteActivity extends local.palmir.NativeActivity {
    @Override protected String remoteLibrary() { return "omni"; }
    @Override protected String sessionFile() { return "session.img"; }
    @Override protected String logTag() { return "OmniRemote"; }
}
