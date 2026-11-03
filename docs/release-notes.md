# petpal v1.6.0 更新说明

> 发布日期：2026-11-04　|　仓库：https://github.com/cheems-boop/petpal
> 应用：`pet.runtime`　v1.6 / versionCode 10006

## 一句话

这一版的重点不是「新功能」，而是**把项目变成真正可复现的开源项目**，顺手修了几个一直默默不工作的路径。

## 新增

- **自建构建链**：`tools/build.sh` 一条命令出 APK + 模块 zip
  （`clang` 编 `libpethost.so` → `aapt2 link` → `javac + d8` → `apksigner`）
- **完整 Java 源码**：`Main / Host / ScreenRx` 从 dex 还原；删掉预编译 `classes.dex`，
  现在 `.java → .apk` 全链路可复现
- **CI**：GitHub Actions（NDK 编 so → 构建 → 签名 → 打 tag 自动发 Release）
- **自写 AndroidManifest**：脱掉系统骨架残留（`versionCode 36/16`、`theme @01030010`、
  `defaultToDeviceProtectedStorage`、`directBootAware`）
- `docs/DECISIONS.md`（设计取舍）、`docs/protocol.md`（文件协议 + JNI 面）

## 修复

| 问题 | 后果 |
|---|---|
| 4 处判空写反（`Host.ctl` / `onStartCommand` / `ScreenRx` / `CtlRx`） | README 宣传的「灭屏 0fps」其实不存在；`--es ctl` 指令从未生效。现已按意图修好 |
| `check_ctl()` 位置错误（v1.5 引入） | 一旦 `pause` 就再也读不到 `resume`，渲染永久停住 |
| keystore 放在 `build/` 里 | `build.sh` 开头会清空 `build/`，导致每次构建换签名、老包无法覆盖升级 |
| 删除 `CtlRx` | `exported=true` 且逻辑反了，任何 App 发 `pet.ctl` 广播就能改状态 |
| CI `versionCode` 拼接错 | `"1$CODE"` 得到 15 而不是 10005 |
| CI `javac` 缺 `-cp android.jar` | `package android.* does not exist`（构建失败） |

实测数据：`pause f=45→45`（停住）、`resume f=75→120`（恢复）、帧率 `f=60 avg 17ms`、crash 0。

## ⚠ 升级必读

- **签名换了，必须先卸载旧版**（否则 `INSTALL_FAILED_UPDATE_INCOMPATIBLE`）。
  卸载只会丢 `files/` 目录，**升级前先备份**：
  ```sh
  cp /data/data/pet.runtime/files/state.cfg /sdcard/state.cfg.bak
  ```
  装完再放回去：
  ```sh
  cp /sdcard/state.cfg.bak /data/data/pet.runtime/files/state.cfg
  chown $(stat -c %u /data/data/pet.runtime) /data/data/pet.runtime/files/state.cfg
  ```
- 版本号：`1.6 / 10006`（旧版是骨架遗留的 `16 / 36`）
- 外部广播控制已移除，改用 `ctl.sh`（文件通道）或 WebUI

## 安装

```sh
# 刷模块（KernelSU / Magisk），然后重启
# 或只装 APK：
pm install -r petpal-v1.6.apk
```

## 控制

```sh
/data/adb/modules/petpal/ctl.sh status     # 心情 / 能量 / 亲密度 / 皮肤 / 屏幕
/data/adb/modules/petpal/ctl.sh skin:fox   # 换皮肤（热重载）
/data/adb/modules/petpal/ctl.sh pause      # 停渲染（灭屏会自动停）
/data/adb/modules/petpal/ctl.sh resume     # 恢复
```

## 已知限制

- 中文渲染只能走系统文本栈（`Canvas → int[] → nativeBubble`）：app 进程 `dlopen` 系统 `libft2.so` 被 linker namespace 拒
- 惯性抛掷做不到：overlay 窗口的异步位置更新会被部分 ROM（如 ColorOS）静默丢弃

## 校验签名

```sh
apksigner verify --print-certs petpal-v1.6.apk
# SHA-256: 0ffa72b2acfcabde39494e5b635919f2eecf7e67938f592cbde61c89dd99bd89
```

签名密钥公开在仓库 `tools/signing/petpal.jks`（RSA4096，别名与口令均为 `petpal`）——
这是为了「零 secrets、`git tag` 即可发版」的取舍。**请只从 Releases 页下载安装包。**
