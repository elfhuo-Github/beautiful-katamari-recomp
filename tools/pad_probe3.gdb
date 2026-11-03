set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $n = 0
set $nd = 0
set $have = 0

break rex::input::sdl::SDLInputDriver::DrainAndLock
commands
  silent
  set $nd = $nd + 1
  continue
end

break rex::input::sdl::SDLInputDriver::GetDeviceState
commands
  silent
  set $n = $n + 1
  if $have == 1
    printf "[DS] dev=%u pkt=%u buttons=0x%04x lt=%u rt=%u lx=%d ly=%d rx=%d ry=%d\n", $lastdev, *(unsigned int*)$lastout, *(unsigned short*)($lastout+4), *(unsigned char*)($lastout+6), *(unsigned char*)($lastout+7), *(short*)($lastout+8), *(short*)($lastout+10), *(short*)($lastout+12), *(short*)($lastout+14)
  end
  set $lastdev = $rsi
  set $lastout = $rdx
  set $have = 1
  if $n > 400
    quit
  end
  continue
end

run
printf "\n=== getdevicestate hits=%d drainandlock hits=%d ===\n", $n, $nd
quit
