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
  if $n == 1500
    shell echo "CAPSTART $(date +%s.%N)"
  end
  if $n >= 1500 && $n <= 3500
    set $g = *(unsigned int*)($rdi + 32)
    set $h = $g + 0x100000000
    append binary memory /tmp/opencode/gs.bin $h ($h + 6144)
  end
  if $n == 3500
    shell echo "CAPEND $(date +%s.%N)"
    quit
  end
  continue
end

run
printf "\n=== SUMMARY submits=%d ===\n", $n
quit
