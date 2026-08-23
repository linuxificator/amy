package org.amy.hello;

import android.app.Activity;
import android.content.Context;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.amy.audio.AmyClient;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private static final String TAG = "AmyHelloWorld";
    private static final ExecutorService EXECUTOR = Executors.newSingleThreadExecutor();

    private TextView status;
    private Button playButton;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setPadding(48, 48, 48, 48);

        TextView title = new TextView(this);
        title.setText("AMY Hello World");
        title.setTextSize(28);
        title.setGravity(Gravity.CENTER);
        root.addView(title, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        status = new TextView(this);
        status.setText("Waiting for AMY...");
        status.setTextSize(18);
        status.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams statusParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        statusParams.setMargins(0, 40, 0, 40);
        root.addView(status, statusParams);

        playButton = new Button(this);
        playButton.setText("Play C scale");
        playButton.setOnClickListener(v -> playScale());
        root.addView(playButton, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        setContentView(root);

        if (state == null) {
            playScale();
        } else {
            status.setText("AMY ready");
        }
    }

    private static int connectWithRetry(Context context, int timeoutMs) {
        long deadline = System.nanoTime() + timeoutMs * 1_000_000L;
        int result;
        do {
            result = AmyClient.connect(context);
            if (result == 0 || System.nanoTime() >= deadline) return result;
            try {
                Thread.sleep(50);
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
                return -4;
            }
        } while (true);
    }

    private static int sendLogged(String wire) {
        int result = AmyClient.sendWire(wire);
        if (result == 0) Log.i(TAG, "wire: " + wire);
        return result;
    }

    private static int playCScale(Context appContext) {
        int result = AmyClient.isConnected() ? 0 : connectWithRetry(appContext, 5000);
        if (result < 0) return result;

        result = sendLogged("v0w0V10.0Z");
        if (result < 0) return result;

        try {
            Thread.sleep(30);
            final int[] notes = {60, 62, 64, 65, 67, 69, 71, 72};
            for (int note : notes) {
                result = sendLogged("v0n" + note + "l1Z");
                if (result < 0) return result;
                Thread.sleep(350);

                result = sendLogged("v0l0Z");
                if (result < 0) return result;
                Thread.sleep(80);
            }
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            return -4;
        }

        Log.i(TAG, "C scale complete");
        return 0;
    }

    private void playScale() {
        playButton.setEnabled(false);
        status.setText("Playing C major scale...");
        Context appContext = getApplicationContext();

        EXECUTOR.execute(() -> {
            int rc = playCScale(appContext);
            runOnUiThread(() -> {
                if (isDestroyed()) return;
                if (rc == 0) {
                    status.setText("C scale complete");
                } else {
                    Log.e(TAG, "C scale failed: " + rc);
                    status.setText("AMY/socket error: " + rc);
                }
                playButton.setEnabled(true);
            });
        });
    }

    @Override
    protected void onDestroy() {
        if (isFinishing() && !isChangingConfigurations()) {
            AmyClient.close();
        }
        super.onDestroy();
    }
}
