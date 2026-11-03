set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $n = 0
set $m = 0

break __imp__XAudioSubmitRenderDriverFrame
commands
  silent
  set $n = $n + 1
  if $n == 1
    printf "SUBMIT ctx=%p  r3=0x%x r4=0x%x r5=0x%x  arg2=0x%x\n", $rdi, *(unsigned int*)($rdi+0), *(unsigned int*)($rdi+32), *(unsigned int*)($rdi+40), $rsi
  end
  set $g = *(unsigned int*)($rdi + 32)
  set $h = $g + 0x100000000
  append binary memory /tmp/opencode/gs.bin $h ($h + 6144)
  if $n > 600
    quit
  end
  continue
end

break SDL_PutAudioStreamData
commands
  silent
  set $m = $m + 1
  set $p = $rsi
  append binary memory /tmp/opencode/ps.bin $p ($p + 6144)
  if $m > 1200
    quit
  end
  continue
end

run
printf "\n=== SUMMARY submits=%d puts=%d ===\n", $n, $m
quit
