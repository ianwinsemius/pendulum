# Source this (". ppwire.sh") after ppenv.sh. Provides pendulum_unwire:
# puts PathPilot's own wiring back and makes sure no Pendulum signal can
# cause motion. Safe to call when nothing of ours is wired.

pendulum_unwire() {
  # 1. Hand-wheel enables off first, so counter changes below can't move anything.
  for _pw_a in 0 1 2; do
    halcmd unlinkp "axis.$_pw_a.jog-enable" >/dev/null 2>&1
    halcmd setp "axis.$_pw_a.jog-enable" 0 >/dev/null 2>&1
  done
  for _pw_s in pendulum-ok pendulum-x-counts pendulum-y-counts pendulum-z-counts; do
    halcmd delsig "$_pw_s" >/dev/null 2>&1
  done

  # 2. Give PathPilot's jog-step button back to the ShuttleXpress, unless it
  #    is already connected there (also repairs a half-finished attach).
  halcmd delsig pendulum-step-out >/dev/null 2>&1
  if halcmd show pin tormach.jog-step-button >/dev/null 2>&1 &&
     ! halcmd show pin tormach.jog-step-button 2>/dev/null | grep -q "sxp.step-button"; then
    halcmd unlinkp tormach.jog-step-button >/dev/null 2>&1
    halcmd net sxp.step-button tormach.jog-step-button >/dev/null 2>&1
  fi
  unset _pw_a _pw_s
}
