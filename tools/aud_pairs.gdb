set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $n = 0
set $dec = 0
set $pairs = 0

break __imp__XAudioSubmitRenderDriverFrame
commands
  silent
  set $n = $n + 1
  set $drv = *(unsigned int*)($rdi + 0)
  set $smp = *(unsigned int*)($rdi + 32)
  printf "PAIR drv=%08x smp=%08x\n", $drv, $smp
  if $n > 400
    quit
  end
  continue
end

break rex::audio::XmaContext::DecodePacket
commands
  silent
  set $dec = $dec + 1
  if $dec < 20
    printf "XMA decode #%d\n", $dec
  end
  continue
end

run
printf "\n=== SUMMARY ===\nframes submitted: %d\nxma DecodePacket calls: %d\n", $n, $dec
quit
