package pet.runtime;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * 屏幕开关 → 暂停/恢复渲染。
 * 灭屏 0fps 就是靠它：SCREEN_OFF 停渲染线程，SCREEN_ON 恢复。
 *
 * v1.6 修正：v1.5 的 smali 里判空写反了（`if (host != null) return;` 且
 * `if (action != null) return;`），两个守卫同时生效 ⇒ 永远什么都不做，
 * 所以 README 宣传的“灭屏 0fps”在 v1.5 实际不成立。
 */
public class ScreenRx extends BroadcastReceiver {

    static final String ACTION_PAUSE = "pet.runtime.action.PAUSE";
    static final String ACTION_RESUME = "pet.runtime.action.RESUME";

    public Host host;

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null) return;
        String a = intent.getAction();
        if (a == null) return;

        if (Intent.ACTION_SCREEN_OFF.equals(a) || ACTION_PAUSE.equals(a)) {
            Host.nativePause();
        } else if (Intent.ACTION_SCREEN_ON.equals(a) || ACTION_RESUME.equals(a)) {
            Host.nativeResume();
        }
    }
}
