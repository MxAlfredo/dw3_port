package io.github.dw3port;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.os.Process;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.util.Log;

import org.libsdl.app.SDLActivity;

/**
 * SDL's activity plus the disc picker (T7b: the system's document picker, the image is read in place, never copied) and the one thing the game cannot do from C on Android: restart itself (the System tab's "Restart game").
 * Android has no execv of the app; a new process needs the system to start it. The game calls restartApp() from its exit
 * path (pc/shim/options_system.c) through JNI.
 */
public class DW3Activity extends SDLActivity {
    /** Intent extras with the harness scenario, absent on a normal launch: "args" (a string array: {@code --esa args a,b,c}) or
     *  "argv" (one string split at '|': {@code --es argv 'a|b|c'}; for values with commas, like the disc's file name). */
    public static final String EXTRA_ARGS = "args";
    public static final String EXTRA_ARGV = "argv";

    /** Splits an "argv" string at '|' (empty parts kept: a skipped argument is an empty string); null or empty gives null (no arguments). */
    static String[] splitArgv(String text) {
        return text == null || text.isEmpty() ? null : text.split("\\|", -1);
    }

    /** The command line SDL_main gets after the program name: the extras when present, else none. */
    @Override
    protected String[] getArguments() {
        Intent intent = getIntent();
        String[] args = intent != null ? intent.getStringArrayExtra(EXTRA_ARGS) : null;
        if (args == null && intent != null) {
            args = splitArgv(intent.getStringExtra(EXTRA_ARGV));
        }
        return args != null ? args : super.getArguments();
    }

    /** Starts RestartActivity (its own process) and hands it this process's id; it ends this one and relaunches the app. */
    public static void restartApp() {
        Context activity = SDLActivity.getContext();
        if (activity == null) {
            return;
        }
        Intent intent = new Intent(activity, RestartActivity.class);
        intent.putExtra(RestartActivity.EXTRA_PID, Process.myPid());
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        activity.startActivity(intent);
    }

    // ---- disc picker (T7b) ----------------------------------------------------------------------------------------
    // The game thread (native) calls pickDisc(), polls pollDisc() until it answers, then openDisc() on the answer. The
    // activity result arrives on the UI thread; the two meet through these statics.

    private static final int REQUEST_DISC = 0x0D53;
    private static final String TAG = "dw3";
    private static final Object DISC_LOCK = new Object();
    private static String sDiscAnswer; // null: still waiting; "": cancelled; else the picked document's URI

    /** Opens the system picker at Downloads, on the UI thread. The answer comes through pollDisc(). */
    public static void pickDisc() {
        final Context ctx = SDLActivity.getContext();
        if (!(ctx instanceof Activity)) {
            synchronized (DISC_LOCK) {
                sDiscAnswer = "";
            }
            return;
        }
        synchronized (DISC_LOCK) {
            sDiscAnswer = null;
        }
        ((Activity) ctx).runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("*/*");
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
                intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI,
                        DocumentsContract.buildDocumentUri("com.android.externalstorage.documents", "primary:Download"));
                try {
                    ((Activity) ctx).startActivityForResult(intent, REQUEST_DISC);
                } catch (RuntimeException e) {
                    Log.w(TAG, "disc: the picker did not start: " + e);
                    synchronized (DISC_LOCK) {
                        sDiscAnswer = "";
                    }
                }
            }
        });
    }

    /** null while the picker is open; "" when the player cancelled; else the picked document's URI. Reading the answer consumes it. */
    public static String pollDisc() {
        synchronized (DISC_LOCK) {
            String answer = sDiscAnswer;
            if (answer != null) {
                sDiscAnswer = null;
            }
            return answer;
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != REQUEST_DISC) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        String answer = "";
        Uri uri = resultCode == RESULT_OK && data != null ? data.getData() : null;
        if (uri != null) {
            try {
                getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (RuntimeException e) {
                Log.w(TAG, "disc: no persistable permission for " + uri + ": " + e);
            }
            answer = uri.toString();
        }
        synchronized (DISC_LOCK) {
            sDiscAnswer = answer;
        }
    }

    /** The file descriptor of a picked document, detached (the caller owns it); -1 when it cannot be opened (moved, deleted, permission gone). */
    public static int openDisc(String uri) {
        Context ctx = SDLActivity.getContext();
        if (ctx == null || uri == null) {
            return -1;
        }
        try (ParcelFileDescriptor pfd = ctx.getContentResolver().openFileDescriptor(Uri.parse(uri), "r")) {
            return pfd != null ? pfd.detachFd() : -1;
        } catch (Exception e) {
            Log.w(TAG, "disc: cannot open " + uri + ": " + e);
            return -1;
        }
    }

    /** The display name of a document ("game.bin"), "" when unknown. */
    public static String discName(String uri) {
        Context ctx = SDLActivity.getContext();
        if (ctx == null || uri == null) {
            return "";
        }
        try (Cursor c = ctx.getContentResolver().query(Uri.parse(uri), new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst() && !c.isNull(0)) {
                return c.getString(0);
            }
        } catch (Exception e) {
            Log.w(TAG, "disc: no name for " + uri + ": " + e);
        }
        return "";
    }

    /** Gives back the persisted read permission of a document the player picked but the game did not accept. */
    public static void releaseDisc(String uri) {
        Context ctx = SDLActivity.getContext();
        if (ctx == null || uri == null) {
            return;
        }
        try {
            ctx.getContentResolver().releasePersistableUriPermission(Uri.parse(uri), Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (RuntimeException e) {
            Log.w(TAG, "disc: release failed for " + uri + ": " + e);
        }
    }
}
