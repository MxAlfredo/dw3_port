package io.github.dw3port;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;

/**
 * Lives in its own process (":restart", see the manifest) so it survives the game's process: it ends that process, then starts
 * the app's launcher activity again in a fresh one. A second start from the dying process itself would find the process still
 * alive and reuse it, with the game's native state still in memory.
 */
public class RestartActivity extends Activity {
    public static final String EXTRA_PID = "io.github.dw3port.pid";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        int pid = getIntent().getIntExtra(EXTRA_PID, -1);
        if (pid > 0 && pid != Process.myPid()) {
            Process.killProcess(pid);
        }
        Intent launch = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (launch != null) {
            launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            startActivity(launch);
        }
        finish();
        Runtime.getRuntime().exit(0);
    }
}
