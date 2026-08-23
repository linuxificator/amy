package org.amy.audio;

import android.content.Context;

import java.io.File;

/** Tiny process-local client for the private AMY SOCK_SEQPACKET control socket. */
public final class AmyClient {
    private static final int EINVAL = 22;
    private static final int ENOTCONN = 107;
    private static int nativeFd = -1;

    static {
        System.loadLibrary("amy_android_client");
    }

    private AmyClient() {}

    private static String socketPath(Context context) {
        if (context == null) return "";
        return new File(context.getFilesDir(), AmyService.DEFAULT_SOCKET_NAME)
                .getAbsolutePath();
    }

    /** Attempt one connection to filesDir/amy.sock. Returns 0 or negative errno. */
    public static synchronized int connect(Context context) {
        if (context == null) return -EINVAL;
        closeLocked();
        int fd = nativeConnect(socketPath(context));
        if (fd < 0) return fd;
        nativeFd = fd;
        return 0;
    }

    /** Send one AMY wire request as one packet. Returns 0 or negative errno. */
    public static synchronized int sendWire(String wire) {
        if (nativeFd < 0) return -ENOTCONN;
        if (wire == null || wire.isEmpty()) return -EINVAL;
        int result = nativeSend(nativeFd, wire);
        if (result < 0) closeLocked();
        return result;
    }

    public static synchronized boolean isConnected() {
        return nativeFd >= 0;
    }

    public static synchronized void close() {
        closeLocked();
    }

    private static void closeLocked() {
        if (nativeFd >= 0) {
            nativeClose(nativeFd);
            nativeFd = -1;
        }
    }

    private static native int nativeConnect(String socketPath);
    private static native int nativeSend(int fd, String wire);
    private static native void nativeClose(int fd);
}
