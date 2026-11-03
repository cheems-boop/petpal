# 文件协议

petpal 的运行时状态全部在应用私有目录：

```
/data/data/pet.runtime/files/
├─ ctl             指令通道（写一条，native 读走即删）
├─ skin.cfg        当前皮肤
├─ skins/
│  ├─ cat.cfg fox.cfg slime.cfg ...
│  └─ .cur         当前皮肤名（ctl.sh 记账用）
├─ state.cfg       心情/能量/亲密度（跨重启）
├─ screen.cfg      屏幕尺寸 `1272x2800`，边缘检测用
├─ disabled        存在则不自启（ctl.sh stop/start）
└─ debug           存在则开 verbose 日志（`logcat -s PetPal:V`）
```

设计原则：**所有写入都是 `.tmp` + `rename` 原子发布**，native 侧“读即消费”（读完 `unlink`），
所以不会出现“半截配置”或“同一条指令每帧重复执行”。

## ctl（指令通道）

单行文本，容量 128 B，一次一条。

| 内容 | 行为 |
|---|---|
| `skin:<名字>` | 切到 `skins/<名字>.cfg`（不存在则忽略） |
| `pause` / `P` | 停渲染（渲染线程 0fps，进 200 ms 空转） |
| `resume` / `R` | 恢复渲染 |
| 其它 | 忽略 |

写入方（`ctl.sh` 用 `put_ctl()`）：

```sh
printf '%s\n' "$1" > $F/.ctl.tmp && mv -f $F/.ctl.tmp $F/ctl
```

读方（`pethost.c: check_ctl()`）在锁内 `fgets` → `fclose` → `unlink`。

## skin.cfg（皮肤）

```ini
body=255,140,80   # 身体 RGB
ear=2             # 0=无耳 1=圆耳 2=尖耳
tail=1            # 0=无尾 1=卷尾
ahoge=0           # 0=无呆毛 1=有
size=1.05         # 整体缩放 0.55~1.45
```

改文件即生效：渲染循环每 15 帧 `stat()` 一次 mtime，变了就重载。

## state.cfg（状态持久化）

```ini
mood=0.350        # -1.0 ~ 1.0
energy=0.950      #  0.0 ~ 1.0
aff=0.120         #  0.0 ~ 1.0 亲密度
```

每 5 秒存一次，写 `state.cfg.tmp` 后 `rename`；启动时 `nativeInit` 读回。

## screen.cfg（屏幕尺寸）

`<宽>x<高>`，由 `service.sh` 开机跑 `wm size` 写入（优先 Override size）。
native 用它判断“贴边”——条件 = 窗口左上角 + 视图尺寸 ≈ 屏幕边界，迟滞取屏宽 6%。
缺失时退回“拖过的最大坐标”启发式（不准确，但不会崩）。

## debug（调试开关）

```sh
touch /data/data/pet.runtime/files/debug   # 开 verbose
logcat -s PetPal:V                          # native 日志
```

关了它 `LOGI` 全部编译为空操作，热路径零开销。

## JNI 面（`libpethost.so` → `pet.runtime.Host`）

| 方法 | 作用 |
|---|---|
| `nativeInit(String dir)` | 传 files/ 目录，初始化状态/皮肤/屏幕 |
| `nativeSurface(Surface, w, h)` | 创建 ANativeWindow，起渲染线程 |
| `nativeStop()` | 停渲染线程、释放文本缓冲 |
| `nativeTouch(action, lx, ly, wx, wy)` | 0=DOWN 1=UP 2=MOVE，像素坐标 + 窗口位置 |
| `nativeBubble(int[], w, h)` | 传一张 ARGB 位图当对话气泡 |
| `nativePause()` / `nativeResume()` | 暂停 / 恢复 |
| `nativeNextSkin()` | 切下一个皮肤 |
| `nativeAttach(Host)` | 存全局引用，供 `moveBy()` 回调 |

> v1.6 起 `CtlRx` 广播接收器改为 `exported=false`：v1.5 时任何 App 发 `pet.ctl` 广播都能改状态。
> 外部控制统一走 `ctl.sh`（root），或 WebUI。
