#!/system/bin/sh
# 桌面宠物控制脚本（走文件通道，不依赖 am / Activity）
# v1.5: 皮肤列表显式排序、ctl 原子写入、目录缺失自愈
PKG=pet.runtime
F=/data/data/$PKG/files
AM=/system/bin/am
MODDIR=${0%/*}

get_cur() { cat $F/skins/.cur 2>/dev/null; }

# 原子写入 ctl：先写 .ctl.tmp 再 mv。native 侧“读即消费”（unlink），
# 所以这里必须用 mv，否则可能被删掉半个命令
put_ctl() {
  mkdir -p $F 2>/dev/null
  printf '%s\n' "$1" > $F/.ctl.tmp 2>/dev/null || return 1
  chmod 644 $F/.ctl.tmp 2>/dev/null
  mv -f $F/.ctl.tmp $F/ctl 2>/dev/null
}

# v1.5: 显式排序，跟 C 侧 next_skin() 的 qsort 一致
list_skins() { ls $F/skins 2>/dev/null | grep '\.cfg$' | sed 's/\.cfg$//' | LC_ALL=C sort; }

need_proc() {
  mkdir -p $F/skins 2>/dev/null
  if ! /system/bin/pidof -s $PKG >/dev/null 2>&1; then
    $AM start -n $PKG/.Main >/dev/null 2>&1
    sleep 2
    if ! /system/bin/pidof -s $PKG >/dev/null 2>&1; then
      echo "错误：宠物进程未运行，且拉起失败"
      return 1
    fi
  fi
  return 0
}

case "$1" in
  skin)
    need_proc || exit 1
    names=$(list_skins)
    [ -z "$names" ] && { echo "错误：$F/skins 里没有任何 .cfg"; exit 1; }
    cur=$(get_cur)
    nxt=$(echo "$names" | awk -v c="$cur" '{a[NR]=$0} END{if(NR==0)exit; for(i=1;i<=NR;i++){if(a[i]==c){print a[i%NR+1];exit}} print a[1]}')
    put_ctl "skin:$nxt" || { echo "错误：写 $F/ctl 失败（需要 root）"; exit 1; }
    echo "$nxt" > $F/skins/.cur
    echo "已切到: $nxt";;
  skin:*)
    need_proc || exit 1
    nm=${1#skin:}
    if [ ! -f "$F/skins/$nm.cfg" ]; then
      echo "错误：找不到皮肤 $nm"
      echo "可用: $(list_skins | tr '\n' ' ')"
      exit 1
    fi
    put_ctl "skin:$nm" || exit 1
    echo "$nm" > $F/skins/.cur
    echo "已切到: $nm";;
  pause)
    need_proc || exit 1
    put_ctl pause || exit 1
    echo "渲染已暂停";;
  resume)
    need_proc || exit 1
    put_ctl resume || exit 1
    echo "渲染已恢复";;
  start)
    mkdir -p $F/skins; rm -f $F/disabled
    need_proc && echo "宠物已启动";;
  stop)
    mkdir -p $F; touch $F/disabled
    $AM force-stop $PKG
    echo "宠物已停止（开机也不自启，用 ctl.sh start 解除）";;
  status)
    echo "== 版本 =="
    sed -n 's/^version=//p' $MODDIR/module.prop 2>/dev/null
    echo "== 运行 =="
    pid=$(/system/bin/pidof $PKG)
    [ -n "$pid" ] && echo "进程: 运行中 ($pid)" || echo "进程: 未运行"
    echo "== 心情/能量/亲密度 =="
    cat $F/state.cfg 2>/dev/null || echo "(无)"
    echo "== 当前皮肤 =="
    echo "$(get_cur)"
    echo "== 可用皮肤（排序后） =="
    list_skins | tr '\n' ' '; echo
    echo "== 屏幕（native 边缘检测用） =="
    cat $F/screen.cfg 2>/dev/null || echo "(未知，退回启发式)";;
  *) echo "用法: ctl.sh skin | skin:名字 | pause | resume | start | stop | status";;
esac
