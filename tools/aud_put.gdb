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
    shell echo "PUT hit1 $(date +%s.%N) len=$rdx"
  end
  if $n == 500
    set $p = $rsi
    set $q = $p + 6144
    dump binary memory /tmp/opencode/put_00500.bin $p $q
    shell echo "PUT hit500 $(date +%s.%N)"
  end
  if $n == 2000
    set $p = $rsi
    set $q = $p + 6144
    dump binary memory /tmp/opencode/put_02000.bin $p $q
    shell echo "PUT hit2000 $(date +%s.%N)"
  end
  if $n == 5000
    set $p = $rsi
    set $q = $p + 6144
    dump binary memory /tmp/opencode/put_05000.bin $p $q
    shell echo "PUT hit5000 $(date +%s.%N)"
  end
  if $n == 9000
    set $p = $rsi
    set $q = $p + 6144
    dump binary memory /tmp/opencode/put_09000.bin $p $q
    shell echo "PUT hit9000 $(date +%s.%N)"
  end
  if $n > 9200
    quit
  end
  continue
end
run
quit
