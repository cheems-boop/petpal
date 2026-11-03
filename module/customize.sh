#!/system/bin/sh
# 刷入时执行：修正文件权限 + 清掉旧版本标记（强制下次开机重装 APK）
SKIPUNZIP=0

ui_print "- 设置文件权限"
set_perm_recursive $MODPATH 0 0 0755 0644
set_perm $MODPATH/ctl.sh 0 0 0755
set_perm $MODPATH/service.sh 0 0 0755
set_perm $MODPATH/petpal.apk 0 0 0644
for f in $MODPATH/skins/*.cfg; do
  [ -e "$f" ] && set_perm $f 0 0 0644
done

# v1.5: 旧的 installed.ver / 看门狗 pid 必须清掉，否则升级后可能跳过 APK 重装
rm -f $MODPATH/installed.ver $MODPATH/watchdog.pid $MODPATH/petpal.log

ui_print "- 版本: $(sed -n 's/^version=//p' $MODPATH/module.prop)"
ui_print "- 模块文件就绪，APK 将在开机后自动安装"
