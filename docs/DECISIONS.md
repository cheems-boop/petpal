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

## 2. 签名：密钥直接入库（公开密钥）

仓库是新建的、**没有存量用户**，所以不存在“升级要卸载重装”的成本，
直接用一把自建 keystore，并且**把它放进仓库**：`tools/signing/petpal.jks`（别名 `petpal` / 口令 `petpal`）。

取舍：
- 得：零 secrets、零生成步骤，`git tag` 一推就能出**签名一致**的正式包；
  任何人 clone 下来都能复现同一个签名，CI 与本机构建可互换
- 失：密钥公开 ⇒ 别人也能签出同名同包名的 APK。缓解：README 首页声明“只从 Releases 下载”；
  且本项目的模块 zip 本来就只是 root 权限的普通 zip（KSU 不做签名校验），信任模型一致

其他约束：
- `versionCode / versionName` 由构建参数注入，单一真源是 `module/module.prop`
- keystore **不能放 `build/`**：build/ 每次构建被清空，会导致签名漂移、老包无法覆盖升级
- 将来想换私钥：`tools/gen-keystore.sh` 生成后換路径 / 设 `KEYSTORE` 环境变量；
  换钥会让已装用户必须卸载重装一次

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
