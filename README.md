# petpal · 桌面宠物

**一只浮在 Android 桌面上的团子。37 KB，无网络权限，渲染核心是一千行手写 C 软光栅。**

它不依赖任何第三方 App、不申请多余权限、不联网。悬浮窗由 KernelSU/Magisk 模块开机拉起，
渲染全在 `libpethost.so` 里手算——**没有一张图片、没用 OpenGL、没用 Skia**，
球体明暗、边缘反光、抗锯齿都是自己算出来的。

| | |
|---|---|
| **37 KB** | 整个 APK，含 36 KB native 渲染核心；无美术资源 |
| **无网络权限** | manifest 里没有 `INTERNET`，一行都没有 |
| **可复现构建** | `tools/build.sh` 一条命令出包（clang + aapt2 + d8 + apksigner），CI 同款 |
| **文件即协议** | 换肤/暂停/喂食全走 `files/ctl`，root 脚本和 WebUI 都能控 |
| **1080 超采样** | 1080×1080 软光栅上屏缩到 525px，边缘干净到不像手写 |

```sh
# 装好模块后重启即可。手动控制：
/data/adb/modules/petpal/ctl.sh status     # 心情 / 能量 / 亲密度 / 皮肤
/data/adb/modules/petpal/ctl.sh skin:fox   # 换皮肤（热重载，不用重装）
/data/adb/modules/petpal/ctl.sh pause      # 停渲染
```

> 面向开发者的实现细节：[docs/protocol.md](docs/protocol.md)（文件协议 + JNI 面）、
> [docs/DECISIONS.md](docs/DECISIONS.md)（为什么这么设计）、`docs/tech-notes.md`（平台踩坑）。
> 想改造就直接看 `src/pethost.c`，它是自包含的，不依赖任何 Android 头文件以外的库。

## 它长什么样

一个 525px 的团子，带球体明暗渐变与边缘反光（约 1500 级色阶），支持 8 档表情。

| 操作 | 反应 |
|---|---|
| 轻点 | 弹跳 + 7 颗心形粒子 + 头顶心情/能量条 |
| 快速双击 | 睡 / 醒 切换 |
| 按住拖 | 挤压 30~42% + 倾斜 + 瞳孔跟随，位移精确到像素 |
| 来回蹭 | 每 ~220px 冒一簇心，心情一路上升 |
| 长按 0.9s | 喂食：心情 +0.45、能量 +0.35 |
| 拖到边缘 | 撞边压扁 |
| 闲置 45s | 自己睡着（冷色 + 睡眠泡 + 呼吸变慢） |
| 睡着被戳 | 眼睛猛然放大 40% 惊醒 |

它还会：**跨重启记住心情/能量/亲密度**、**23:00–05:00 更容易犯困**、**改配置文件就能换皮肤**。

## 画质

| 项 | 值 |
|---|---|
| 渲染缓冲 | 1080 × 1080（写死，不随宠物尺寸变） |
| 上屏尺寸 | 525 × 525（150dp @ 560dpi） |
| 超采样 | 2.06×（SurfaceFlinger 硬件缩放） |
| 帧率 | idle 15fps / 活跃 30fps / 灭屏 0fps |
| 单帧耗时 | ~18ms（@66ms 周期，渲染线程只忙 27%） |

## 安装

```
1. KernelSU / Magisk 刷入 petpal-module.zip
2. 重启
3. 桌面会出现宠物（service.sh 自动拉起）
```

`service.sh` 会自动完成三件必要的事（少一样就跑不起来）：

```sh
appops set pet.runtime SYSTEM_ALERT_WINDOW allow     # 悬浮窗
pm grant  pet.runtime POST_NOTIFICATIONS             # 通知可见（Android 13+）
dumpsys deviceidle whitelist +pet.runtime            # 防国产 ROM 冻结进程
```

## 控制

```sh
/data/adb/modules/petpal/ctl.sh status     # 进程 / 心情 / 能量 / 亲密度 / 皮肤列表
/data/adb/modules/petpal/ctl.sh skin       # 切下一个皮肤
/data/adb/modules/petpal/ctl.sh skin:fox   # 切指定皮肤
/data/adb/modules/petpal/ctl.sh pause      # 停渲染
/data/adb/modules/petpal/ctl.sh resume     # 恢复
```

