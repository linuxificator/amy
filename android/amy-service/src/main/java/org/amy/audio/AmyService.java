package org.amy.audio;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.IBinder;
import android.util.Log;

import java.io.File;
import java.io.IOException;

/**
 * Unexported same-UID service hosting native AMY + Oboe in a separate process.
 *
 * Musical control never crosses JNI. The host opens the private pathname Unix
 * SOCK_SEQPACKET socket and sends one AMY wire message per packet. JNI is only
 * used to start and stop the native engine with the validated socket pathname.
 */
public final class AmyService extends Service {
    private static final String TAG = "AmyService";

    public static final String EXTRA_SOCKET_PATH = "org.amy.audio.extra.SOCKET_PATH";
    public static final String DEFAULT_SOCKET_NAME = "amy.sock";

    static {
        System.loadLibrary("amy_android");
    }

    private boolean running;
    private String runningSocketPath;

    private static native int nativeStart(String socketPath);
    private static native void nativeStop();

    /** Start the private AMY process using filesDir/amy.sock. */
    public static void start(Context context) {
        File socket = new File(context.getFilesDir(), DEFAULT_SOCKET_NAME);
        Intent intent = new Intent(context, AmyService.class);
        intent.putExtra(EXTRA_SOCKET_PATH, socket.getAbsolutePath());
        context.startService(intent);
    }

    /** Stop the private AMY process. */
    public static void stop(Context context) {
        context.stopService(new Intent(context, AmyService.class));
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null) {
            stopSelf(startId);
            return START_NOT_STICKY;
        }

        String requested = intent.getStringExtra(EXTRA_SOCKET_PATH);
        if (requested == null) {
            requested = new File(getFilesDir(), DEFAULT_SOCKET_NAME).getAbsolutePath();
        }

        final String socketPath;
        try {
            socketPath = validatePrivateSocketPath(requested);
        } catch (IOException | SecurityException ex) {
            Log.e(TAG, "Refusing AMY socket path", ex);
            stopSelf(startId);
            return START_NOT_STICKY;
        }

        // Starting the same service again is normal Android lifecycle behavior.
        // Do not tear down an active audio engine and disconnect its socket
        // client merely because another equivalent startService() arrived.
        if (running && socketPath.equals(runningSocketPath)) {
            Log.i(TAG, "AMY already running on private socket " + socketPath);
            return START_NOT_STICKY;
        }

        if (running) {
            nativeStop();
            running = false;
            runningSocketPath = null;
        }

        int result = nativeStart(socketPath);
        if (result != 0) {
            Log.e(TAG, "nativeStart failed: " + result);
            stopSelf(startId);
            return START_NOT_STICKY;
        }

        running = true;
        runningSocketPath = socketPath;
        Log.i(TAG, "AMY listening on private socket " + socketPath);
        return START_NOT_STICKY;
    }

    private String validatePrivateSocketPath(String requested) throws IOException {
        File files = getFilesDir().getCanonicalFile();
        File socket = new File(requested).getCanonicalFile();
        File parent = socket.getParentFile();
        if (parent == null || !parent.equals(files)) {
            throw new SecurityException("AMY socket must be directly inside app filesDir");
        }
        if (!DEFAULT_SOCKET_NAME.equals(socket.getName())) {
            throw new SecurityException("AMY socket filename must be " + DEFAULT_SOCKET_NAME);
        }
        return socket.getAbsolutePath();
    }

    @Override
    public void onDestroy() {
        if (running) {
            nativeStop();
            running = false;
            runningSocketPath = null;
        }
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
