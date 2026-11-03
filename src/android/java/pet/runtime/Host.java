package pet.runtime;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.drawable.Icon;
import android.os.IBinder;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;

/**
 * 悬浮窗宿主：建 SurfaceView → 交给 native 渲染，自己只管窗口位置、触摸分发与前台通知。
 *
 * 渲染全部在 libpethost.so（软光栅，1080×1080 超采样）；Java 侧刻意保持极薄，
 * 因为本项目的体验目标之一是「一个 100 行的胶水层」。
 *
 * 与 native 的约定见 docs/protocol.md 的 JNI 表。
 */
public class Host extends Service implements SurfaceHolder.Callback, View.OnTouchListener {

    private static final String TAG = "PetPal";

    private WindowManager wm;
    private SurfaceView sv;
    private WindowManager.LayoutParams lp;
    private SharedPreferences sp;
    private ScreenRx rx;

    /** 触摸起点（屏幕坐标）与按下时的窗口位置，用于拖拽换算 */
    private float downX, downY;
    private int startX, startY;

    // ---- native（libpethost.so）----
    private static native void nativeInit(String filesDir);
    private static native void nativeSurface(Surface s, int w, int h);
    private static native void nativeTouch(int action, float lx, float ly, int wx, int wy);
    private static native void nativeBubble(int[] px, int w, int h);
    private static native void nativeStop();
    public  static native void nativePause();
    public  static native void nativeResume();
    public  static native void nativeNextSkin();
    public  native void nativeAttach();          // 实例方法：native 侧存 global ref，回调 moveBy

    // ---- 对外指令入口（文件通道之外的备用路）----
    public static void ctl(String cmd) {
        if (cmd == null) return;
        Log.i(TAG, "ctl " + cmd);
        if ("skin".equalsIgnoreCase(cmd)) nativeNextSkin();
        else if ("pause".equalsIgnoreCase(cmd)) nativePause();
        else if ("resume".equalsIgnoreCase(cmd)) nativeResume();
    }

    @Override
    public void onCreate() {
        super.onCreate();
        Log.i(TAG, "on-create-enter");

        System.loadLibrary("pethost");
        nativeAttach();
        nativeInit(getFilesDir().getAbsolutePath());

        wm = (WindowManager) getSystemService(WINDOW_SERVICE);
        DisplayMetrics dm = getResources().getDisplayMetrics();
        final int scrW = dm.widthPixels, scrH = dm.heightPixels;
        final float d = dm.density;

        // 150dp 见方，最多占屏宽 3/5
        int size = (int) (150f * d);
        final int pad = (int) (8f * d);
        int maxSize = scrW * 3 / 5;
        if (size > maxSize) size = maxSize;

        int defX = scrW - size - pad;
        int defY = (int) (scrH * 0.62f);
        if (defY + size > scrH) defY = scrH - size - pad;

        sp = getSharedPreferences("pet", Context.MODE_PRIVATE);
        int x = sp.getInt("x", defX);
        int y = sp.getInt("y", defY);

        sv = new SurfaceView(this);
        SurfaceHolder holder = sv.getHolder();
        holder.addCallback(this);
        holder.setFormat(PixelFormat.TRANSLUCENT);
        sv.setOnTouchListener(this);

        lp = new WindowManager.LayoutParams();
        lp.type = WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY;
        lp.width = size;
        lp.height = size;
        lp.flags = WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                 | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL;
        lp.format = PixelFormat.TRANSLUCENT;
        lp.gravity = Gravity.TOP | Gravity.START;
        lp.x = x;
        lp.y = y;
        wm.addView(sv, lp);

        Log.i(TAG, "layout size=" + size + " x=" + x + " y=" + y + " density=" + d);

        rx = new ScreenRx();
        rx.host = this;
        IntentFilter f = new IntentFilter();
        f.addAction(Intent.ACTION_SCREEN_ON);
        f.addAction(Intent.ACTION_SCREEN_OFF);
        registerReceiver(rx, f);

        try {
            goForeground();
        } catch (Throwable t) {
            Log.e(TAG, t.toString());
        }
        Log.i(TAG, "on-create-inst");
    }