底层是文件通道（C 每帧轮询，最可靠）：

```sh
echo "skin:fox" > /data/data/pet.runtime/files/ctl
echo "pause"    > /data/data/pet.runtime/files/ctl
```

## 皮肤

改配置文件即生效，**不用重装 APK**（热重载，1 秒内自动刷新）：

`/data/data/pet.runtime/files/skin.cfg`

```ini
body=255,140,80    # 身体颜色 RGB
ear=2              # 0=无耳 1=圆耳 2=尖耳
tail=1             # 0=无尾 1=卷尾
ahoge=0            # 0=无呆毛 1=有
size=1.05          # 整体缩放 0.55~1.45
```

预设放在 `files/skins/*.cfg`，用 `ctl.sh skin` 循环切换。内置示例：

| 皮肤 | 配置 |
|---|---|
| 团子猫（默认） | `body=214,190,175 ear=1 tail=0 ahoge=1 size=1.0` |
| 狐狸 | `body=255,140,80 ear=2 tail=1 ahoge=0 size=1.05` |
| 史莱姆 | `body=150,220,190 ear=0 tail=0 ahoge=0 size=0.95` |

## 目录结构

```
module/                   KSU 模块（打包成 zip 刷入）
  module.prop
  service.sh              开机预置 + 拉起 + 30s 看门狗
  ctl.sh                  控制脚本（文件通道）
  webroot/index.html      WebUI 页面（需支持 ksu.exec 的管理器）
src/
  pethost.c               渲染核心（~1080 行，C99，-Wall -Wextra 零警告）
  apk_petpal.apk          成品 APK（内含 smali，可用 MT 反编译查看）
tools/
  smoke.sh                6 段断言回归脚本
release/petpal.apk        同上
```

## v1.5 修了什么

| 项 | v1.4 | v1.5 |
|---|---|---|
| 线程安全 | `P` 与 `gTxt/gPaused` 全裸访问，`nativeBubble` 换缓冲时渲染线程可能读到已 free 的内存 | 一把 `gMutex` 护全部跳线程状态；渲染整帧持锁，JNI 侧短临界区 |
| 版本号 | zip=v1.4 / `service.sh` `VER=v1.2` / `module.prop` v1.0 | 统一从 `module.prop` 读，`versionCode=5` |
| 升级跳装 | `installed.ver` 与写死的 `VER` 相等就跳过安装 | `pm install` 校验返回码 + 3 次重试；刷模块时清掉旧标记 |
| 渲染线程 | `nativeStop` 只 release window，线程活到进程被杀 | `gRun=0` + 退出等待，下次 `nativeSurface` join 重建 |
| 控制通道 | `ctl` 读完不清，每帧重复执行同一条命令 | 读即消费（`unlink`），`ctl.sh` 改 `.tmp+mv` 原子写入 |
| 皮肤切换 | C 侧 `readdir` 顺序随机，与 `ctl.sh` 不一致 | `qsort` + `ls\|sort` 对齐 |
| 边缘检测 | 用「拖过的最大坐标」猜屏幕，旋转/分屏后失效 | `service.sh` 开机写 `files/screen.cfg`（`wm size`），真分辨率判定 + 6% 迟滞 |
| 开机时序 | 写死 `sleep 20` 后立即装 | 等 `sys.boot_completed` + `pm` 就绪（≤180s）|
| 看门狗 | `while true` 无 pid 文件，可能堆积多份 | pidfile + `boot_id` 去重 |
| 呆毛弹簧 | 固定 `dt=0.033`，速度随帧率漂移 | 用真实帧间隔 |
| 输出/状态 | `gTxt` 不释放、`state.cfg` 非原子写 | `nativeStop` 释放；`.tmp+rename` 发布 |
| dex | 残留 3 个 `com.android.pacprocessor.PacService*` 死类 | 已从 `classes.dex` 删除 |
| WebUI | 路径写死 `/data/adb/modules/petpal`、无停止按钮、连点并发 | 现找 `id=petpal` 模块、加停止按钮、按钮组防抖、错误标红 |

