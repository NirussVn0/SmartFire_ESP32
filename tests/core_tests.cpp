#include <AlarmCore.h>
#include <cassert>
#include <iostream>
using namespace smartfire;
int main() {
  int passed = 0;
  {
    AlarmCore c; c.update({1000, false, true, false}, 0);
    assert(c.state(false, true) == State::Normal); assert(!c.hornRequested()); ++passed;
  }
  {
    AlarmCore c; c.update({0, false, false, false}, 0);
    assert(c.state(true, false) == State::Warming);
    assert(c.state(false, false) == State::Unconfigured); ++passed;
  }
  {
    AlarmCore c; c.update({1000, true, false, false}, 0);
    assert(c.state(true, false) == State::Suspected);
    c.update({1000, true, false, false}, 499); assert(!c.alarm());
    c.update({1000, true, false, false}, 500); assert(c.alarm() && c.hornRequested()); ++passed;
  }
  {
    AlarmCore c; c.update({2500, false, true, false}, 10);
    c.update({2500, false, true, false}, 2009); assert(!c.alarm());
    c.update({2500, false, true, false}, 2010); assert(c.alarm()); ++passed;
  }
  {
    AlarmCore c; c.update({4000, false, false, false}, 0);
    c.update({4000, false, false, false}, 9000); assert(!c.alarm()); ++passed;
  }
  {
    AlarmCore c; c.update({2500, false, true, false}, 0);
    c.update({1800, false, true, false}, 1900);
    c.update({2500, false, true, false}, 2000);
    c.update({2500, false, true, false}, 3999); assert(!c.alarm()); ++passed;
  }
  {
    AlarmCore c; c.update({2500, false, true, false}, 0); c.update({2500, false, true, false}, 2000);
    assert(c.command(Command::Ack, 2000) == Result::Applied && c.hornRequested());
    assert(c.command(Command::Silence, 2000) == Result::Applied && !c.hornRequested());
    assert(c.command(Command::Silence, 3000) == Result::Rejected);
    c.update({2500, false, true, false}, 31999); assert(c.muted());
    c.update({2500, false, true, false}, 32000); assert(!c.muted() && c.hornRequested());
    assert(c.command(Command::Silence, 32000) == Result::Rejected); ++passed;
  }
  {
    AlarmCore c; c.update({2500, false, true, false}, 0); c.update({2500, false, true, false}, 2000);
    c.command(Command::Silence, 2000);
    c.update({2500, true, true, false}, 2050); assert(c.hornRequested());
    assert(c.command(Command::Silence, 2050) == Result::Rejected); ++passed;
  }
  {
    AlarmCore c; c.update({2500, false, true, false}, 0); c.update({2500, false, true, false}, 2000);
    c.command(Command::Silence, 2000);
    c.update({0, false, true, true}, 2050); assert(c.hornRequested());
    c.update({0, false, true, true}, 20000); assert(c.alarm()); ++passed;
  }
  {
    AlarmCore c; c.update({2500, false, true, false}, 0); c.update({2500, false, true, false}, 2000);
    c.update({2000, false, true, false}, 15000); assert(c.alarm()); // Hysteresis band keeps latch.
    c.update({1800, false, true, false}, 15050);
    c.update({1800, false, true, false}, 25049); assert(c.alarm());
    c.update({1800, false, true, false}, 25050); assert(!c.alarm());
    c.update({2500, false, true, false}, 26000); c.update({2500, false, true, false}, 28000);
    assert(c.command(Command::Silence, 28000) == Result::Applied); ++passed;
  }
  {
    AlarmCore c; c.update({1000, false, true, false}, 0);
    assert(c.command(Command::Ack, 0) == Result::Rejected);
    assert(c.command(Command::Silence, 0) == Result::Rejected);
    assert(c.command(Command::Test, 0) == Result::Applied && c.testing());
    assert(c.command(Command::Test, 500) == Result::Rejected);
    c.update({1000, true, true, false}, 500);
    c.update({1000, true, true, false}, 1000);
    assert(c.alarm() && c.hornRequested() && !c.testing()); ++passed;
  }
  {
    AlarmCore c; c.update({4095, false, true, true}, 0);
    assert(c.state(false, true) == State::Fault);
    c.update({4095, false, true, true}, 2000); assert(c.alarm()); ++passed;
  }
  {
    AlarmCore c; const uint32_t t = UINT32_MAX - 200;
    c.update({1000, true, true, false}, t);
    c.update({1000, true, true, false}, t + 500u); assert(c.alarm()); ++passed;
  }
  {
    HeldFor h; assert(!h.update(true, 0, 100));
    assert(!h.update(false, 90, 100)); assert(!h.update(true, 100, 100));
    assert(h.update(true, 200, 100)); ++passed;
  }
  {
    AlarmCore c; const uint32_t t = UINT32_MAX - 3000;
    c.update({2500, false, true, false}, t); c.update({2500, false, true, false}, t + 2000u);
    c.command(Command::Silence, t + 2000u);
    c.update({2500, false, true, false}, t + 32000u); assert(c.hornRequested()); ++passed;
  }
  std::cout << passed << " core scenarios passed\n";
}
