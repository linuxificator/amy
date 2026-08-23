package org.amy.audio;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.util.Log;

import java.io.File;

/** Starts the private :amy service when the application process starts. */
public final class AmyAutoStartProvider extends ContentProvider {
    private static final String TAG = "AmyAutoStart";

    @Override
    public boolean onCreate() {
        Context context = getContext();
        if (context == null) return false;

        File socket = new File(context.getFilesDir(), AmyService.DEFAULT_SOCKET_NAME);
        Intent intent = new Intent(context, AmyService.class);
        intent.putExtra(AmyService.EXTRA_SOCKET_PATH, socket.getAbsolutePath());
        try {
            context.startService(intent);
            Log.i(TAG, "AMY service auto-start requested");
            return true;
        } catch (RuntimeException error) {
            Log.e(TAG, "Unable to auto-start AMY service", error);
            return false;
        }
    }

    @Override public Cursor query(Uri uri, String[] projection, String selection,
                                  String[] selectionArgs, String sortOrder) { return null; }
    @Override public String getType(Uri uri) { return null; }
    @Override public Uri insert(Uri uri, ContentValues values) { return null; }
    @Override public int delete(Uri uri, String selection, String[] selectionArgs) { return 0; }
    @Override public int update(Uri uri, ContentValues values, String selection,
                                String[] selectionArgs) { return 0; }
}
