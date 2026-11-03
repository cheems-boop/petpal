# 开源决策记录（ADOPTED）

本文件记录 petpal 开源时的几个关键决定和理由，避免以后又走回头路。

## 1. 构建：自建 manifest，彻底脱掉系统骨架

v1.5 及以前的 APK 是用 MT 管理器从系统应用模板（`com.android.pacprocessor` 等）崩出来的，
manifest 里残留 `versionCode=36 / versionName=16`、`theme @01030010`、
`defaultToDeviceProtectedStorage`、`directBootAware`，dex 里还曾跟着三个 `PacService` 死类（v1.5 已删）。

这类产物：
- 不是自己的版权物（AOSP 的 manifest / 资源），挂在 MIT 仓库下不合适
- 版本号不可控（应用版本 36 与模块版本 v1.5 两套体系）
- 别人拿到源码也复现不出 APK——那是“源码可见”，不是开源

所以 v1.6 起：`src/android/AndroidManifest.xml` 自写，`res/` 自备，CI 用
`aapt2 + smali/d8 + apksigner` 产 APK。

## 2. 签名：用自己的 keystore

仓库是新建的、**没有存量用户**，所以不存在“升级要卸载重装”的成本，
直接切自己的 release keystore（只放 GitHub Actions secrets，不进仓库）。

- `versionCode / versionName` 由构建参数注入，单一真源是 `module/module.prop`
- `tools/gen-keystore.sh` 生成密钥；CI 用 `KEYSTORE_BASE64 / KEY_ALIAS / KEY_PASS` 三个 secret
- 本地调试用 debug key，不参与发布

## 3. 开源前必须补的三个洞

1. **`CtlRx` 的 `exported=true`**（v1.5）：任何 App 发 `pet.ctl` 广播就能暂停/换肤。
   而且它的逻辑是“无 extra 就 resume”，等于一个可被外部触发的开关。→ 改 `exported=false`。
2. **版本号三源归一**：`module.prop` 为唯一真源，manifest 由构建注入；CI 校验
   `module.prop` / `CHANGELOG.md` / git tag 三者一致。
3. **`res/` 是骨架的**：`@7F010000`(app label)、`@01030010`(theme) 必须换成自己的，
   否则脱骨架后构建直接断。

## 4. 文档定位

`README` 只解决“是什么 / 怎么装 / 怎么用 / 怎么改”；`docs/tech-notes.md` 放那 12 条
平台踩坑（软光栅、`ASurfaceControl_create` 拿不到、`libft2.so` 被 linker namespace 拒、
MT dexer 写字段不可靠……）——这部分才是本项目真正的差异化内容。
