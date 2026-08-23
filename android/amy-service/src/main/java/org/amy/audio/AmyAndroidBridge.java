package org.amy.audio;

import android.content.Context;
import android.util.Log;

/**
 * Framework-neutral, reflection-friendly facade for the AMY Android service.
 *
 * <p>The full {@link AmyClient} instance API remains available to ordinary
 * Android applications. This facade is intentionally limited to primitive
 * return values, strings and {@link Context}, which makes it convenient for
 * frameworks such as Godot that call Java through a reflection/JNI bridge.</p>
 *
 * <p>The facade contains no synthesizer and no PCM path. AMY runs exclusively
 * in {@link AmyService}'s separate {@code :amy} process; this class owns only
 * one persistent control-socket client in the caller process.</p>
 */
public final class AmyAndroidBridge {
    private static final String TAG = "AmyAndroidBridge";
    private static final int EINVAL = 22;
    private static final int EJAVA = 1000;
    private static final int ELINKAGE = 1001;

    private static AmyClient client;
    private static String lastErrorText = "";

    private AmyAndroidBridge() {}

    private static Context applicationContext(Context context) {
        if (context == null) return null;
        Context app = context.getApplicationContext();
        return app != null ? app : context;
    }

    private static int fail(String operation, Throwable error, int code) {
        lastErrorText = operation + ": " + error.getClass().getName()
                + (error.getMessage() == null ? "" : ": " + error.getMessage());
        Log.e(TAG, lastErrorText, error);
        return -code;
    }

    /** Request startup of the private {@code :amy} service process. */
    public static synchronized int start(Context context) {
        Context app = applicationContext(context);
        if (app == null) {
            lastErrorText = "start: null Context";
            return -EINVAL;
        }
        try {
            AmyService.start(app);
            lastErrorText = "";
            Log.i(TAG, "AMY service start requested");
            return 0;
        } catch (LinkageError error) {
            return fail("start", error, ELINKAGE);
        } catch (RuntimeException error) {
            return fail("start", error, EJAVA);
        }
    }

    /** Attempt one immediate connection to filesDir/amy.sock. */
    public static synchronized int connect(Context context) {
        Context app = applicationContext(context);
        if (app == null) {
            lastErrorText = "connect: null Context";
            return -EINVAL;
        }
        try {
            if (client == null) client = new AmyClient();
            int result = client.connect(app);
            if (result == 0) {
                lastErrorText = "";
                Log.i(TAG, "AMY control socket connected");
            } else {
                lastErrorText = "connect returned " + result;
            }
            return result;
        } catch (LinkageError error) {
            return fail("connect", error, ELINKAGE);
        } catch (RuntimeException error) {
            return fail("connect", error, EJAVA);
        }
    }

    /** Convenience retrying connect for non-UI worker threads. */
    public static synchronized int connectWithRetry(Context context, int timeoutMs) {
        Context app = applicationContext(context);
        if (app == null) {
            lastErrorText = "connectWithRetry: null Context";
            return -EINVAL;
        }
        try {
            if (client == null) client = new AmyClient();
            int result = client.connectWithRetry(app, timeoutMs);
            if (result == 0) {
                lastErrorText = "";
                Log.i(TAG, "AMY control socket connected");
            } else {
                lastErrorText = "connectWithRetry returned " + result;
            }
            return result;
        } catch (LinkageError error) {
            return fail("connectWithRetry", error, ELINKAGE);
        } catch (RuntimeException error) {
            return fail("connectWithRetry", error, EJAVA);
        }
    }

    /** Send one AMY wire request as one SOCK_SEQPACKET packet. */
    public static synchronized int sendWire(String wire) {
        if (client == null) {
            lastErrorText = "sendWire: client not connected";
            return -107;
        }
        try {
            int result = client.sendWire(wire);
            if (result == 0) {
                lastErrorText = "";
            } else {
                lastErrorText = "sendWire returned " + result;
            }
            return result;
        } catch (LinkageError error) {
            return fail("sendWire", error, ELINKAGE);
        } catch (RuntimeException error) {
            return fail("sendWire", error, EJAVA);
        }
    }

    public static synchronized boolean isConnected() {
        return client != null && client.isConnected();
    }

    /** Close only the caller-process control socket. */
    public static synchronized void close() {
        if (client != null) {
            client.close();
            client = null;
        }
    }

    /** Close the client and request shutdown of the private AMY service. */
    public static synchronized int stop(Context context) {
        close();
        Context app = applicationContext(context);
        if (app == null) {
            lastErrorText = "stop: null Context";
            return -EINVAL;
        }
        try {
            AmyService.stop(app);
            lastErrorText = "";
            return 0;
        } catch (RuntimeException error) {
            return fail("stop", error, EJAVA);
        }
    }

    /** Human-readable detail for the last bridge failure, if any. */
    public static synchronized String getLastErrorText() {
        return lastErrorText;
    }
}
