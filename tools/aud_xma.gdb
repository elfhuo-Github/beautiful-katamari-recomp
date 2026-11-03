set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $n = 0
set $dec = 0
set $first = 0

break __imp__XAudioSubmitRenderDriverFrame
commands
  silent
  set $n = $n + 1
  if $n == 1
    shell echo "SUBMIT hit1 $(date +%s.%N)"
  end
  continue
end

break rex::audio::XmaContext::DecodePacket
commands
  silent
  set $dec = $dec + 1
  if $dec == 1
    set $first = $n
    shell echo "XMA first decode at submit-hit $n $(date +%s.%N)"
  end
  if $dec % 200 == 0
    shell echo "XMA decode count $dec at submit-hit $n $(date +%s.%N)"
  end
  continue
end

run
printf "\n=== SUMMARY: submits=%d xma_decodes=%d ===\n", $n, $dec
quit
