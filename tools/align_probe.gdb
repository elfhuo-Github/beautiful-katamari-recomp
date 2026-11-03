set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $nc = 0
set $nm = 0

break sub_82539960
commands
  silent
  set $c = $rdi
  set $p3 = *(unsigned int*)($c+0)
  set $p4 = *(unsigned int*)($c+32)
  set $p5 = *(unsigned int*)($c+40)
  set $p6 = *(unsigned int*)($c+48)
  set $t10 = $p3 + 48
  set $t11 = $p4 + 32
  set $t6 = $p5 + $t11
  set $mis = ($p3 | $p4 | $p5 | $p6 | $t10 | $t11 | $t6) & 0xF
  set $nc = $nc + 1
  if $mis != 0
    set $nm = $nm + 1
    if $nm <= 12
      printf "MISALIGNED call#%d p3=0x%x p4=0x%x p5=0x%x p6=0x%x t10=0x%x t11=0x%x t6=0x%x\n", $nc, $p3, $p4, $p5, $p6, $t10, $t11, $t6
    end
  end
  if $nc <= 6
    printf "call#%d p3=0x%x p4=0x%x p5=0x%x p6=0x%x t10=0x%x t11=0x%x t6=0x%x\n", $nc, $p3, $p4, $p5, $p6, $t10, $t11, $t6
  end
  continue
end

run
printf "\n=== mixer calls=%d misaligned=%d ===\n", $nc, $nm
quit