    /** 前台服务 + 一个现画的小圆脸图标（刻意不带美术资源，APK 才好小） */
    private void goForeground() {
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        NotificationChannel ch = new NotificationChannel("pet", "桌面宠物",
                NotificationManager.IMPORTANCE_LOW);
        nm.createNotificationChannel(ch);

        Bitmap bmp = Bitmap.createBitmap(96, 96, Bitmap.Config.ARGB_8888);
        Canvas cv = new Canvas(bmp);
        Paint p = new Paint();
        p.setAntiAlias(true);
        p.setColor(0xFFFFC396);              // 身体色
        cv.drawCircle(48f, 48f, 32f, p);
        p.setColor(0xFF000000);              // 两只眼
        cv.drawCircle(20f, 48f, 10f, p);
        cv.drawCircle(21f, 48f, 10f, p);

        Notification n = new Notification.Builder(this, "pet")
                .setSmallIcon(Icon.createWithBitmap(bmp))
                .setContentTitle("桌面宠物运行中")
                .setOngoing(true)
                .build();
        startForeground(0x3e9, n);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null) {
            String a = intent.getAction();
            if ("pet.pause".equals(a)) nativePause();
            else if ("pet.skin".equals(a)) nativeNextSkin();
        }
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }

    @Override
    public void onDestroy() {
        nativeStop();
        if (wm != null && sv != null) wm.removeView(sv);
        super.onDestroy();
    }

    // ---- 触摸：自己拖窗口，同时把像素/窗口坐标交给 native 做表情与边缘判定 ----
    @Override
    public boolean onTouch(View v, MotionEvent e) {
        int action = e.getActionMasked();
        float rawX = e.getRawX(), rawY = e.getRawY();
        float lx = e.getX(), ly = e.getY();

        int wx = 0, wy = 0;
        if (lp != null) { wx = lp.x; wy = lp.y; }
        nativeTouch(action, lx, ly, wx, wy);

        if (action == MotionEvent.ACTION_DOWN) {
            downX = rawX; downY = rawY;
            if (lp != null) { startX = lp.x; startY = lp.y; }
        } else if (action == MotionEvent.ACTION_MOVE) {
            if (lp != null) {
                lp.x = startX + (int) (rawX - downX);
                lp.y = startY + (int) (rawY - downY);
                if (wm != null && sv != null) wm.updateViewLayout(sv, lp);
            }
        } else if (action == MotionEvent.ACTION_UP) {
            if (sp != null && lp != null) {
                sp.edit().putInt("x", lp.x).putInt("y", lp.y).apply();
            }
            showThought();
        }
        return true;
    }

    /** native 让窗口跟随物理位移用的回调（保留：ColorOS 异步位置更新会丢，双保险） */
    public void moveBy(int x, int y) {
        if (lp == null) return;
        lp.x = x;
        lp.y = y;
        if (wm != null && sv != null) {
            wm.updateViewLayout(sv, lp);
            sv.requestLayout();
        }
    }

    /** 画一张文字位图丢给 native 当对话气泡（本机无法 dlopen libft2，文字只能走系统栈） */
    public void showThought() {
        final String text = "今天也开心～";
        float d = getResources().getDisplayMetrics().density;
        float ts = 40f * d;

        Paint p = new Paint();
        p.setAntiAlias(true);
        p.setTextSize(ts);
        p.setColor(0xFF222222);

        int tw = (int) p.measureText(text);
        if (tw > 950) {                       // 超宽就缩字号
            ts = ts * (956f / tw);
            p.setTextSize(ts);
            tw = (int) p.measureText(text);
        }
        int padX = (int) (10f * d);
        int w = tw + padX * 2;
        int h = (int) (1.25f * ts) + padX * 2;

        Bitmap bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        Canvas cv = new Canvas(bmp);
        cv.drawText(text, padX, padX + ts, p);

        int bw = bmp.getWidth(), bh = bmp.getHeight();
        int[] px = new int[bw * bh];
        bmp.getPixels(px, 0, bw, 0, 0, bw, bh);
        nativeBubble(px, bw, bh);
    }

    // ---- SurfaceHolder.Callback ----
    @Override
    public void surfaceCreated(SurfaceHolder holder) { }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        nativeSurface(holder.getSurface(), width, height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        nativeStop();
    }
}
