set pagination off
set confirm off
set breakpoint pending on
handle SIGSEGV nostop noprint pass
handle SIGBUS nostop noprint pass
handle SIGILL nostop noprint pass
set $n = 0

break rex::audio::XmaContext::Work
commands
  silent
  set $n = $n + 1
  if $n <= 3
    printf "\n=== XmaContext::Work hit %d  this=%p ===\n", $n, $rdi
    printf "object words:\n"
    x/20wx $rdi
  end
  if $n >= 400
    quit
  end
  continue
end

run
printf "\n=== Work hits=%d ===\n", $n
quit