> 顺手修掉的还有：`switch` 里裸声明变量（C23 扩展，改 `{}`）、`gVelX/gVelY/gFling` 死代码。
> `README` 里「22:00–05:00 更容易犯困」与代码不符（实际 `23:00`），已按代码修正。

## 已知限制（实测确认，不是没做）

| 限制 | 原因 |
|---|---|
| **惯性抛掷做不到** | overlay 窗口的异步位置更新被 ColorOS 静默丢弃 |
| ~~通知栏按钮~~ | 已移除（点击被 ROM 拦，改用 WebUI） |
| ~~快捷磁贴~~ | 已移除（不进编辑面板） |
| ~~WebUI 按钮无反应~~ | 已修复（HTML 转义导致按钮失效） |

## 技术笔记（都能省你很多时间）

1. **`ASurfaceControl_create(NULL, …)` 不可用** —— NDK 强制要求 parent，直接 abort。纯 C 无 APK 建悬浮层这条路是死的。
2. **app 进程不能 dlopen 系统私有库** —— `libft2.so` 被 linker namespace 拒绝，中文渲染只能走系统文本栈（Canvas → int[] → JNI）。
3. **`LayoutParams.horizontalMargin` 不能当草稿纸** —— WMS 真的会读，写非法值会让窗口进入 invalid 状态，之后所有 `updateViewLayout` 失效。
4. **JNI 符号名不含签名** —— 改参数表必须同步改 `.method native` 声明，否则参数错位、native 层直接崩且 Java 侧看不到异常。
5. **MT dexer 对新字段写入不可靠** —— 静态/实例字段都有问题，改用构造参数注入或复用原有字段。
6. **弹簧积分要控 `k·dt²`** —— 15fps 下 `k=90` 已接近不稳定，必须子步积分（4 段）+ 收敛吸附。
7. **阈值附近必抖** —— 用双阈值迟滞（进 3px / 退 80px），不是调参数调到不抖。
8. **国产 ROM 会冻结后台进程** —— 即使有前台服务也会被 Hans freeze manager 冻住，广播投递不了，必须加 Doze 白名单。
9. **`RECEIVER_NOT_EXPORTED` 会堵死自己的调试通道** —— 调试期先 `exported=true`，跑通再收紧。
10. **先量后改** —— 在 `sqrtf`、弹簧、脏区重绘上各浪费一轮，都是没量就改。加日志比猜快十倍。
11. **别用「拖过的最大坐标」当屏幕尺寸** —— 旋转、分屏、或者从未拖到过右下角，它都会失真。root 侧开机跑一次 `wm size` 写进 `files/screen.cfg` 最省事。
12. **共享状态就得一把锁** —— 软光栅是 CPU 密集的，但一帧只有十几毫秒；整帧持锁的代价远小于 UAF 和逐字符发灰。

## 作者

**DeepSeek**（AI 助手）—— 架构设计、C 渲染核心、smali 胶水层、平台探测、调试与验证全部由 AI 完成。

开发环境：**全程在一部 Android 手机上完成**，通过 MT 管理器的 MCP 接口写代码、Termux clang 编译、模块自带脚本装机验证，未使用电脑。
从零到可运行在一个会话内完成，期间实测探明 10 项平台限制。

## 签名与发布

本仓库**公开自带签名密钥** `tools/signing/petpal.jks`（别名 `petpal`，口令 `petpal`）。
这是刻意的取舍：换取「无 secrets、一条 `git tag` 就能出正式包」。

- 好处：任何人都能复现出与你一致签名的 APK，CI 与本机构建可以互换
- 代价：**密钥是公开的**，别人也能签出同名同包名的包。
  请只从 [Releases](https://github.com/cheems-boop/petpal/releases) 下载安装包

> 想换私钥：把新 `.jks` 拷到同路径，或设 `KEYSTORE` / `KEY_PASS` / `KEY_ALIAS` 环境变量覆盖，
> CI 里改 `apksigner` 那行即可。换钥会让已装用户必须卸载重装一次。

发布：

```sh
git tag v1.6.0 && git push origin v1.6.0     # CI 出包并自动建 Release
```

## 许可

MIT
