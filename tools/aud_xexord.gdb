set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $n = 0
set $have = 0

break __imp__XexGetProcedureAddress
commands
  silent
  set $n = $n + 1
  set $ord = *(unsigned int*)($rdi + 32)
  set $out = *(unsigned int*)($rdi + 40)
  if $have == 1
    set $res = *(unsigned int*)($lastout + 0x100000000)
    printf "ORD %d -> 0x%x\n", $lastord, $res
    info symbol $res
  end
  set $lastord = $ord
  set $lastout = $out
  set $have = 1
  if $n > 14
    quit
  end
  continue
end

run
printf "\n=== XexGetProcedureAddress hits=%d ===\n", $n
quit
