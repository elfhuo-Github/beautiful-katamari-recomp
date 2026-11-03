set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $fr = 0
set $w = 0

break __imp__XAudioSubmitRenderDriverFrame
commands
  silent
  set $fr = $fr + 1
  continue
end

watch *(int*)0x14001FA00
commands
  silent
  set $w = $w + 1
  if $w <= 6
    printf "\n=== watch hit #%d  audio_frame=%d ===\n", $w, $fr
    bt 16
  end
  continue
end

run
printf "\n=== watch hits=%d frames=%d ===\n", $w, $fr
quit
