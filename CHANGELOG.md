# 更新说明

版本号以 `module/module.prop` 为单一真源：APK 的 `versionCode` = `10000 + versionCode`。

## [v1.6.0] - 2026-11

这一版的重点不是「新功能」，而是**把项目变成真正可复现的开源项目**，顺手修了几个一直默默不工作的路径。

### 新增

- **自建构建链**：`tools/build.sh` 一条命令出 APK + 模块 zip
  （`clang` 编 `libpethost.so` → `aapt2 link` → `javac + d8` → `apksigner`）
- **完整 Java 源码**：从 v1.5 的 dex 还原 `src/android/java/pet/runtime/{Main,Host,ScreenRx}.java`；
  删除预编译 `classes.dex`，`.java → .apk` 全链路可复现
- **CI**：GitHub Actions（NDK 编 so → 构建 → 签名 → 打 tag 自动发 Release），附 `-Werror` 门禁
- **自写 `AndroidManifest.xml`**：脱掉系统骨架残留（`versionCode 36/16`、`theme @01030010`、
  `defaultToDeviceProtectedStorage`、`directBootAware`）
- `docs/DECISIONS.md`（设计取舍与闭坑记录）、`docs/protocol.md`（文件协议 + JNI 面）

### 修复

- **判空写反导致 4 条路径从未生效**（v1.5 及以前）：`Host.ctl()`、`Host.onStartCommand()`、
  `ScreenRx.onReceive()`、`CtlRx.onReceive()` 都是 `if (x != null) return;`。
  后果：README 宣传的「灭屏 0fps」实际不存在、`--es ctl` 指令从来没生效。现已按意图修好
- **暂停后无法恢复（v1.5 引入的回归）**：`check_ctl()` 被放在 `if (gPaused) continue;` 之后，
  一旦 `pause` 就再也读不到 `resume`，渲染永久停住。改为循环顶部先处理 ctl。
  实测：`pause f=45→45`（停住） / `resume f=75→120`（恢复）
- **签名漂移**：keystore 原本放在 `build/` 里，而 `build.sh` 开头会 `rm -rf build`，
  导致每次构建重生密钥、老包无法覆盖升级。已移到 `tools/signing/petpal.jks`
- **删除 `CtlRx`**：`exported="true"` 且逻辑反了（只在「没带 extra」时踩一脚 `nativeResume()`），
  纯坑。外部控制统一走 `ctl.sh`（root 文件通道）或 WebUI
- CI 的 `versionCode` 拼接错误（`"1$CODE"` 得到 `15` 而非 `10005`）
- CI 的 `javac` 缺 `-cp android.jar`（表现为 `package android.* does not exist`）

### 变更

- **应用签名换了**：现在用仓库自带密钥 `tools/signing/petpal.jks`（RSA4096，口令 `petpal`）。
  密钥公开是刻意取舍——零 secrets、`git tag` 即可发正式包；但**从 v1.5 升级必须先卸载**
  （`INSTALL_FAILED_UPDATE_INCOMPATIBLE`）。卸载只丢 `files/` 目录，请先备份 `state.cfg`。
- 版本号归一：`versionName=1.6 / versionCode=10006`，不再跟随骨架的 36/16
- `CtlRx` 从 manifest 移除；`ScreenRx` 保持动态注册

### 已知限制

- 中文渲染只能走系统文本栈（`Canvas → int[] → nativeBubble`）：app 进程 `dlopen` 系统
  `libft2.so` 被 linker namespace 拒
- 惯性抛掷做不到：overlay 窗口的异步位置更新会被部分 ROM（如 ColorOS）静默丢弃

## [v1.5.0] - 2026-11-03

- **线程安全**：一把 `gMutex` 护 `P` 结构体与全部跳线程共享状态；渲染整帧持锁，JNI 侧短临界区；
  `nativeBubble` 换缓冲不再 UAF；`nativeStop` 置 `gRun=0` 并 join，`gTxt` 正确释放
- 版本号三处不一致修正（`service.sh` 写死 `VER=v1.2` → 从 `module.prop` 读）
- `ctl` 读即消费（`unlink`），不再每帧重复执行同一条指令
- 皮肤切换顺序确定化（C 侧 `qsort` 与 `ctl.sh` 的 `sort` 对齐）
- 边缘检测改用真实屏幕尺寸（`service.sh` 开机写 `files/screen.cfg`）
- dex 删除 3 个 `com.android.pacprocessor.PacService*` 死类
- `state.cfg` 改 `.tmp+rename` 原子发布；WebUI 动态定位模块目录、加停止按钮、按钮防抖

## [v1.0.0] - 2026-11

首个可用版：1080×1080 超采样软光栅、8 档表情、拖拽/双击睡醒/长按喂食/抚摸攒心、
皮肤热重载、文件控制通道、KernelSU 模块装机。
