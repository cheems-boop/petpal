// pethost.c -- desktop pet core v1.5 (pure C, zero runtime deps)
#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <pthread.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>
#include <stdint.h>
#include <dlfcn.h>
#include <fcntl.h>

#define TAG "PetPal"
static int gVerbose = 0;
#define LOGI(...) do { if (gVerbose) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__); } while (0)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

typedef struct { float r, g, b; } Col;
static Col C(float r, float g, float b) { Col c; c.r = r; c.g = g; c.b = b; return c; }
static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float mixf(float a, float b, float t) { return a + (b - a) * t; }
static Col mixc(Col a, Col b, float t) {
    Col o; t = clampf(t, 0.0f, 1.0f);
    o.r = mixf(a.r, b.r, t); o.g = mixf(a.g, b.g, t); o.b = mixf(a.b, b.b, t);
    return o;
}

static ANativeWindow *gWin = NULL;
static pthread_t gThread;
static volatile int gRun = 0;
static volatile int gPaused = 0;
static int gFrames = 0;
static float gDrawMs = 0.0f;
static char gCtlPath[300] = {0};
static int gCtlState = -1;

static JavaVM *gVM = NULL;
static jobject gHostRef = NULL;
static jmethodID gMidMove = NULL;
static int gWPosX = 0, gWPosY = 0;
static int gLastWx = 0, gLastWy = 0;
static int gMaxX = 0, gMaxY = 0;
static int gScrW = 0, gScrH = 0;   /* v1.5: 真实屏幕尺寸，来自 files/screen.cfg */
static float gSkinR = -1.0f, gSkinG = 0.0f, gSkinB = 0.0f;
static int gSkinEar = 1, gSkinTail = 0, gSkinAhoge = 1;
static float gSkinSize = 1.0f;
static char gCfgPath[300] = {0};
static char gSkinDir[300] = {0};
static long gSkinMtime = 0;
static char gSkinCur[64] = {0};
static char gStatePath[300] = {0};
static int gHour = 12;
static float gSaveAcc = 0.0f;
static float gClockAcc = 0.0f;


static int gTapN = 0;
static double gLastDownT = 0.0;
static int gTapStartSleep = 0;
static int gHoldX = 0, gHoldY = 0, gFed = 1, gEdgeSide = 0;

static char gScreenPath[300] = {0};

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
static int gW = 400, gH = 400;
static int gViewW = 400, gViewH = 400;
#define SS 2
static pthread_mutex_t gLock = PTHREAD_MUTEX_INITIALIZER;   /* 只保护 gWin 指针 */

/* v1.5: 一把总锁，保护 P 结构体与所有跳线程共享的可变全局。
 * 约定：JNI 侧只在短临界区内持锁；渲染线程在 tick / render 期间持锁。
 * gLock 与 gMutex 从不嵌套，不存在 ABBA。 */
static pthread_mutex_t gMutex = PTHREAD_MUTEX_INITIALIZER;
#define ST_LOCK()   pthread_mutex_lock(&gMutex)
#define ST_UNLOCK() pthread_mutex_unlock(&gMutex)
static volatile int gThreadAlive = 0;   /* 渲染线程可 join */
static volatile int gInFrame = 0;       /* 正在 lock/unlockAndPost 之间 */
static float gLastDt = 0.033f;          /* 真实帧间隔，给呆毛弹簧用 */

/* speech bubble state (needed by tick and render) */
static uint32_t *gTxt = NULL;
static int gTxtW = 0, gTxtH = 0;
static float gBubShow = 0.0f;
static float gBubHold = 0.0f;
static float gBubVel = 0.0f;

enum { ST_IDLE = 0, ST_DRAG, ST_POKE, ST_SLEEP };

static struct {
    int   state;
    float stT;
    float phase;
    float blink, blinkT;
    float lookX, lookY, curLX, curLY;
    float squash, squashV;
    float mood, energy, yawn, lean;
    float edge, wake, hud;
    float tapT, pet, aff, offX, holdT;
    float idleT, nextIdle, idleAct;
    float ax[6], ay[6], oax[6], oay[6];
    int   ahInit;
    unsigned int rnd;
    float prt[8][5];
    int   prtN;
} P;

static void load_skin(void);

static int cmp_name(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

static void next_skin(void)
{
    if (!gSkinDir[0]) return;
    DIR *d = opendir(gSkinDir);
    if (!d) return;
    char names[20][64];
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL && n < 20) {
        size_t L = strlen(de->d_name);
        if (L > 4 && strcmp(de->d_name + L - 4, ".cfg") == 0) {
            strncpy(names[n], de->d_name, 63);
            names[n][63] = 0;
            n++;
        }
    }
    closedir(d);
    if (n == 0) return;
    qsort(names, (size_t)n, sizeof(names[0]), cmp_name);   /* v1.5: 与 ctl.sh 的 ls 顺序一致 */
    char cur[96];
    snprintf(cur, sizeof(cur), "%s.cfg", gSkinCur);
    int idx = -1;
    for (int i = 0; i < n; i++) if (strcmp(names[i], cur) == 0) { idx = i; break; }
    int nx = (idx + 1) % n;
    char nm[64];
    strncpy(nm, names[nx], 63);
    nm[63] = 0;
    size_t L2 = strlen(nm);
    if (L2 > 4) nm[L2 - 4] = 0;
    strncpy(gSkinCur, nm, sizeof(gSkinCur) - 1);
    gSkinCur[sizeof(gSkinCur) - 1] = 0;
    snprintf(gCfgPath, sizeof(gCfgPath), "%s/%s.cfg", gSkinDir, nm);
    load_skin();
    LOGI("next skin -> %s", nm);
}

JNIEXPORT void JNICALL Java_pet_runtime_Host_nativeNextSkin(JNIEnv *e, jclass c)
{
    (void)e; (void)c;
    ST_LOCK();
    next_skin();
    ST_UNLOCK();
}

static void save_state(void)
{
    if (!gStatePath[0]) return;
    char tmp[320];
    snprintf(tmp, sizeof(tmp), "%s.tmp", gStatePath);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "mood=%.3f\nenergy=%.3f\naff=%.3f\n", P.mood, P.energy, P.aff);
    if (fclose(f) != 0) return;
    rename(tmp, gStatePath);   /* v1.5: 原子发布，杀进程也不会留下半截状态 */
}

