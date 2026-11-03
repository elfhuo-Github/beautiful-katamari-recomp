set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $np = 0
set $nb = 0
set $na = 0
set $nd = 0
set $nadd = 0
set $nrem = 0

break rex::input::sdl::SDLInputDriver::ProcessEventLocked
commands
  silent
  set $np = $np + 1
  printf "[PE ] type=0x%x\n", *(unsigned int*)$rsi
  if $np > 60
    quit
  end
  continue
end

break rex::input::sdl::SDLInputDriver::DrainAndLock
commands
  silent
  set $nd = $nd + 1
  continue
end

break rex::input::sdl::SDLInputDriver::OnControllerDeviceButtonChangedLocked
commands
  silent
  set $nb = $nb + 1
  printf "[BTN] type=0x%x which=%u button=%u state=%u\n", *(unsigned int*)($rsi+0), *(unsigned int*)($rsi+16), *(unsigned char*)($rsi+20), *(unsigned char*)($rsi+21)
  continue
end

break rex::input::sdl::SDLInputDriver::OnControllerDeviceAxisMotionLocked
commands
  silent
  set $na = $na + 1
  printf "[AXS] type=0x%x which=%u axis=%u value=%d\n", *(unsigned int*)($rsi+0), *(unsigned int*)($rsi+16), *(unsigned char*)($rsi+20), *(int*)($rsi+24)
  continue
end

break rex::input::sdl::SDLInputDriver::OnControllerDeviceAddedLocked
commands
  silent
  set $nadd = $nadd + 1
  printf "[ADD] type=0x%x which=%u\n", *(unsigned int*)($rsi+0), *(unsigned int*)($rsi+16)
  continue
end

break rex::input::sdl::SDLInputDriver::OnControllerDeviceRemovedLocked
commands
  silent
  set $nrem = $nrem + 1
  printf "[REM] type=0x%x which=%u\n", *(unsigned int*)($rsi+0), *(unsigned int*)($rsi+16)
  continue
end

run
printf "\n=== PE=%d DRAIN=%d BTN=%d AXS=%d ADD=%d REM=%d ===\n", $np, $nd, $nb, $na, $nadd, $nrem
printf "=== breakpoints ===\n"
info breakpoints
quit
