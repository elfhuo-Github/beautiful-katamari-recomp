set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $n = 0

break SDL_PutAudioStreamData
commands
  silent
  set $n = $n + 1
  if $n == 1
    shell echo "TS hit1 $(date +%s.%N)"
  end
  if $n == 2000
    set $p = $rsi
    set $q = $p + 6144
    dump binary memory /tmp/opencode/rt_02000.bin $p $q
    set $g = 0x14001FA00
    set $gq = $g + 6144
    dump binary memory /tmp/opencode/gst_02000.bin $g $gq
    shell echo "TS hit2000 $(date +%s.%N)"
  end
  if $n == 5000
    set $p = $rsi
    set $q = $p + 6144
    dump binary memory /tmp/opencode/rt_05000.bin $p $q
    set $g = 0x14001FA00
    set $gq = $g + 6144
    dump binary memory /tmp/opencode/gst_05000.bin $g $gq
    shell echo "TS hit5000 $(date +%s.%N)"
  end
  if $n == 9000
    set $p = $rsi
    set $q = $p + 6144
    dump binary memory /tmp/opencode/rt_09000.bin $p $q
    set $g = 0x14001FA00
    set $gq = $g + 6144
    dump binary memory /tmp/opencode/gst_09000.bin $g $gq
    shell echo "TS hit9000 $(date +%s.%N)"
  end
  if $n > 9200
    quit
  end
  continue
end
run
quit
