set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $nd = 0

break rex::audio::XmaContext::Decode
commands
  silent
  set $nd = $nd + 1
  if $nd == 1
    printf "=== first Decode ===\n"
    printf "in0(phys)=0x%08x OUT(phys)=0x%08x work(phys)=0x%08x\n", *(unsigned int*)($rsi+20), *(unsigned int*)($rsi+28), *(unsigned int*)($rsi+32)
    printf "--- host candidates for OUT 0x0d213080 ---\n"
    printf "  [0x10d213080] "
    x/8xw 0x10d213080
    printf "  [0x0d213080]  "
    x/8xw 0x0d213080
    printf "--- host candidates for IN 0x0ed3e800 ---\n"
    printf "  [0x10ed3e800] "
    x/8xw 0x10ed3e800
    printf "  [0x0ed3e800]  "
    x/8xw 0x0ed3e800
    printf "--- guest XMA context in guest memory? try base+0x0d213080 ---\n"
    x/8xw 0x10d213080
  end
  continue
end

run
quit
