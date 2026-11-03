#!/system/bin/sh
# 桌面宠物 petpal：安装 APK + 环境预置 + 屏幕尺寸 + 拉起 + 看门狗
# v1.5: 版本号不再写死（读 module.prop）、pm 结果真校验 + 重试、看门狗 pidfile 去重
MODDIR=${0%/*}
PKG=pet.runtime
AM=/system/bin/am
PM=/system/bin/pm
F=/data/data/$PKG/files
LOG=$MODDIR/petpal.log
VER=$(sed -n 's/^version=//p' "$MODDIR/module.prop" 2>/dev/null | head -1)
[ -n "$VER" ] || VER=unknown
BOOTID=$(cat /proc/sys/kernel/random/boot_id 2>/dev/null)

log() { echo "$(date '+%m-%d %H:%M:%S') $*" >> "$LOG"; }

# 0) 等系统真的起来（boot_completed + pm 可用），最多 180s。
#    不再写死 sleep 20：快设备不用白等，慢设备也不会在 pm 未就绪时装失败
i=0
while [ $i -lt 180 ]; do
  [ "$(getprop sys.boot_completed)" = "1" ] && $PM path android >/dev/null 2>&1 && break
  sleep 1; i=$((i+1))
done
[ $i -ge 180 ] && log "boot wait timeout, continue anyway"
sleep 3

# 1) 安装 / 升级自带 APK：版本标记 + pm 返回码双重校验
if [ "$(cat "$MODDIR/installed.ver" 2>/dev/null)" != "$VER" ] && [ -f "$MODDIR/petpal.apk" ]; then
  ok=0
  for try in 1 2 3; do
    out=$($PM install -r -d "$MODDIR/petpal.apk" 2>&1)
    case "$out" in
      *Success*) ok=1; break;;
      *) log "install try $try failed: $out"; sleep $((try*3));;
    esac
  done
  if [ $ok = 1 ]; then
    echo "$VER" > "$MODDIR/installed.ver"
    log "apk installed $VER"
  else
    log "apk install FAILED, retry next boot"
  fi
fi

# 2) 环境预置（缺一样就用不了）
/system/bin/appops set $PKG SYSTEM_ALERT_WINDOW allow 2>/dev/null
$PM grant $PKG android.permission.POST_NOTIFICATIONS 2>/dev/null
/system/bin/dumpsys deviceidle whitelist +$PKG >/dev/null 2>&1

# 3) files/ 目录：预设皮肤 + 真实屏幕尺寸（v1.5 边缘检测用）
if [ -d /data/data/$PKG ]; then
  mkdir -p $F/skins
  for s in cat fox slime; do
    [ -f "$MODDIR/skins/$s.cfg" ] || continue
    [ -f "$F/skins/$s.cfg" ] && continue
    cp -f "$MODDIR/skins/$s.cfg" "$F/skins/$s.cfg"
  done
  SZ=$(/system/bin/wm size 2>/dev/null | sed -n 's/^[[:space:]]*[A-Za-z ]*size:[[:space:]]*\([0-9]*x[0-9]*\).*/\1/p' | tail -1)
  [ -n "$SZ" ] && { echo "$SZ" > "$F/.screen.tmp" && mv -f "$F/.screen.tmp" "$F/screen.cfg"; }
  U=$(stat -c %u /data/data/$PKG 2>/dev/null)
  [ -n "$U" ] && chown -R $U:$U $F 2>/dev/null
  log "files ready, screen=${SZ:-unknown}"
fi

# 4) 拉起宠物（用户主动 stop 过则尊重）
disabled() { [ -f "$F/disabled" ]; }
disabled || $AM start -n $PKG/.Main >/dev/null 2>&1

# 5) 看门狗：pidfile + boot_id 去重，service.sh 被多刷几次也不会堆出多份循环
[ -f "$MODDIR/watchdog.pid" ] && read WPID WBOOT < "$MODDIR/watchdog.pid"
if [ -n "$WPID" ] && [ "$WBOOT" = "$BOOTID" ] && kill -0 "$WPID" 2>/dev/null; then
  log "watchdog already running ($WPID)"
else
  (
    while true; do
      sleep 30
      disabled && continue
      /system/bin/pidof -s $PKG >/dev/null 2>&1 && continue
      /system/bin/dumpsys deviceidle whitelist +$PKG >/dev/null 2>&1
      $AM start -n $PKG/.Main >/dev/null 2>&1
      log "watchdog restarted pet"
    done
  ) &
  echo "$! $BOOTID" > "$MODDIR/watchdog.pid"
fi
