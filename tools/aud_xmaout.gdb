set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $n = 0

break rex::audio::XmaContext::ConvertFrame
commands
  silent
  set $n = $n + 1
  if $n == 1
    printf "rdi(samples)=%p esi(stereo)=%d rdx(out)=%p\n", $rdi, $esi, $rdx
    printf "  ch0=%p ch1=%p\n", *(void**)$rdi, *(void**)($rdi+8)
  end
  set $in0 = *(void**)$rdi
  append binary memory /tmp/opencode/xma_f0.bin $in0 ($in0 + 2048)
  if $n >= 400
    quit
  end
  continue
end

run
printf "\n=== ConvertFrame hits=%d ===\n", $n
quit
