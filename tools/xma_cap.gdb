set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $fr = 0
set $nd = 0

break rex::audio::XmaContext::Decode
commands
  silent
  set $nd = $nd + 1
  if $nd == 1
    printf "=== IN buffer @0x10ed3e800 (physical 0x0ed3e800) ===\n"
    x/16xb 0x10ed3e800
    printf "=== OUT buffer @0x10d213080 ===\n"
    x/8xw 0x10d213080
  end
  continue
end

break __imp__XAudioSubmitRenderDriverFrame
commands
  silent
  set $fr = $fr + 1
  if $fr == 1490
    shell rm -f /tmp/opencode/r2.bin /tmp/opencode/x2.bin
    shell echo "CAPSTART frame $fr"
  end
  if $fr >= 1490 && $fr <= 1505
    append binary memory /tmp/opencode/r2.bin 0x14001FA00 0x14021A00
    append binary memory /tmp/opencode/x2.bin 0x10d213080 0x10d21480
  end
  if $fr == 1505
    shell echo "CAPEND frame $fr"
    shell ls -l /tmp/opencode/r2.bin /tmp/opencode/x2.bin
  end
  continue
end

run
quit
