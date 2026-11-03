#!/data/data/com.termux/files/usr/bin/bash
# 生成 release keystore（只在本地跑一次，然后转 base64 存进 GitHub Secrets）
#
# 用法：
#   sh tools/gen-keystore.sh            # 产出 build/petpal-release.keystore
#   base64 -w0 build/petpal-release.keystore > ks.b64
#   把 ks.b64 内容存为 secret: KEYSTORE_BASE64
#   另存两个 secret: KEY_ALIAS / KEY_PASS
#
# 千万不要把 keystore 提交进仓库（.gitignore 已排除 *.keystore）。
set -euo pipefail
cd "$(dirname "$0")/.."
PREFIX=${PREFIX:-/data/data/com.termux/files/usr}
KEYTOOL=${KEYTOOL:-$PREFIX/bin/keytool}
OUT=${OUT:-build/petpal-release.keystore}
ALIAS=${KEY_ALIAS:-petpal}
PASS=${KEY_PASS:-}
DN=${KEY_DN:-"CN=petpal, OU=dev, O=petpal, C=CN"}

[ -x "$KEYTOOL" ] || { echo "缺少 keytool: $KEYTOOL"; exit 1; }
if [ -z "$PASS" ]; then
  printf '输入 keystore 口令（至少 6 位）: '
  stty -echo; read -r PASS; stty echo; echo
fi
mkdir -p "$(dirname "$OUT")"
[ -f "$OUT" ] && { echo "已存在: $OUT（不覆盖）"; exit 1; }

"$KEYTOOL" -genkeypair \
  -keystore "$OUT" -alias "$ALIAS" \
  -keyalg RSA -keysize 4096 -validity 10000 \
  -storepass "$PASS" -keypass "$PASS" -dname "$DN"

"$KEYTOOL" -list -v -keystore "$OUT" -storepass "$PASS" | grep -E 'Alias|SHA256|Valid'
echo
echo "下一步："
echo "  base64 -w0 $OUT > ks.b64    # 内容 → GitHub secret KEYSTORE_BASE64"
echo "  并设置 secrets: KEY_ALIAS=$ALIAS  KEY_PASS=<你的口令>"
echo "  然后用: KEYSTORE=$OUT sh tools/build.sh   本地做一次 release 签名验证"
