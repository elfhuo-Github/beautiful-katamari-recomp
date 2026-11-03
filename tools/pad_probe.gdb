set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $nb = 0
set $na = 0

break rex::input::sdl::SDLInputDriver::OnControllerDeviceButtonChangedLocked
commands
  silent
  set $nb = $nb + 1
  printf "[BTN] type=%u which=%u button=%u state=%u\n", *(unsigned int*)($rsi+0), *(unsigned int*)($rsi+16), *(unsigned char*)($rsi+20), *(unsigned char*)($rsi+21)
  if $nb > 60
    quit
  end
  continue
end

break rex::input::sdl::SDLInputDriver::OnControllerDeviceAxisMotionLocked
commands
  silent
  set $na = $na + 1
  if $na <= 40
    printf "[AXS] type=%u which=%u axis=%u value=%d\n", *(unsigned int*)($rsi+0), *(unsigned int*)($rsi+16), *(unsigned char*)($rsi+20), *(int*)$rsi+24
  end
  continue
end

break rex::input::sdl::SDLInputDriver::GetDeviceState
commands
  silent
  printf "[ST ] dev=%u\n", $rsi
  continue
end

run
printf "\n=== buttons=%d axes=%d ===\n", $nb, $na
quit
