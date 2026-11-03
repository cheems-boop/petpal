package pet.runtime;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

/**
 * 唯一入口 Activity：无界面，拉起 Host 服务后立刻 finish。
 *
 * 支持通过 Intent extra 传一次性指令（
 * {@code am start -n pet.runtime/.Main --es ctl pause}）。
 *
 * v1.6 修正：v1.5 的 smali 里 `ctl` 分支的判空写反了（传入非 null 就直接 return），
 * 导致这条链路从来没生效过。
 */
public class Main extends Activity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        startService(new Intent(this, Host.class));

        Intent it = getIntent();
        if (it != null) {
            String c = it.getStringExtra("ctl");
            if (c != null) Host.ctl(c);
        }
        finish();
    }
}
