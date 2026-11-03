set pagination off
set confirm off
set breakpoint pending on
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $fr = 0
set $nseg = 0

break __imp__XAudioSubmitRenderDriverFrame
commands
  silent
  set $fr = $fr + 1
  continue
end

handle SIGSEGV stop print

run
printf "\n=== SIGSEGV #%d  audio_frame=%d ===\n", $nseg, $fr
set $nseg = $nseg + 1
printf "si_addr = %p\n", $_siginfo._sifields._sigfault.si_addr
printf "guest r11 = 0x%x  r31 = 0x%x\n", $r11, $r31
printf "[r31+48]=0x%x [r31+56]=0x%x [r31+0]=0x%x\n", *(unsigned int*)($r31+48), *(unsigned int*)($r31+56), *(unsigned int*)($r31+0)
printf "--- backtrace ---\n"
bt 12
continue
