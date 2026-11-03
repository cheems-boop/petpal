#!/system/bin/sh
# smoke.sh -- 宠物冒烟自测（模块开发期用，可在 MT 终端直接跑）
AM=/system/bin/am
P=${PREFIX:-/data/data/com.termux/files/usr}/bin
SET=/system/bin/settings
# v1.5: 不再写死开发机路径，均可用环境变量覆盖
APK=${APK:-/storage/emulated/0/MT2/mcp/petpal.apk}
MODDIR=${MODDIR:-/data/adb/modules/petpal}
FS=/data/data/pet.runtime/files
AR=$($SET get system accelerometer_rotation 2>/dev/null)
UR=$($SET get system user_rotation 2>/dev/null)
cropdiff() {
$P/python3 - "$1" "$2" $3 <<'PYEOF'
import sys,struct
def rd(f):
    d=open(f,'rb').read()
    w,h,_,_=struct.unpack('<4I',d[:16])
    return w,h,d[16:]
w1,h1,p1=rd(sys.argv[1]); w2,h2,p2=rd(sys.argv[2])
if (w1,h1)!=(w2,h2): print(-1); sys.exit(0)
x1,y1,x2,y2=[int(v) for v in sys.argv[3].split(',')]
x1=max(0,x1); y1=max(0,y1); x2=min(w1-1,x2); y2=min(h1-1,y2)
st=w1*4; n=0
for y in range(y1,y2+1,2):
    b=y*st
    for x in range(x1,x2+1,2):
        i=b+x*4
        if abs(p1[i]-p2[i])+abs(p1[i+1]-p2[i+1])+abs(p1[i+2]-p2[i+2])>8: n+=1
print(n)
PYEOF
}
rectof() {
$P/python3 - "$1" <<'PYEOF'
import sys,re
v=[int(x) for x in re.findall(r'[0-9]+', sys.argv[1])]
print('%d,%d,%d,%d' % (v[0],v[1],v[2],v[3]))
PYEOF
}
centerof() {
$P/python3 - "$1" <<'PYEOF'
import sys,re
v=[int(x) for x in re.findall(r'[0-9]+', sys.argv[1])]
print('%d %d' % ((v[0]+v[2])//2,(v[1]+v[3])//2))
PYEOF
}
patchof() {
$P/python3 - "$1" <<'PYEOF'
import sys,re
v=[int(x) for x in re.findall(r'[0-9]+', sys.argv[1])]
x1,y1,x2,y2=v[0],v[1],v[2],v[3]
w=x2-x1; h=y2-y1
cx=(x1+x2)//2; cy=y1+int(h*0.62)
print('%d,%d,%d,%d' % (cx-10,cy-10,cx+10,cy+10))
PYEOF
}
fc() {
$P/python3 - "$(logcat -d -s PetPal:V 2>/dev/null | grep -o 'f=[0-9]*' | tail -1)" <<'PYEOF'
import sys
s=sys.argv[1]
print(int(s.split('=')[1]) if '=' in s else 0)
PYEOF
}
frameof() {
dumpsys window windows 2>/dev/null | grep -A45 'Window{.*pet.runtime}' | grep -oE 'frame=\[[0-9]+,[0-9]+\]\[[0-9]+,[0-9]+\]' | head -1
}
$AM force-stop pet.runtime
[ -f "$APK" ] || { echo "APK 不存在: $APK  （用 APK=/path/to/petpal.apk 指定）"; exit 1; }
cp -f "$APK" /data/local/tmp/petpal.apk && chmod 644 /data/local/tmp/petpal.apk
pm install -r -d /data/local/tmp/petpal.apk 2>&1 | grep -viE '^\s+at ' | tail -2
appops set pet.runtime SYSTEM_ALERT_WINDOW allow
U=$(stat -c %u /data/data/pet.runtime)
mkdir -p /data/data/pet.runtime/files
touch /data/data/pet.runtime/files/debug
chown -R $U:$U /data/data/pet.runtime/files
logcat -c 2>/dev/null
$AM start -n pet.runtime/.Main >/dev/null 2>&1
sleep 3
FR=$(frameof)
if [ -z "$FR" ]; then echo 'NO_WINDOW - app dead?'; logcat -d -b crash 2>/dev/null | grep -m1 'Caused by'; exit 1; fi
R=$(rectof "$FR")
echo "A. frame=$FR"
logcat -d -s PetPal:V 2>/dev/null | grep -E 'verbose=|nativeSurface' | tail -2
F1=$(fc); sleep 3; F2=$(fc)
echo "B. idle_frames=$(( F2 - F1 )) (应>0)"
echo pause > /data/data/pet.runtime/files/ctl
sleep 1
F3=$(fc); sleep 3; F4=$(fc)
echo "C. paused_frames=$(( F4 - F3 )) (应为0)"
echo resume > /data/data/pet.runtime/files/ctl
sleep 1
F5=$(fc); sleep 3; F6=$(fc)
echo "   resumed_frames=$(( F6 - F5 )) (应>0)"
logcat -d -s PetPal:V 2>/dev/null | grep -E 'pause|resume' | tail -2
echo '== D. drag bottom -> rotate -> back =='
C=$(centerof "$FR")
/system/bin/input swipe ${C% *} ${C#* } ${C% *} 2350 400
sleep 1
FRB=$(frameof); echo "   before=$FRB"
$SET put system accelerometer_rotation 0
$SET put system user_rotation 1
sleep 3
FRL=$(frameof); echo "   land  =$FRL"
$SET put system user_rotation $UR
$SET put system accelerometer_rotation $AR
sleep 3
FRA=$(frameof); echo "   after =$FRA"
echo '== F. 边缘检测（v1.5 改用真实屏幕尺寸） =='
echo "   screen.cfg = $(cat $FS/screen.cfg 2>/dev/null || echo '(缺失，会退回启发式)')"
logcat -d -s PetPal:V 2>/dev/null | grep -E 'nativeInit v1.5' | tail -1
# 说明：惯性抛掷测试已移除 —— 该特性 v1.4 就没实现，v1.5 把 gFling/gVelX/gVelY 死代码清掉了
echo '== E. tap -> bubble =='
FRNOW=$(frameof)
C=$(centerof "$FRNOW")
/system/bin/input tap ${C% *} ${C#* }
sleep 1
logcat -d -s PetPal:V 2>/dev/null | grep -E 'bubble' | tail -1
RA=$(rectof "$FRNOW")
screencap /data/local/tmp/f.raw
$P/python3 - /data/local/tmp/f.raw $RA <<'PYEOF'
import sys,struct
d=open(sys.argv[1],'rb').read()
w,h,_,_=struct.unpack('<4I',d[:16]); p=d[16:]; st=w*4
x1,y1,x2,y2=[int(v) for v in sys.argv[2].split(',')]
bub=0; body=0
for y in range(max(0,y1-220),min(h-1,y2+1),2):
    b=y*st
    for x in range(max(0,x1),min(w-1,x2+1),2):
        i=b+x*4
        r,g,bl=p[i],p[i+1],p[i+2]
        if abs(r-255)<5 and abs(g-251)<5 and abs(bl-242)<5: bub+=1
        if abs(r-221)<26 and abs(g-174)<26 and abs(bl-146)<26: body+=1
print('body',body,'bubble',bub)
PYEOF
