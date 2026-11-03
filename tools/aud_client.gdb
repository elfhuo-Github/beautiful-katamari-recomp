set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $n = 0

break __imp__XexGetProcedureAddress
break __imp__XAudioSubmitRenderDriverFrame
commands
  silent
  set $n = $n + 1
  if $n == 60
    printf "\n=== guest XMA fn table @ guest 0x82A18D00 (host 0x182A18D00) ===\n"
    x/10wx 0x182A18D00
    printf "\n=== symbols ===\n"
    info symbol *(unsigned int*)0x182A18D00
    info symbol *(unsigned int*)0x182A18D04
    info symbol *(unsigned int*)0x182A18D08
    info symbol *(unsigned int*)0x182A18D0c
    info symbol *(unsigned int*)0x182A18D10
    info symbol *(unsigned int*)0x182A18D14
    printf "\n=== breakpoints ===\n"
    info breakpoints
    printf "\n=== maps (grep-ish) ===\n"
    info proc mappings
  end
  if $n > 70
    quit
  end
  continue
end

run
quit
