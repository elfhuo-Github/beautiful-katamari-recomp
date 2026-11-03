set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $n = 0
set $nh = 0
set $np = 0

break LINUX_JoystickOpen
commands
  silent
  printf "[OPEN] path=%s\n", $rsi
  continue
end

break rex::input::sdl::SDLInputDriver::HandleEvent
commands
  silent
  set $nh = $nh + 1
  if $nh <= 80
    printf "[HE ] type=0x%x\n", *(unsigned int*)$rsi
  end
  continue
end

break rex::input::sdl::SDLInputDriver::ProcessEventLocked
commands
  silent
  set $np = $np + 1
  if $np <= 80
    printf "[PE ] type=0x%x\n", *(unsigned int*)$rsi
  end
  continue
end

run
printf "\n=== handle=%d process=%d ===\n", $nh, $np
quit
