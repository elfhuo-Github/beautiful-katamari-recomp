set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $nk = 0
set $nmu = 0
set $nms = 0
set $nds = 0

break rex::input::mnk::MnkInputDriver::OnKeyDown
commands
  silent
  set $nk = $nk + 1
  if $nk <= 40
    printf "[KEYDOWN] raw=%02x %02x %02x %02x %02x %02x %02x %02x\n", *(unsigned char*)$rsi, *(unsigned char*)($rsi+1), *(unsigned char*)($rsi+2), *(unsigned char*)($rsi+3), *(unsigned char*)($rsi+4), *(unsigned char*)($rsi+5), *(unsigned char*)($rsi+6), *(unsigned char*)($rsi+7)
  end
  continue
end

break rex::input::mnk::MnkInputDriver::OnMouseMove
commands
  silent
  set $nmu = $nmu + 1
  continue
end

break rex::input::mnk::MnkInputDriver::GetDeviceState
commands
  silent
  set $nms = $nms + 1
  if $nms <= 12
    printf "[MNKSTATE] dev=%u pkt=%u buttons=0x%04x lt=%u rt=%u lx=%d ly=%d rx=%d ry=%d\n", $rsi, *(unsigned int*)$rdx, *(unsigned short*)($rdx+4), *(unsigned char*)($rdx+6), *(unsigned char*)($rdx+7), *(short*)($rdx+8), *(short*)($rdx+10), *(short*)($rdx+12), *(short*)($rdx+14)
  end
  continue
end

break __imp__XamInputGetState
commands
  silent
  set $nds = $nds + 1
  if $nds <= 24
    printf "[GUEST] user=%u pkt=%u buttons=0x%04x lt=%u rt=%u lx=%d ly=%d rx=%d ry=%d\n", *(unsigned int*)$rdi, *(unsigned int*)(*(unsigned long*)($rdi+32)+0x100000000), *(unsigned short*)(*(unsigned long*)($rdi+32)+0x100000004), *(unsigned char*)(*(unsigned long*)($rdi+32)+0x100000006), *(unsigned char*)(*(unsigned long*)($rdi+32)+0x100000007), *(short*)(*(unsigned long*)($rdi+32)+0x100000008), *(short*)(*(unsigned long*)($rdi+32)+0x10000000a), *(short*)(*(unsigned long*)($rdi+32)+0x10000000c), *(short*)(*(unsigned long*)($rdi+32)+0x10000000e)
  end
  continue
end

run
printf "\n=== keydown=%d mousemove=%d mnkstate=%d guest=%d ===\n", $nk, $nmu, $nms, $nds
quit
