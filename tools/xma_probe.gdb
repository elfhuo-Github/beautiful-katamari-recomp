set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $nw = 0
set $nr = 0
set $nd = 0
set $nc = 0

break rex::audio::XmaDecoder::WriteRegister
commands
  silent
  set $nw = $nw + 1
  if $nw <= 80
    printf "[XMA WR] reg=0x%03x val=0x%08x\n", $rsi, $rdx
  end
  continue
end

break rex::audio::XmaDecoder::ReadRegister
commands
  silent
  set $nr = $nr + 1
  if $nr <= 80
    printf "[XMA RD] reg=0x%03x\n", $rsi
  end
  continue
end

break rex::audio::XmaContext::Decode
commands
  silent
  set $nd = $nd + 1
  if $nd <= 6
    printf "[DECODE#%d] data=%p\n", $nd, $rsi
    printf "   d0=%08x d1=%08x d2=%08x d3=%08x d4=%08x\n", *(unsigned int*)$rsi, *(unsigned int*)($rsi+4), *(unsigned int*)($rsi+8), *(unsigned int*)($rsi+12), *(unsigned int*)($rsi+16)
    printf "   in0=%08x in1=%08x OUT=%08x work=%08x d9=%08x\n", *(unsigned int*)($rsi+20), *(unsigned int*)($rsi+24), *(unsigned int*)($rsi+28), *(unsigned int*)($rsi+32), *(unsigned int*)($rsi+36)
  end
  continue
end

break rex::audio::XmaContext::ConvertFrame
commands
  silent
  set $nc = $nc + 1
  if $nc <= 6
    printf "[CONV#%d] outbuf=%p stereo=%d\n", $nc, $rdx, $rsi
  end
  continue
end

run
printf "\n=== wr=%d rd=%d decode=%d conv=%d ===\n", $nw, $nr, $nd, $nc
quit
