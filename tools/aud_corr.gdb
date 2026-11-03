set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $n = 0

break __imp__XAudioSubmitRenderDriverFrame
commands
  silent
  set $n = $n + 1
  if $n == 1
    shell echo "TS hit1 $(date +%s.%N)"
  end
  if $n == 2000
    set $p = *(unsigned int*)($rdi + 32) + 0x100000000
    set $q = $p + 6144
    dump binary memory /tmp/opencode/g_2000.bin $p $q
    shell echo "TS hit2000 $(date +%s.%N)"
  end
  if $n == 4000
    set $p = *(unsigned int*)($rdi + 32) + 0x100000000
    set $q = $p + 6144
    dump binary memory /tmp/opencode/g_4000.bin $p $q
    shell echo "TS hit4000 $(date +%s.%N)"
  end
  if $n == 6000
    set $p = *(unsigned int*)($rdi + 32) + 0x100000000
    set $q = $p + 6144
    dump binary memory /tmp/opencode/g_6000.bin $p $q
    shell echo "TS hit6000 $(date +%s.%N)"
  end
  if $n == 8000
    set $p = *(unsigned int*)($rdi + 32) + 0x100000000
    set $q = $p + 6144
    dump binary memory /tmp/opencode/g_8000.bin $p $q
    shell echo "TS hit8000 $(date +%s.%N)"
  end
  if $n > 8200
    quit
  end
  continue
end
run
quit
