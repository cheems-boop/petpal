#!/data/data/com.termux/files/usr/bin/bash
# petpal 构建脚本（on-device / Termux）
#
# 依赖（都在 Termux 里，pkg/apt 装不了时可用 curl+dpkg，见 docs/build.md）：
#   clang  aapt2  d8  apksigner  openjdk  python3
#
# 产物：
#   build/petpal.apk                已签名、可直接 pm install
#   build/petpal-module.zip         KSU/Magisk 刷机包
#
# 关键设计：
#   - 版本号单一真源 = module/module.prop（manifest 里故意不写）
#   - 框架资源用机上 /system/framework/framework-res.apk，不需要 Android SDK
#   - libpethost.so 现场用 clang 编，不存二进制产物

set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
OUT=$ROOT/build
PREFIX=${PREFIX:-/data/data/com.termux/files/usr}
CLANG=${CLANG:-$PREFIX/bin/clang}
AAPT2=${AAPT2:-$PREFIX/bin/aapt2}
APKSIGNER=${APKSIGNER:-$PREFIX/bin/apksigner}
KEYTOOL=${KEYTOOL:-$PREFIX/bin/keytool}
PY=${PY:-$PREFIX/bin/python3}
FRAMEWORK=${FRAMEWORK:-/system/framework/framework-res.apk}
# keystore 必须放在 build/ 之外：build/ 每次构建都会被清空，
# 否则每次都会重生一把密钥，签名漂移、无法覆盖升级
KEYSTORE=${KEYSTORE:-$ROOT/.keystore/petpal.keystore}
KEY_ALIAS=${KEY_ALIAS:-petpal}
KEY_PASS=${KEY_PASS:-petpal-dev}

need() { [ -x "$1" ] || { echo "缺少工具: $1"; exit 1; }; }
need "$CLANG"; need "$AAPT2"; need "$APKSIGNER"; need "$PY"
[ -f "$FRAMEWORK" ] || { echo "缺少框架资源: $FRAMEWORK（需要 root 可读）"; exit 1; }

VER_NAME=$(sed -n 's/^version=v//p' module/module.prop | head -1)
VER_CODE=$(sed -n 's/^versionCode=//p' module/module.prop | head -1)
[ -n "${VER_NAME:-}" ] || { echo "module.prop 缺 version="; exit 1; }
[ -n "${VER_CODE:-}" ] || { echo "module.prop 缺 versionCode="; exit 1; }
VC=$((10000 + VER_CODE))            # 10000+ 避开历史骨架版本号

echo "== 版本 v$VER_NAME / versionCode $VC =="
rm -rf "$OUT"; mkdir -p "$OUT/pkg"

# 1) native
echo "== 1/5 编译 libpethost.so =="
$CLANG -O2 -fPIC -shared -Wall -Wextra -Wno-unused-parameter \
  -o "$OUT/libpethost.so" src/pethost.c -llog -landroid -lm
ls -l "$OUT/libpethost.so"

# 2) dex
echo "== 2/5 生成 classes.dex =="
if [ -f src/android/java/pet/runtime/Main.java ]; then
  echo "   从 Java 源码编译"
  need "$PREFIX/bin/javac"; need "$PREFIX/bin/d8"
  mkdir -p "$OUT/classes"
  "$PREFIX/bin/javac" -source 11 -target 11 -nowarn \
    -cp "$FRAMEWORK" -d "$OUT/classes" src/android/java/pet/runtime/*.java
  "$PREFIX/bin/d8" --min-api 28 --output "$OUT" $(find "$OUT/classes" -name '*.class')
else
  echo "   用预编译字节码 src/android/dex/classes.dex"
  cp src/android/dex/classes.dex "$OUT/classes.dex"
fi
ls -l "$OUT/classes.dex"

# 3) resources + manifest
echo "== 3/5 aapt2 link =="
$AAPT2 link -o "$OUT/base.apk" \
  -I "$FRAMEWORK" \
  --manifest src/android/AndroidManifest.xml \
  --version-code "$VC" --version-name "$VER_NAME" \
  --min-sdk-version 28 --target-sdk-version 36

# 4) 塞 dex + so + 签名
echo "== 4/5 打包与签名 =="
"$PY" - "$OUT/base.apk" "$OUT/classes.dex" "$OUT/libpethost.so" <<'PYEOF'
import sys, zipfile, os
apk, dex, so = sys.argv[1], sys.argv[2], sys.argv[3]
with zipfile.ZipFile(apk, 'a', zipfile.ZIP_DEFLATED) as z:
    z.write(dex, 'classes.dex')
    z.write(so, 'lib/arm64-v8a/libpethost.so')
print('apk entries:', zipfile.ZipFile(apk).namelist())
PYEOF

mkdir -p "$(dirname "$KEYSTORE")"
if [ ! -f "$KEYSTORE" ]; then
  echo "   首次构建，生成调试 keystore（发布请用 tools/gen-keystore.sh + secrets）"
  "$KEYTOOL" -genkeypair -keystore "$KEYSTORE" -alias "$KEY_ALIAS" \
    -keyalg RSA -keysize 2048 -validity 10000 \
    -storepass "$KEY_PASS" -keypass "$KEY_PASS" \
    -dname "CN=petpal, OU=dev, O=petpal, L=NA, C=CN" >/dev/null 2>&1
fi
$APKSIGNER sign --ks "$KEYSTORE" --ks-key-alias "$KEY_ALIAS" \
  --ks-pass "pass:$KEY_PASS" --key-pass "pass:$KEY_PASS" \
  --out "$OUT/petpal.apk" "$OUT/base.apk"
$APKSIGNER verify --print-certs "$OUT/petpal.apk" | head -4

# 5) 模块 zip
echo "== 5/5 打模块包 =="
mkdir -p "$OUT/pkg"
cp -f module/module.prop module/service.sh module/ctl.sh module/customize.sh "$OUT/pkg/"
cp -f "$OUT/petpal.apk" "$OUT/pkg/petpal.apk"
cp -a module/skins module/webroot "$OUT/pkg/"
"$PY" - "$ROOT/build/petpal-module.zip" "$OUT/pkg" <<'PYEOF'
import sys, zipfile, os
out, src = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for base, _, files in os.walk(src):
        for f in files:
            p = os.path.join(base, f)
            z.write(p, os.path.relpath(p, src))
print('module zip:', zipfile.ZipFile(out).namelist())
PYEOF

echo
ls -l "$OUT/petpal.apk" "$OUT/petpal-module.zip"
echo "=== 完成：build/petpal.apk + build/petpal-module.zip ==="
