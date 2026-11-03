set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $n = 0

break rex::audio::XmaContext::DecodePacket
commands
  silent
  set $n = $n + 1
  set $pk = $rdx
  set $pdata = *(void**)($pk + 24)
  set $psz = *(int*)($pk + 32)
  printf "PKT n=%d data=%p size=%d head=%02x%02x%02x%02x%02x%02x%02x%02x\n", $n, $pdata, $psz, *(unsigned char*)($pdata), *(unsigned char*)($pdata+1), *(unsigned char*)($pdata+2), *(unsigned char*)($pdata+3), *(unsigned char*)($pdata+4), *(unsigned char*)($pdata+5), *(unsigned char*)($pdata+6), *(unsigned char*)($pdata+7)
  if $psz > 0 && $psz <= 8192
    append binary memory /tmp/opencode/xma_pkt.bin $pdata ($pdata + $psz)
  end
  if $n >= 60
    quit
  end
  continue
end

run
printf "\n=== DecodePacket hits=%d ===\n", $n
quit