static void load_state(void)
{
    if (!gStatePath[0]) return;
    FILE *f = fopen(gStatePath, "r");
    if (!f) return;
    char line[96];
    while (fgets(line, sizeof(line) - 1, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = eq + 1;
        while (*v == ' ') v++;
        if (!strcmp(line, "mood")) P.mood = clampf((float)atof(v), -1.0f, 1.0f);
        else if (!strcmp(line, "energy")) P.energy = clampf((float)atof(v), 0.0f, 1.0f);
        else if (!strcmp(line, "aff")) P.aff = clampf((float)atof(v), 0.0f, 1.0f);
    }
    fclose(f);
    LOGI("state loaded mood=%.2f energy=%.2f aff=%.2f", P.mood, P.energy, P.aff);
}

/* v1.5: 屏幕尺寸由 service.sh 开机时写 files/screen.cfg（wm size）。
 * 拿不到就保持 0，边缘检测自动退回旧启发式。 */
static void load_screen(void)
{
    gScrW = gScrH = 0;
    if (!gScreenPath[0]) return;
    FILE *f = fopen(gScreenPath, "r");
    if (!f) return;
    char line[64];
    if (fgets(line, sizeof(line) - 1, f)) {
        int w = 0, h = 0;
        if (sscanf(line, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) { gScrW = w; gScrH = h; }
    }
    fclose(f);
    LOGI("screen %dx%d", gScrW, gScrH);
}

static float frnd(void)
{
    P.rnd = P.rnd * 1103515245u + 12345u;
    return (float)((P.rnd >> 9) & 0x7FFF) / 32767.0f;
}

static void spawn_particles(float nx, float ny)
{
    for (int i = 0; i < 7; i++) {
        if (P.prtN >= 8) break;
        float *q = P.prt[P.prtN++];
        q[0] = nx + (frnd() - 0.5f) * 0.17f;
        q[1] = ny + (frnd() - 0.5f) * 0.04f;
        q[2] = (frnd() - 0.5f) * 0.34f;
        q[3] = -0.09f - frnd() * 0.11f;
        q[4] = 0.72f + frnd() * 0.42f;
    }
}

/* ---------- pixel helpers ---------- */
static inline void blend_px(uint32_t *p, int a, float r, float g, float b)
{
    if (a <= 0) return;
    if (a > 255) a = 255;
    uint8_t *q = (uint8_t *)p;
    float sa = (float)a / 255.0f;
    float inv = 1.0f - sa;
    q[0] = (uint8_t)(r * sa + q[0] * inv);
    q[1] = (uint8_t)(g * sa + q[1] * inv);
    q[2] = (uint8_t)(b * sa + q[2] * inv);
    q[3] = (uint8_t)(255.0f * sa + q[3] * inv);
}

static void ellipse(uint32_t *base, int stride, int W, int H,
                    float cx, float cy, float rx, float ry, Col c, float alpha)
{
    if (rx <= 0.0f || ry <= 0.0f || alpha <= 0.001f) return;
    int x0 = (int)floorf(cx - rx - 2), x1 = (int)ceilf(cx + rx + 2);
    int y0 = (int)floorf(cy - ry - 2), y1 = (int)ceilf(cy + ry + 2);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1; if (y1 > H - 1) y1 = H - 1;
    float aa = rx < ry ? rx : ry;
    if (aa > 2.5f) aa = 2.5f;
    if (aa < 0.6f) aa = 0.6f;
    for (int y = y0; y <= y1; y++) {
        float dy = ((float)y + 0.5f - cy) / ry;
        uint32_t *row = &base[(size_t)y * stride];
        for (int x = x0; x <= x1; x++) {
            float dx = ((float)x + 0.5f - cx) / rx;
            float d2 = dx * dx + dy * dy;
            float cov;
            if (d2 <= 0.78f) {
                cov = 1.0f;
            } else if (d2 >= 1.35f) {
                continue;
            } else {
                float d = sqrtf(d2) - 1.0f;
                cov = 0.5f - d * aa * 0.5f;
                if (cov <= 0.0f) continue;
                if (cov > 1.0f) cov = 1.0f;
            }
            blend_px(&row[x], (int)(alpha * cov * 255.0f), c.r, c.g, c.b);
        }
    }
}

/* shaded body fill: vertical light gradient + edge shading + rim light */
static void ellipse_shaded(uint32_t *base, int stride, int W, int H,
                           float cx, float cy, float rx, float ry,
                           Col c, float alpha)
{
    if (rx <= 0.0f || ry <= 0.0f || alpha <= 0.001f) return;
    int x0 = (int)floorf(cx - rx - 2), x1 = (int)ceilf(cx + rx + 2);
    int y0 = (int)floorf(cy - ry - 2), y1 = (int)ceilf(cy + ry + 2);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1; if (y1 > H - 1) y1 = H - 1;
    float aa = rx < ry ? rx : ry;
    if (aa > 2.5f) aa = 2.5f;
    if (aa < 0.6f) aa = 0.6f;
    for (int y = y0; y <= y1; y++) {
        float dy = ((float)y + 0.5f - cy) / ry;
        uint32_t *row = &base[(size_t)y * stride];
        for (int x = x0; x <= x1; x++) {
            float dx = ((float)x + 0.5f - cx) / rx;
            float d2 = dx * dx + dy * dy;
            float cov;
            if (d2 <= 0.78f) {
                cov = 1.0f;
            } else if (d2 >= 1.35f) {
                continue;
            } else {
                float d = sqrtf(d2) - 1.0f;
                cov = 0.5f - d * aa * 0.5f;
                if (cov <= 0.0f) continue;
                if (cov > 1.0f) cov = 1.0f;
            }
            float n2 = d2 > 1.0f ? 1.0f : d2;
            float shade = 1.0f - 0.30f * n2;
            float light = 0.92f - 0.30f * dy;
            float rim = 0.0f;
            if (d2 > 0.70f) {
                float n = sqrtf(d2);
                if (n > 0.84f) rim = (n - 0.84f) * 0.48f;
            }
            float k = shade * light + rim;
            if (k < 0.35f) k = 0.35f;
            if (k > 1.35f) k = 1.35f;
            float rr = c.r * k, gg = c.g * k, bb = c.b * k;
            if (rr > 255.0f) rr = 255.0f;
            if (gg > 255.0f) gg = 255.0f;
            if (bb > 255.0f) bb = 255.0f;
            blend_px(&row[x], (int)(alpha * cov * 255.0f), rr, gg, bb);
        }
    }
}

static void dot(uint32_t *base, int stride, int W, int H, float cx, float cy,
                float rad, Col c, float alpha)
{
    ellipse(base, stride, W, H, cx, cy, rad, rad, c, alpha);
}

/* ---------- state machine ---------- */
static void set_state(int s)
{
    if (P.state != s) { P.state = s; P.stT = 0.0f; LOGI("state=%d buzz=%d", s, 1); }
}

static void ahoge_step(float dt, float hx, float hy, float s)
{
    const int N = 6;
    float L = s * 0.052f;
    if (!P.ahInit) {
        for (int i = 0; i < N; i++) {
            P.ax[i] = hx; P.ay[i] = hy - i * L;
            P.oax[i] = P.ax[i]; P.oay[i] = P.ay[i];
        }
        P.ahInit = 1;
    }
    P.ax[0] = hx; P.ay[0] = hy;
    float g = s * 1.6f;
    float damp = 0.90f;
    for (int i = 1; i < N; i++) {
        float vx = (P.ax[i] - P.oax[i]) * damp;
        float vy = (P.ay[i] - P.oay[i]) * damp + g * dt * dt;
        P.oax[i] = P.ax[i]; P.oay[i] = P.ay[i];
        P.ax[i] += vx; P.ay[i] += vy;
    }
    for (int pass = 0; pass < 3; pass++) {
        for (int i = 1; i < N; i++) {
            float dx = P.ax[i] - P.ax[i - 1], dy = P.ay[i] - P.ay[i - 1];
            float d = sqrtf(dx * dx + dy * dy);
            if (d > 0.0001f) {
                float diff = (d - L) / d;
                float k = (i == 1) ? 1.0f : 0.5f;
                P.ax[i] -= dx * diff * k;
                P.ay[i] -= dy * diff * k;
            }
        }
    }
}

static void tick(float dt)
{
    P.stT += dt;
    P.idleT += dt;

    if (P.blink > 0.002f) P.blink *= powf(0.22f, dt);
    P.blinkT -= dt;
    if (P.blinkT <= 0.0f) {
        P.blinkT = 2.0f + frnd() * 5.0f;
        P.blink = 1.0f;
    }

    P.curLX += (P.lookX - P.curLX) * clampf(dt * 10.0f, 0.0f, 1.0f);
    P.curLY += (P.lookY - P.curLY) * clampf(dt * 10.0f, 0.0f, 1.0f);
    P.lookX *= powf(0.25f, dt);
    P.lookY *= powf(0.25f, dt);
    P.lean *= powf(0.15f, dt);

    P.squashV += (-95.0f * P.squash - 11.0f * P.squashV) * dt;
    P.squash += P.squashV * dt;
    P.squash = clampf(P.squash, -1.0f, 1.0f);

    if (P.yawn > 0.002f) P.yawn *= powf(0.22f, dt);
    if (P.wake > 0.002f) P.wake *= powf(0.05f, dt);
    if (P.hud > 0.002f) P.hud -= dt;
    if (P.state == ST_DRAG) {
        P.holdT += dt;
        int mv = (gWPosX > gHoldX ? gWPosX - gHoldX : gHoldX - gWPosX) +
                 (gWPosY > gHoldY ? gWPosY - gHoldY : gHoldY - gWPosY);
        if (!gFed && P.holdT > 0.9f && mv < 30) {
            gFed = 1;
            P.mood = clampf(P.mood + 0.45f, -1.0f, 1.0f);
            P.energy = clampf(P.energy + 0.35f, 0.0f, 1.0f);
            P.hud = 2.6f;
            P.squash = 0.85f;
            P.squashV = -2.0f;
            spawn_particles(0.5f, 0.46f);
            spawn_particles(0.5f, 0.54f);
            LOGI("feed mood=%.2f", P.mood);
        }
    }
    {
        float etgt = (P.edge > 0.5f && P.state != ST_DRAG) ? 1.0f : 0.0f;
        P.offX += (etgt - P.offX) * clampf(dt * 3.0f, 0.0f, 1.0f);
    }
    P.tapT += dt;
    P.pet *= powf(0.35f, dt);
    if (P.idleAct > 0.002f) P.idleAct *= powf(0.10f, dt);

    float speed = (P.state == ST_SLEEP) ? 1.1f : 1.9f;
    P.phase += dt * speed;
    if (P.phase > 6.2831853f) P.phase -= 6.2831853f;

    gClockAcc += dt;
    if (gClockAcc > 2.0f) {
        gClockAcc = 0.0f;
        time_t tt = time(NULL);
        struct tm *lt = localtime(&tt);
        if (lt) gHour = lt->tm_hour;
    }
    int night = (gHour >= 23 || gHour < 5) ? 1 : 0;
    float drain = night ? 0.0085f : 0.0035f;
    P.energy = clampf(P.energy - dt * drain, 0.0f, 1.0f);
    if (P.aff > 0.01f) P.mood = clampf(P.mood + dt * 0.0015f * P.aff, -1.0f, 1.0f);
    gSaveAcc += dt;
    if (gSaveAcc > 5.0f) {
        gSaveAcc = 0.0f;
        save_state();
    }

    for (int i = 0; i < P.prtN; ) {
        float *q = P.prt[i];
        q[3] += 0.30f * dt;
        q[0] += q[2] * dt;
        q[1] += q[3] * dt;
        q[4] -= dt * 0.95f;
        if (q[4] <= 0.0f) {
            for (int k = i; k < P.prtN - 1; k++)
                for (int j = 0; j < 5; j++) P.prt[k][j] = P.prt[k + 1][j];
            P.prtN--;
        } else {
            i++;
        }
    }

    switch (P.state) {
    case ST_IDLE:
        if (P.energy < 0.10f || (P.idleT > (night ? 20.0f : 45.0f) && P.energy < 0.45f)) set_state(ST_SLEEP);
        break;
    case ST_POKE:
        if (P.stT > 0.5f) set_state(ST_IDLE);
        break;
    case ST_SLEEP:
        P.energy = clampf(P.energy + dt * 0.03f, 0.0f, 1.0f);
        if (P.energy > 0.9f) set_state(ST_IDLE);
        break;
    default:
        break;
    }

    if (gBubHold > 0.0f) gBubHold -= dt;
    float btarget = (gBubHold > 0.0f) ? 1.0f : 0.0f;
    {
        float sdt = dt * 0.25f;
        for (int si = 0; si < 4; si++) {
            gBubVel += ((btarget - gBubShow) * 90.0f - gBubVel * 12.0f) * sdt;
            gBubShow += gBubVel * sdt;
        }
    }
    if (gBubShow < 0.0f) { gBubShow = 0.0f; gBubVel = 0.0f; }
    if (gBubShow > 1.12f) { gBubShow = 1.12f; gBubVel = 0.0f; }
    {
        float dsh = gBubShow - btarget;
        float dvl = gBubVel < 0.0f ? -gBubVel : gBubVel;
        if ((dsh < 0.0f ? -dsh : dsh) < 0.004f && dvl < 0.03f) {
            gBubShow = btarget;
            gBubVel = 0.0f;
        }
    }

    P.nextIdle -= dt;
    if (P.nextIdle <= 0.0f) {
        P.nextIdle = 2.5f + frnd() * 6.0f;
        if (P.state == ST_IDLE) {
            P.idleAct = 1.0f;
            if (frnd() < 0.45f) P.blink = 1.0f;
            P.lookX = (frnd() - 0.5f) * 1.2f;
            P.lookY = (frnd() - 0.5f) * 0.8f;
        } else if (P.state == ST_SLEEP && frnd() < 0.3f) {
            P.yawn = 1.0f;
        }
    }
}

/* ---------- speech bubble ---------- */

static void round_rect(uint32_t *base, int stride, int W, int H,
                       float x0, float y0, float x1, float y1, float r,
                       Col c, float alpha)
{
    if (x1 <= x0 || y1 <= y0) return;
    float rx = (x1 - x0) * 0.5f;
    float ry = (y1 - y0) * 0.5f;
    if (r > rx) r = rx;
    if (r > ry) r = ry;
    float ccx = (x0 + x1) * 0.5f;
    float ccy = (y0 + y1) * 0.5f;
    ellipse(base, stride, W, H, ccx, ccy, rx, ry - r, c, alpha);
    ellipse(base, stride, W, H, ccx, ccy, rx - r, ry, c, alpha);
    dot(base, stride, W, H, x0 + r, y0 + r, r, c, alpha);
    dot(base, stride, W, H, x1 - r, y0 + r, r, c, alpha);
    dot(base, stride, W, H, x0 + r, y1 - r, r, c, alpha);
    dot(base, stride, W, H, x1 - r, y1 - r, r, c, alpha);
}

static void draw_bubble(uint32_t *base, int stride, int W, int H, float s, float cx, float cy)
{
    if (!gTxt || gBubShow <= 0.02f) return;

    float a0 = gBubShow > 1.0f ? 1.0f : gBubShow;
    float sc = 0.88f + 0.12f * a0;
    float bw = ((float)gTxtW + 0.034f * s) * sc;
    float bh = ((float)gTxtH + 0.026f * s) * sc;
    float byy = cy - 0.300f * s - bh * 0.5f;
    float x0 = cx - bw * 0.5f, x1 = cx + bw * 0.5f;
    float y0 = byy - bh * 0.5f, y1 = byy + bh * 0.5f;
    if (x0 < 4.0f) { x1 += 4.0f - x0; x0 = 4.0f; }
    if (x1 > (float)(W - 4)) { x0 -= x1 - (float)(W - 4); x1 = (float)(W - 4); }
    if (y0 < 4.0f) { y1 += 4.0f - y0; y0 = 4.0f; }

    float a = a0;
    float r = 0.032f * s;
    Col bgc = C(255, 251, 242);
    Col brd = C(126, 100, 88);

    round_rect(base, stride, W, H, x0 + 1.5f, y0 + 2.5f, x1 + 1.5f, y1 + 2.5f, r, C(0, 0, 0), 0.13f * a);
    round_rect(base, stride, W, H, x0 - 1.0f, y0 - 1.0f, x1 + 1.0f, y1 + 1.0f, r, brd, a);
    round_rect(base, stride, W, H, x0, y0, x1, y1, r, bgc, a);

    dot(base, stride, W, H, cx - 0.015f * s, y1 + 0.006f * s, 0.020f * s, brd, a);
    dot(base, stride, W, H, cx - 0.015f * s, y1 + 0.004f * s, 0.014f * s, bgc, a);

    int tx = (int)(x0 + (bw - (float)gTxtW) * 0.5f);
    int ty = (int)(y0 + (bh - (float)gTxtH) * 0.5f);
    int ga = (int)(a * 255.0f);
    for (int y = 0; y < gTxtH; y++) {
        int sy = ty + y;
        if (sy < 0 || sy >= H) continue;
        uint32_t *row = &base[(size_t)sy * stride];
        for (int x = 0; x < gTxtW; x++) {
            int sx = tx + x;
            if (sx < 0 || sx >= W) continue;
            uint32_t v = gTxt[y * gTxtW + x];
            int sa = (int)((v >> 24) & 0xFF);
            if (sa == 0) continue;
            sa = sa * ga / 255;
            blend_px(&row[sx], sa, (float)((v >> 16) & 0xFF), (float)((v >> 8) & 0xFF), (float)(v & 0xFF));
        }
    }
}

/* ---------- draw ---------- */
static void render_frame(ANativeWindow_Buffer *buf)
{
    uint32_t *base = (uint32_t *)buf->bits;
    int stride = buf->stride;
    int W = buf->width, H = buf->height;

    float s = (float)(W < H ? W : H) * gSkinSize;
    int   sleepy = (P.state == ST_SLEEP);

    int expr = (int)((P.mood + 1.0f) * 3.5f);
    if (expr < 0) expr = 0;
    if (expr > 7) expr = 7;

    float bobAmp = sleepy ? 0.009f : 0.015f;
    float bob = sinf(P.phase) * bobAmp * s;
    float sqx = 1.0f + P.squash * 0.20f;
    float sqy = 1.0f - P.squash * 0.20f;
    if (P.edge > 0.5f) {
        sqx *= 1.10f;
        sqy *= 0.90f;
    }

    float cx = W * 0.5f + P.lean * 0.035f * s + P.offX * (float)gEdgeSide * 0.40f * s;
    float cy = H * 0.60f + bob;

    /* full clear: correctness over the ~1.5ms dirty-rect saving */
    for (int yy = 0; yy < H; yy++)
        memset(&base[(size_t)yy * stride], 0, (size_t)W * 4);

    Col happy  = C(255, 176, 120);
    Col calm   = C(210, 190, 175);
    Col sad    = C(150, 170, 200);
    Col slp    = C(150, 158, 180);
    Col body = (P.mood >= 0.0f) ? mixc(calm, happy, P.mood) : mixc(calm, sad, -P.mood);
    body = mixc(body, slp, sleepy ? 0.55f : 0.0f);
    body = mixc(body, C(255, 240, 230), P.idleAct * 0.12f);
    if (gSkinR >= 0.0f) body = C(gSkinR, gSkinG, gSkinB);

    Col outline = mixc(C(120, 96, 84), C(70, 76, 100), sleepy ? 0.7f : 0.0f);
    Col dark  = C(64, 56, 62);
    Col white = C(255, 255, 255);
    Col blush = C(255, 130, 150);

    /* ground shadow */
    ellipse(base, stride, W, H, cx, cy + 0.21f * s, 0.24f * s, 0.042f * s, C(0, 0, 0), 0.16f);

    /* ears */
    if (gSkinEar != 0) {
        float earDx = 0.165f * s;
        float earY  = cy - 0.215f * s * sqy;
        float earRx = (gSkinEar == 2) ? 0.052f : 0.072f;
        float earRy = (gSkinEar == 2) ? 0.140f : 0.108f;
        ellipse(base, stride, W, H, cx - earDx, earY, earRx * s, earRy * s, outline, 1.0f);
        ellipse(base, stride, W, H, cx + earDx, earY, earRx * s, earRy * s, outline, 1.0f);
        ellipse(base, stride, W, H, cx - earDx, earY + 0.014f * s, earRx * 0.62f * s, earRy * 0.66f * s, body, 1.0f);
        ellipse(base, stride, W, H, cx + earDx, earY + 0.014f * s, earRx * 0.62f * s, earRy * 0.66f * s, body, 1.0f);
    }

    /* body */
    float rx = 0.285f * s * sqx;
    float ry = 0.255f * s * sqy;
    ellipse(base, stride, W, H, cx, cy, rx + 0.017f * s, ry + 0.017f * s, outline, 1.0f);
    ellipse_shaded(base, stride, W, H, cx, cy, rx, ry, body, 1.0f);

    if (gSkinTail != 0) {
        float tx0 = cx + rx * 0.92f;
        dot(base, stride, W, H, tx0 + 0.028f * s, cy + 0.020f * s, 0.030f * s, outline, 1.0f);
        dot(base, stride, W, H, tx0 + 0.060f * s, cy - 0.018f * s, 0.024f * s, outline, 1.0f);
        dot(base, stride, W, H, tx0 + 0.082f * s, cy - 0.070f * s, 0.017f * s, outline, 1.0f);
    }

    /* top highlight */
    ellipse(base, stride, W, H, cx - 0.085f * s, cy - 0.105f * s,
            0.115f * s, 0.075f * s, white, 0.18f);

    if (gSkinAhoge != 0) {
        float hx = cx - P.lean * 0.02f * s;
        float hy = cy - 0.255f * s * sqy - 0.008f * s;
        ahoge_step(gLastDt, hx, hy, s);   /* v1.5: 用真实帧间隔，弹簧速度不再随帧率漂移 */
        for (int i = 1; i < 6; i++) {
            float t = (float)i / 5.0f;
            float r = mixf(0.020f, 0.005f, t) * s;
            dot(base, stride, W, H, P.ax[i], P.ay[i], r, outline, 0.98f);
        }
    }

    /* eyes */
    float eyeDx = 0.105f * s * sqx;
    float eyeY  = cy - 0.048f * s + P.curLY * 0.012f * s;
    float erx = 0.043f * s;
    float open = 1.0f - (sleepy ? 0.93f : 0.0f);
    float blinkAmt = clampf(P.blink + (sleepy ? 0.93f : 0.0f), 0.0f, 1.0f);
    if (P.state == ST_DRAG) blinkAmt = clampf(blinkAmt, 0.25f, 1.0f);
    float ery = 0.055f * s * (1.0f - 0.92f * blinkAmt) * open * (0.86f + 0.045f * (float)expr);
    if (P.wake > 0.02f) ery *= 1.0f + 0.40f * (P.wake > 1.0f ? 1.0f : P.wake);
    float px = P.curLX * 0.017f * s;
    float py = P.curLY * 0.010f * s;

    if (ery > 0.004f * s) {
        ellipse(base, stride, W, H, cx - eyeDx, eyeY, erx, ery, dark, 1.0f);
        ellipse(base, stride, W, H, cx + eyeDx, eyeY, erx, ery, dark, 1.0f);
        if (ery > 0.018f * s) {
            dot(base, stride, W, H, cx - eyeDx + px - 0.013f * s, eyeY + py - 0.016f * s,
                0.014f * s, white, 0.95f);
            dot(base, stride, W, H, cx + eyeDx + px - 0.013f * s, eyeY + py - 0.016f * s,
                0.014f * s, white, 0.95f);
        }
    } else {
        /* closed eyes: soft arcs */
        ellipse(base, stride, W, H, cx - eyeDx, eyeY, 0.040f * s, 0.010f * s, dark, 0.85f);
        ellipse(base, stride, W, H, cx + eyeDx, eyeY, 0.040f * s, 0.010f * s, dark, 0.85f);
    }

    /* blush */
    float ba = (P.mood > 0.0f) ? 0.28f + 0.30f * P.mood : 0.22f;
    if (sleepy) ba *= 0.6f;
    ellipse(base, stride, W, H, cx - 0.185f * s * sqx, cy + 0.055f * s, 0.046f * s, 0.026f * s, blush, ba);
    ellipse(base, stride, W, H, cx + 0.185f * s * sqx, cy + 0.055f * s, 0.046f * s, 0.026f * s, blush, ba);

    /* mouth */
    float mx = cx + P.curLX * 0.006f * s;
    float my = cy + 0.082f * s;
    static const float MW[8] = { 0.030f, 0.028f, 0.022f, 0.026f, 0.032f, 0.036f, 0.040f, 0.030f };
    static const float MH[8] = { 0.005f, 0.006f, 0.006f, 0.008f, 0.012f, 0.018f, 0.026f, 0.009f };
    static const float MY[8] = { 0.090f, 0.088f, 0.084f, 0.082f, 0.082f, 0.086f, 0.090f, 0.082f };
    if (P.yawn > 0.25f) {
        ellipse(base, stride, W, H, mx, my + 0.012f * s,
                0.030f * s, (0.016f + 0.034f * P.yawn) * s, dark, 0.92f);
    } else {
        ellipse(base, stride, W, H, mx, my + MY[expr] * s,
                MW[expr] * s, MH[expr] * s, dark, 0.9f);
    }
    if (expr >= 6) {
        dot(base, stride, W, H, mx - 0.012f * s, my + 0.070f * s, 0.006f * s, white, 0.85f);
        dot(base, stride, W, H, mx + 0.012f * s, my + 0.070f * s, 0.006f * s, white, 0.85f);
    }

    for (int i = 0; i < P.prtN; i++) {
        float *q = P.prt[i];
        float qx = q[0] * (float)W;
        float qy = q[1] * (float)H;
        float qa = q[4] > 1.0f ? 1.0f : q[4];
        float qr = 0.046f * s * (0.72f + 0.28f * qa);
        Col pc = C(255, 64, 120);
        dot(base, stride, W, H, qx, qy - qr * 0.30f, qr * 2.10f, C(255, 120, 160), qa * 0.30f);
        dot(base, stride, W, H, qx - qr * 0.44f, qy - qr * 0.32f, qr * 0.68f, pc, qa);
        dot(base, stride, W, H, qx + qr * 0.44f, qy - qr * 0.32f, qr * 0.68f, pc, qa);
        dot(base, stride, W, H, qx, qy + qr * 0.30f, qr * 0.64f, pc, qa);
        dot(base, stride, W, H, qx - qr * 0.16f, qy - qr * 0.46f, qr * 0.14f, C(255, 225, 235), qa * 0.95f);
    }

    if (P.hud > 0.02f) {
        float ha = P.hud > 1.0f ? 1.0f : P.hud;
        float hbw = 0.30f * s;
        float hx0 = cx - hbw * 0.5f;
        float hy0 = cy - 0.36f * s;
        float mf = (P.mood + 1.0f) * 0.5f;
        float ef2 = P.energy;
        ellipse(base, stride, W, H, cx, hy0, hbw * 0.5f + 0.012f * s, 0.020f * s, C(40, 40, 50), 0.55f * ha);
        ellipse(base, stride, W, H, hx0 + hbw * 0.5f * mf, hy0, hbw * 0.5f * mf, 0.013f * s, C(255, 170, 90), 0.95f * ha);
        ellipse(base, stride, W, H, cx, hy0 + 0.040f * s, hbw * 0.5f + 0.012f * s, 0.016f * s, C(40, 40, 50), 0.55f * ha);
        ellipse(base, stride, W, H, hx0 + hbw * 0.5f * ef2, hy0 + 0.040f * s, hbw * 0.5f * ef2, 0.010f * s, C(120, 220, 255), 0.95f * ha);
    }

    draw_bubble(base, stride, W, H, s, cx, cy);

    /* sleep puffs */
    if (sleepy) {
        for (int i = 0; i < 3; i++) {
            float t = P.phase * 0.55f + i * 0.9f;
            float f = fmodf(t, 2.7f) / 2.7f;
            float bx = cx + (0.22f + f * 0.10f) * s;
            float by = cy - (0.20f + f * 0.22f) * s;
            dot(base, stride, W, H, bx, by, (0.010f + f * 0.016f) * s, white, 0.55f * (1.0f - f));
        }
    }
}

/* ---------- loop ---------- */
static void load_skin(void)
{
    if (!gCfgPath[0]) return;
    gSkinR = -1.0f; gSkinG = 0.0f; gSkinB = 0.0f;
    gSkinEar = 1; gSkinTail = 0; gSkinAhoge = 1; gSkinSize = 1.0f;
    struct stat sst;
    if (stat(gCfgPath, &sst) == 0) gSkinMtime = (long)sst.st_mtime;
    FILE *f = fopen(gCfgPath, "r");
    if (!f) return;
    char line[160];
    while (fgets(line, sizeof(line) - 1, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = line;
        char *v = eq + 1;
        while (*v == ' ') v++;
        if (!strcmp(k, "body")) {
            float r = 0, g = 0, b = 0;
            if (sscanf(v, "%f,%f,%f", &r, &g, &b) == 3) { gSkinR = r; gSkinG = g; gSkinB = b; }
        } else if (!strcmp(k, "ear")) {
            gSkinEar = atoi(v);
        } else if (!strcmp(k, "tail")) {
            gSkinTail = atoi(v);
        } else if (!strcmp(k, "ahoge")) {
            gSkinAhoge = atoi(v);
        } else if (!strcmp(k, "size")) {
            gSkinSize = (float)atof(v);
            if (gSkinSize < 0.55f) gSkinSize = 0.55f;
            if (gSkinSize > 1.45f) gSkinSize = 1.45f;
        }
    }
    fclose(f);
    LOGI("skin r=%.0f g=%.0f b=%.0f ear=%d tail=%d ahoge=%d size=%.2f",
         gSkinR, gSkinG, gSkinB, gSkinEar, gSkinTail, gSkinAhoge, gSkinSize);
}

static void check_ctl(void)
{
    if (!gCtlPath[0]) return;
    char b[128];
    memset(b, 0, sizeof(b));
    FILE *cf = fopen(gCtlPath, "r");
    if (!cf) return;
    int got = (fgets(b, sizeof(b) - 1, cf) != NULL);
    fclose(cf);
    /* v1.5: 读即消费。ctl.sh 用 .tmp+mv 写入，unlink 不会丢掉新写入的命令 */
    unlink(gCtlPath);
    if (!got) return;
    if (b[0] == 's' && b[1] == 'k' && gSkinDir[0]) {
        char nm[64];
        int i = 0;
        char *c = b + 5;                 /* 跳过 "skin:" */
        while (*c && *c != '\n' && *c != '\r' && i < 63) nm[i++] = *c++;
        nm[i] = 0;
        if (i > 0 && strcmp(nm, gSkinCur) != 0) {
            strncpy(gSkinCur, nm, sizeof(gSkinCur) - 1);
            gSkinCur[sizeof(gSkinCur) - 1] = 0;
            snprintf(gCfgPath, sizeof(gCfgPath), "%s/%s.cfg", gSkinDir, nm);
            load_skin();
            LOGI("ctl skin -> %s", nm);
        }
        return;
    }
    int st = (b[0] == 'p' || b[0] == 'P') ? 1 : ((b[0] == 'r' || b[0] == 'R') ? 0 : -1);
    if (st >= 0 && st != gCtlState) {
        gCtlState = st;
        gPaused = st;
        LOGI("ctl-file %s paused=%d", b, gPaused);
    }
}

static void *render_loop(void *arg)
{
    (void)arg;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (gRun) {
        /* v1.6 修正：ctl 必须在“暂停分支”之前处理。
         * v1.5 把它放在 `if (gPaused) continue;` 之后，结果 pause 之后永远读不到 resume，
         * 只能重启进程才能恢复渲染（实测 f=285 -> f=285 卡死）。 */
        ST_LOCK();
        check_ctl();
        ST_UNLOCK();

        ANativeWindow *win = NULL;
        pthread_mutex_lock(&gLock);
        win = gWin;
        if (win) ANativeWindow_acquire(win);
        pthread_mutex_unlock(&gLock);

        if (win && !gPaused) {
            ANativeWindow_Buffer buf;
            gInFrame = 1;
            if (ANativeWindow_lock(win, &buf, NULL) == 0) {
                struct timespec rt0, rt1;
                clock_gettime(CLOCK_MONOTONIC, &rt0);
                ST_LOCK();                     /* v1.5: 整帧持锁，与 JNI 侧互斥 */
                render_frame(&buf);
                ST_UNLOCK();
                clock_gettime(CLOCK_MONOTONIC, &rt1);
                gDrawMs += (float)(rt1.tv_sec - rt0.tv_sec) * 1000.0f + (float)(rt1.tv_nsec - rt0.tv_nsec) / 1e6f;
                ANativeWindow_unlockAndPost(win);
                gFrames++;
                if ((gFrames % 15) == 0) {
                    LOGI("f=%d avg=%.2fms", gFrames, gDrawMs / 15.0f);
                    gDrawMs = 0.0f;
                    if (gCfgPath[0]) {
                        struct stat sst;
                        ST_LOCK();
                        if (stat(gCfgPath, &sst) == 0 && (long)sst.st_mtime != gSkinMtime) {
                            load_skin();
                            LOGI("skin reloaded");
                        }
                        ST_UNLOCK();
                    }
                }
            }
            gInFrame = 0;
        }
        if (win) ANativeWindow_release(win);

        if (!gRun) break;
        if (gPaused) { usleep(200000); continue; }

        clock_gettime(CLOCK_MONOTONIC, &t1);
        float dt = (float)(t1.tv_sec - t0.tv_sec) + (float)(t1.tv_nsec - t0.tv_nsec) / 1e9f;
        if (dt <= 0.0f || dt > 0.25f) dt = 0.033f;
        t0 = t1;
        gLastDt = dt;
        int busy;
        ST_LOCK();
        tick(dt);
        busy = (P.state != ST_IDLE) || (gBubShow > 0.015f) || (P.squash > 0.02f);
        ST_UNLOCK();
        usleep(busy ? 33000 : 66000);
    }
    return NULL;
}

/* ---------- JNI ---------- */
static void probe_freetype(void)
{
    const char *paths[2] = { "libft2.so", "/system/lib64/libft2.so" };
    for (int i = 0; i < 2; i++) {
        void *h = dlopen(paths[i], RTLD_NOW);
        if (h) {
            void *fi = dlsym(h, "FT_Init_FreeType");
            void *nf = dlsym(h, "FT_New_Face");
            void *sl = dlsym(h, "FT_Set_Pixel_Sizes");
            LOGI("FT ok via %s init=%p newface=%p setpx=%p", paths[i], fi, nf, sl);
            dlclose(h);
            return;
        }
        LOGE("FT fail %s: %s", paths[i], dlerror());
    }
}

JNIEXPORT void JNICALL Java_pet_runtime_Host_nativeInit(JNIEnv *e, jclass c, jstring dir)
{
    (void)c;
    gVerbose = 0;
    if (dir) {
        const char *d = (*e)->GetStringUTFChars(e, dir, NULL);
        if (d) {
            char p[300];
            struct stat st;
            snprintf(p, sizeof(p), "%s/debug", d);
            gVerbose = (stat(p, &st) == 0) ? 1 : 0;
            (*e)->ReleaseStringUTFChars(e, dir, d);
        }
    }
    if (dir) {
        const char *d2 = (*e)->GetStringUTFChars(e, dir, NULL);
        if (d2) {
            snprintf(gCtlPath, sizeof(gCtlPath), "%s/ctl", d2);
            snprintf(gCfgPath, sizeof(gCfgPath), "%s/skin.cfg", d2);
            snprintf(gSkinDir, sizeof(gSkinDir), "%s/skins", d2);
            snprintf(gStatePath, sizeof(gStatePath), "%s/state.cfg", d2);
            snprintf(gScreenPath, sizeof(gScreenPath), "%s/screen.cfg", d2);
            (*e)->ReleaseStringUTFChars(e, dir, d2);
        }
    }
    load_screen();
    ST_LOCK();
    load_skin();
    LOGI("nativeInit verbose=%d", gVerbose);
    memset(&P, 0, sizeof(P));
    P.rnd = 12345u;
    P.mood = 0.35f;
    P.energy = 0.95f;
    P.blinkT = 3.0f;
    P.nextIdle = 3.0f;
    P.state = ST_IDLE;
    load_state();
    ST_UNLOCK();
    LOGI("nativeInit v1.5 state=%d screen=%dx%d", P.state, gScrW, gScrH);
    probe_freetype();
}

JNIEXPORT void JNICALL Java_pet_runtime_Host_nativeSurface(JNIEnv *e, jclass c, jobject surface, jint w, jint h)
{
    (void)c;
    if (!surface) return;
    ANativeWindow *nw = ANativeWindow_fromSurface(e, surface);
    if (!nw) { LOGE("fromSurface failed"); return; }
    ST_LOCK();
    gViewW = w > 0 ? (int)w : 400;
    gViewH = h > 0 ? (int)h : 400;
    ST_UNLOCK();
    gW = 1080;
    gH = 1080;
    ANativeWindow_setBuffersGeometry(nw, gW, gH, WINDOW_FORMAT_RGBA_8888);
    /* v1.5: 上一轮渲染线程若已退出，先回收，避免同进程内出现两条渲染线程 */
    if (gThreadAlive) {
        gRun = 0;
        pthread_mutex_lock(&gLock);
        ANativeWindow *old = gWin;
        gWin = NULL;
        pthread_mutex_unlock(&gLock);
        if (old) ANativeWindow_release(old);
        pthread_join(gThread, NULL);
        gThreadAlive = 0;
    }
    pthread_mutex_lock(&gLock);
    if (gWin) ANativeWindow_release(gWin);
    gWin = nw;
    pthread_mutex_unlock(&gLock);
    ST_LOCK();
    P.ahInit = 0;
    ST_UNLOCK();
    gRun = 1;
    if (!gThreadAlive) {
        if (pthread_create(&gThread, NULL, render_loop, NULL) == 0) gThreadAlive = 1;
        else { gRun = 0; LOGE("pthread_create failed"); }
    }
    LOGI("nativeSurface %dx%d view=%dx%d", gW, gH, gViewW, gViewH);
}

JNIEXPORT void JNICALL Java_pet_runtime_Host_nativeStop(JNIEnv *e, jclass c)
{
    (void)e; (void)c;
    gRun = 0;   /* v1.5: 通知渲染线程退出，不再活到进程被杀；由下次 nativeSurface join */
    for (int i = 0; i < 60 && gInFrame; i++) usleep(2000);   /* 等当前帧落盘，最多 120ms */
    pthread_mutex_lock(&gLock);
    ANativeWindow *w = gWin;
    gWin = NULL;
    pthread_mutex_unlock(&gLock);
    if (w) ANativeWindow_release(w);
    ST_LOCK();
    if (gTxt) { free(gTxt); gTxt = NULL; gTxtW = 0; gTxtH = 0; }   /* v1.5: 补上泄漏的文本缓冲 */
    gBubShow = 0.0f; gBubVel = 0.0f; gBubHold = 0.0f;
    ST_UNLOCK();
    LOGI("nativeStop");
}

JNIEXPORT void JNICALL Java_pet_runtime_Host_nativePause(JNIEnv *e, jclass c)
{
    (void)e; (void)c;
    ST_LOCK();
    gPaused = 1;
    ST_UNLOCK();
    LOGI("pause");
}

JNIEXPORT void JNICALL Java_pet_runtime_Host_nativeResume(JNIEnv *e, jclass c)
{
    (void)e; (void)c;
    ST_LOCK();
    gPaused = 0;
    ST_UNLOCK();
    LOGI("resume");
}

JNIEXPORT void JNICALL Java_pet_runtime_Host_nativeBubble(JNIEnv *e, jclass c, jintArray px, jint w, jint h)
{
    (void)c;
    if (!px || w <= 0 || h <= 0) { ST_LOCK(); gBubHold = 0.0f; ST_UNLOCK(); return; }
    jint *buf = (*e)->GetIntArrayElements(e, px, NULL);
    if (!buf) return;
    uint32_t *nb = (uint32_t *)malloc((size_t)w * (size_t)h * 4u);
    if (!nb) { (*e)->ReleaseIntArrayElements(e, px, buf, JNI_ABORT); return; }
    jsize n = (*e)->GetArrayLength(e, px);
    if (n > w * h) n = w * h;
    for (jsize i = 0; i < n; i++) nb[i] = (uint32_t)buf[i];
    (*e)->ReleaseIntArrayElements(e, px, buf, JNI_ABORT);
    ST_LOCK();   /* v1.5: 换指针与释放旧缓冲都在锁内（原本是渲染线程读一边 free 的 UAF） */
    if (gTxt) free(gTxt);
    gTxt = nb;
    gTxtW = (int)w;
    gTxtH = (int)h;
    gBubHold = 5.0f;
    ST_UNLOCK();
    LOGI("bubble %dx%d", w, h);
}

JNIEXPORT void JNICALL Java_pet_runtime_Host_nativeAttach(JNIEnv *e, jobject thiz)
{
    (*e)->GetJavaVM(e, &gVM);
    if (gHostRef) (*e)->DeleteGlobalRef(e, gHostRef);
    gHostRef = (*e)->NewGlobalRef(e, thiz);
    jclass cls = (*e)->GetObjectClass(e, thiz);
    gMidMove = (*e)->GetMethodID(e, cls, "moveBy", "(II)V");
    LOGI("attach host=%p moveBy=%p", (void *)gHostRef, (void *)gMidMove);
}

JNIEXPORT void JNICALL Java_pet_runtime_Host_nativeTouch(JNIEnv *e, jclass c, jint action, jfloat lx, jfloat ly, jint wx, jint wy)
{
    (void)e; (void)c;
    ST_LOCK();
    float nx = (gViewW > 0) ? (lx / (float)gViewW) * 2.0f - 1.0f : 0.0f;
    float ny = (gViewH > 0) ? (ly / (float)gViewH) * 2.0f - 1.0f : 0.0f;
    P.lookX = clampf(nx, -1.0f, 1.0f);
    P.lookY = clampf(ny, -1.0f, 1.0f);
    P.idleT = 0.0f;
    switch (action) {
    case 0: {   /* v1.5: C99 不允许 label 后直接声明 */
        double tnow = now_s();
        int isDbl = (gLastDownT > 0.0 && (tnow - gLastDownT) < 0.70);
        if (!isDbl) gTapStartSleep = (P.state == ST_SLEEP) ? 1 : 0;
        gTapN = isDbl ? (gTapN + 1) : 1;
        gLastDownT = tnow;
        P.tapT = 0.0f;
        if (gTapN >= 2) {
            gTapN = 0;
            gLastDownT = 0.0;
            if (gTapStartSleep) {
                set_state(ST_IDLE);
                P.energy = 0.95f;
            } else {
                set_state(ST_SLEEP);
                P.energy = 0.30f;
            }
            P.hud = 1.6f;
            break;
        }
        if (P.state == ST_SLEEP) P.wake = 1.0f;
        set_state(ST_DRAG);
        P.energy = clampf(P.energy + 0.06f, 0.0f, 1.0f);
        P.squash = 0.30f;
        P.hud = 2.4f;
        P.holdT = 0.0f;
        gHoldX = (int)wx;
        gHoldY = (int)wy;
        gFed = 0;
        gWPosX = (int)wx;
        gWPosY = (int)wy;
        gLastWx = (int)wx;
        gLastWy = (int)wy;
        break;
    }
    case 2: {
        P.squash = 0.42f;
        P.lean = clampf(P.lean * 0.6f + nx * 0.4f, -1.0f, 1.0f);
        /* v1.5: 边缘检测改用真实屏幕尺寸（window 左上角 + 视图尺寸 = 贴边条件），
         * 拿不到 screen.cfg 才退回「拖过的最大坐标」启发式。 */
        int vw = gViewW > 0 ? gViewW : 0;
        int vh = gViewH > 0 ? gViewH : 0;
        int rightEdge = (gScrW > 0 && vw > 0) ? (gScrW - vw) : gMaxX;
        int botEdge   = (gScrH > 0 && vh > 0) ? (gScrH - vh) : gMaxY;
        if ((int)wx > gMaxX) gMaxX = (int)wx;
        if ((int)wy > gMaxY) gMaxY = (int)wy;
        if (P.edge < 0.5f) {
            if ((int)wx <= 1) { P.edge = 1.0f; gEdgeSide = -1; }
            else if (rightEdge > 0 && (int)wx >= rightEdge - 3) { P.edge = 1.0f; gEdgeSide = 1; }
            else if ((int)wy <= 1) { P.edge = 1.0f; gEdgeSide = 0; }
            else if (botEdge > 0 && (int)wy >= botEdge - 3) { P.edge = 1.0f; gEdgeSide = 0; }
        } else {
            int awayX = (gEdgeSide < 0) ? ((int)wx - 1) : (rightEdge - (int)wx);
            int awayY = (botEdge > 0) ? (botEdge - (int)wy) : 999;
            int need = (int)(0.06f * (float)(gScrW > 0 ? gScrW : 1080));   /* 迟滞：屏宽 6% */
            if (need < 40) need = 40;
            if (gEdgeSide == 0) {
                if (awayX > need && awayY > need) P.edge = 0.0f;
            } else if (awayX > need) {
                P.edge = 0.0f;
            }
        }
        P.pet += (fabsf((float)((int)wx - gLastWx)) + fabsf((float)((int)wy - gLastWy))) / 400.0f;
        if (P.pet > 0.55f - 0.25f * P.aff) {
            P.pet = 0.0f;
            P.aff = clampf(P.aff + 0.012f, 0.0f, 1.0f);
            spawn_particles(0.5f, 0.50f);
            P.mood = clampf(P.mood + 0.06f, -1.0f, 1.0f);
            P.energy = clampf(P.energy + 0.04f, 0.0f, 1.0f);
            P.hud = 1.2f;
        }
        gLastWx = (int)wx;
        gLastWy = (int)wy;
        gWPosX = (int)wx;
        gWPosY = (int)wy;
        break;
    }
    case 1:
        if (P.state == ST_SLEEP) {
            P.hud = 1.6f;
            break;
        }
        set_state(ST_POKE);
        P.squash = 0.70f;
        P.squashV = -1.8f;
        P.mood = clampf(P.mood + 0.07f, -1.0f, 1.0f);
        P.energy = clampf(P.energy + 0.15f, 0.0f, 1.0f);
        P.edge = 0.0f;
        spawn_particles(0.5f, 0.50f);
        break;
    default:
        break;
    }
    ST_UNLOCK();
}
