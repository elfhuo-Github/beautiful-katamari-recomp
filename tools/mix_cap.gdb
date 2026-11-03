set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $fr = 0
set $nm = 0

break __imp__XAudioSubmitRenderDriverFrame
commands
  silent
  set $fr = $fr + 1
  if $fr == 1490
    shell rm -f /tmp/opencode/m_p3.bin /tmp/opencode/m_p4.bin /tmp/opencode/m_coef.bin /tmp/opencode/render.bin
  end
  if $fr >= 1490 && $fr <= 1505
    append binary memory /tmp/opencode/render.bin 0x14001FA00 (0x14001FA00 + 6144)
  end
  continue
end

break sub_82539960
commands
  silent
  set $nm = $nm + 1
  set $g3 = *(unsigned int*)($rdi + 0)
  set $g4 = *(unsigned int*)($rdi + 32)
  set $g5 = *(unsigned int*)($rdi + 40)
  if $fr >= 1490 && $fr <= 1505
    set $h3 = $g3 + 0x100000000
    set $h4 = $g4 + 0x100000000
    set $h5 = $g5 + 0x100000000
    printf "MIX call=%d fr=%d p3=0x%x p4=0x%x k5=0x%x\n", $nm, $fr, $g3, $g4, $g5
    append binary memory /tmp/opencode/m_p3.bin $h3 ($h3 + 1024)
    append binary memory /tmp/opencode/m_p4.bin $h4 ($h4 + 1024)
    append binary memory /tmp/opencode/m_coef.bin $h5 ($h5 + 32)
  end
  continue
end

run
printf "\n=== frames=%d mixer_calls=%d ===\n", $fr, $nm
quit
