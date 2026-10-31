#include <string>

#include "check.hpp"
#include "rma/host.hpp"

int main() {
  rma::Outputs o;
  CHECK(rma::parse_state_reply("OK R1 G0 B1 Z0", &o));
  CHECK(o.r && !o.g && o.b && !o.buzz);
  CHECK(rma::parse_state_reply("OK R0 G1 B0 Z1", &o));
  CHECK(!o.r && o.g && !o.b && o.buzz);
  CHECK(!rma::parse_state_reply("OK R1 G0 B1", &o));
  CHECK(!rma::parse_state_reply("OK R2 G0 B1 Z0", &o));
  CHECK(!rma::parse_state_reply("OK G1 R0 B1 Z0", &o));
  CHECK(!rma::parse_state_reply("ERR BADCMD", &o));
  CHECK(!rma::parse_state_reply("OK R1 G0 B1 Z0 ", &o));
  CHECK(!rma::parse_state_reply("", &o));

  CHECK_STR(rma::format_state(o), "R0 G1 B0 Z1");

  std::string fw;
  CHECK(rma::parse_hello_reply("RMA 1 0.1.0", &fw));
  CHECK_STR(fw, "0.1.0");
  CHECK(!rma::parse_hello_reply("RMA 2 0.1.0", &fw));
  CHECK(!rma::parse_hello_reply("RMA 1 ", &fw));
  CHECK(!rma::parse_hello_reply("RMA 1 a b", &fw));
  CHECK(!rma::parse_hello_reply("ERR BADCMD", &fw));
  CHECK(!rma::parse_hello_reply("", &fw));

  if (g_failures == 0) std::printf("test_host: ok\n");
  return g_failures;
}
