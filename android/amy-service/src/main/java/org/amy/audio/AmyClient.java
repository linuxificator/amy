package org.amy.audio;

import android.content.Context;

import java.io.File;

/**
 * Small persistent client for the private AMY Android wire socket.
 *
 * <p>This class contains no synthesizer and no audio path. The AMY engine stays
 * in {@link AmyService}'s separate {@code :amy} process; AmyClient only owns a
 * native AF_UNIX/SOCK_SEQPACKET descriptor in the calling process. Each
 * {@link #sendWire(String)} call is one packet and therefore one AMY wire
 * request.</p>
 *
 * <p>{@link #connect(Context)} performs one immediate connection attempt.
 * {@link #connectWithRetry(Context, int)} is a convenience for a worker thread;
 * do not use the retrying form on an Android or game-engine UI thread.</p>
 */
public final class AmyClient implements AutoCloseable {
    private static final int CONNECT_RETRY_MS = 50;
    private static final int EINVAL = 22;
    private static final int EINTR = 4;
    private static final int ENOTCONN = 107;

    private int nativeFd = -1;

    static {
        System.loadLibrary("amy_android_client");
    }

    public AmyClient() {}

    /** Return the service socket pathname for this application. */
    public static String socketPath(Context context) {
        if (context == null) return "";
        return new File(context.getFilesDir(), AmyService.DEFAULT_SOCKET_NAME)
                .getAbsolutePath();
    }

    /**
     * Attempt one connection. Returns 0 on success or a negative errno value.
     * The service publishes amy.sock only after its Oboe callback is running.
     */
    public synchronized int connect(Context context) {
        if (context == null) return -EINVAL;
        closeLocked();
        int fd = nativeConnect(socketPath(context));
        if (fd < 0) return fd;
        nativeFd = fd;
        return 0;
    }

    /**
     * Retry connect() every 50 ms until success or timeout. Intended for a
     * worker thread. timeoutMs <= 0 means one attempt only.
     */
    public int connectWithRetry(Context context, int timeoutMs) {
        final long deadline = System.nanoTime()
                + Math.max(timeoutMs, 0) * 1_000_000L;
        int result;
        do {
            result = connect(context);
            if (result == 0 || timeoutMs <= 0) return result;
            if (System.nanoTime() >= deadline) return result;
            try {
                Thread.sleep(CONNECT_RETRY_MS);
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
                return -EINTR;
            }
        } while (true);
    }

    /**
     * Send exactly one AMY wire request packet. Returns 0 or negative errno.
     * The native socket is non-blocking; a caller may see -EAGAIN under
     * sustained backpressure instead of stalling its UI/control thread.
     */
    public synchronized int sendWire(String wire) {
        if (nativeFd < 0) return -ENOTCONN;
        if (wire == null || wire.isEmpty()) return -EINVAL;
        int result = nativeSend(nativeFd, wire);
        if (result < 0) closeLocked();
        return result;
    }

    public synchronized boolean isConnected() {
        return nativeFd >= 0;
    }

    @Override
    public synchronized void close() {
        closeLocked();
    }

    private void closeLocked() {
        if (nativeFd >= 0) {
            nativeClose(nativeFd);
            nativeFd = -1;
        }
    }

    private static native int nativeConnect(String socketPath);
    private static native int nativeSend(int fd, String wire);
    private static native void nativeClose(int fd);
}
