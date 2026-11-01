// Project 3: the finished meeting alert. Project 2's light, melodies and knob,
// plus Wi-Fi: the Mac sends the next meetings over the network and the robot
// keeps time and runs the alerts itself. All behaviour lives in shared/rma3;
// this file only drives the hardware, Wi-Fi and the network.
#include <cstdio>
#include <cstring>

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "lwip/apps/mdns.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/tcp.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "pins.hpp"
#include "rma3/device.hpp"
#include "rma3/wifi.hpp"

namespace {

constexpr unsigned kLedPins[3] = {pins::kRed, pins::kGreen, pins::kBlue};
constexpr uint16_t kPort = 4242;
// Wi-Fi settings live in the last 4 KB sector of the 4 MB flash.
constexpr uint32_t kFlashOffset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

rma3::Device g_dev;
uint16_t g_led[3] = {0, 0, 0};
rma3::Tone g_tone;

uint32_t now_ms() { return to_ms_since_boot(get_absolute_time()); }

// ---------------------------------------------------------------- outputs

// Common-anode RGB LED: a colour is on while its pin is LOW. Inverting the PWM
// output makes level 65535 mean "fully on" and level 0 (pin high) "off".
void led_init() {
  for (unsigned pin : kLedPins) {
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, 65535);  // 150 MHz / 65536 = ~2.3 kHz, no visible flicker
    pwm_config_set_clkdiv_int(&cfg, 1);
    pwm_config_set_output_polarity(&cfg, true, true);
    pwm_init(slice, &cfg, false);
    pwm_set_chan_level(slice, pwm_gpio_to_channel(pin), 0);
  }
  for (unsigned pin : kLedPins) pwm_set_enabled(pwm_gpio_to_slice_num(pin), true);
  for (unsigned pin : kLedPins) gpio_set_function(pin, GPIO_FUNC_PWM);  // pins go high: off
}

void buzzer_init() {
  uint slice = pwm_gpio_to_slice_num(pins::kBuzzer);
  pwm_config cfg = pwm_get_default_config();
  pwm_init(slice, &cfg, false);
  pwm_set_chan_level(slice, pwm_gpio_to_channel(pins::kBuzzer), 0);
  pwm_set_enabled(slice, true);
  gpio_set_function(pins::kBuzzer, GPIO_FUNC_PWM);
}

// Integer clock divider (fractional dividers make notes wobble), wrap sized
// so one PWM period is one cycle of the note.
void buzzer_set(const rma3::Tone& t) {
  uint slice = pwm_gpio_to_slice_num(pins::kBuzzer);
  uint chan = pwm_gpio_to_channel(pins::kBuzzer);
  pwm_set_chan_level(slice, chan, 0);  // silence first; change pitch while quiet
  if (t.hz == 0 || t.duty_permille == 0) return;
  uint32_t sys = clock_get_hz(clk_sys);
  uint32_t div = (sys + 65536u * t.hz - 1) / (65536u * t.hz);
  if (div < 1) div = 1;
  if (div > 255) div = 255;
  uint32_t wrap = (sys + div * t.hz / 2) / (div * t.hz) - 1;
  if (wrap > 65535) wrap = 65535;
  pwm_set_clkdiv_int_frac(slice, div, 0);
  pwm_set_wrap(slice, wrap);
  uint32_t duty = t.duty_permille > 500 ? 500 : t.duty_permille;  // never above 50 %
  pwm_set_chan_level(slice, chan, static_cast<uint16_t>((wrap + 1) * duty / 1000));
}

void apply_outputs() {
  for (int ch = 0; ch < 3; ++ch) {
    uint16_t level = g_dev.led_level(ch);
    if (level != g_led[ch]) {
      g_led[ch] = level;
      pwm_set_chan_level(pwm_gpio_to_slice_num(kLedPins[ch]), pwm_gpio_to_channel(kLedPins[ch]), level);
    }
  }
  rma3::Tone t = g_dev.tone();
  if (t.hz != g_tone.hz || t.duty_permille != g_tone.duty_permille) {
    g_tone = t;
    buzzer_set(t);
  }
}

// ---------------------------------------------------------------- network

bool g_wifi_ok = false;  // the CYW43 chip started
rma3::WifiCreds g_creds;
bool g_have_creds = false;
rma3::WifiSupervisor g_sup;

// One TCP client at a time. lwIP callbacks run inside cyw43_arch_poll(), in
// the main loop, so no locking is needed.
tcp_pcb* g_client = nullptr;
uint8_t g_rx[512];
size_t g_rx_head = 0, g_rx_len = 0;

struct netif* sta_netif() { return &cyw43_state.netif[CYW43_ITF_STA]; }

void drop_client() {
  if (!g_client) return;
  tcp_arg(g_client, nullptr);
  tcp_recv(g_client, nullptr);
  tcp_err(g_client, nullptr);
  if (tcp_close(g_client) != ERR_OK) tcp_abort(g_client);
  g_client = nullptr;
  g_rx_len = 0;
}

err_t on_recv(void*, tcp_pcb* pcb, pbuf* p, err_t) {
  if (pcb != g_client) {
    if (p) pbuf_free(p);
    return ERR_OK;
  }
  if (!p) {  // client closed
    drop_client();
    return ERR_OK;
  }
  if (p->tot_len > sizeof g_rx - g_rx_len) return ERR_MEM;  // lwIP offers it again later
  for (pbuf* q = p; q; q = q->next) {
    const uint8_t* d = static_cast<const uint8_t*>(q->payload);
    for (u16_t i = 0; i < q->len; ++i) g_rx[(g_rx_head + g_rx_len++) % sizeof g_rx] = d[i];
  }
  tcp_recved(pcb, p->tot_len);
  pbuf_free(p);
  return ERR_OK;
}

void on_err(void*, err_t) {  // lwIP already freed the pcb
  g_client = nullptr;
  g_rx_len = 0;
}

err_t on_accept(void*, tcp_pcb* pcb, err_t err) {
  if (err != ERR_OK || !pcb) return ERR_VAL;
  drop_client();  // the newest connection wins (e.g. the work laptop takes over)
  g_client = pcb;
  g_rx_head = g_rx_len = 0;
  ip_set_option(pcb, SOF_KEEPALIVE);  // notice a Mac that vanished
  pcb->keep_idle = 10000;
  pcb->keep_intvl = 2000;
  pcb->keep_cnt = 3;
  tcp_nagle_disable(pcb);
  tcp_recv(pcb, on_recv);
  tcp_err(pcb, on_err);
  return ERR_OK;
}

void server_start() {
  tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
  if (!pcb) return;
  if (tcp_bind(pcb, IP_ANY_TYPE, kPort) != ERR_OK) {
    tcp_close(pcb);
    return;
  }
  tcp_pcb* listener = tcp_listen_with_backlog(pcb, 1);
  if (!listener) {
    tcp_close(pcb);
    return;
  }
  tcp_accept(listener, on_accept);
}

void net_reply(const char* reply) {
  if (!g_client) return;
  size_t n = std::strlen(reply);
  if (tcp_sndbuf(g_client) < n + 2) return;  // client is not reading; it will time out
  tcp_write(g_client, reply, n, TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);
  tcp_write(g_client, "\r\n", 2, TCP_WRITE_FLAG_COPY);
  tcp_output(g_client);
}

void mdns_txt(struct mdns_service* service, void*) {
  mdns_resp_add_service_txtitem(service, "v=3", 3);
}

void mdns_start() {
  mdns_resp_init();
  mdns_resp_add_netif(sta_netif(), "rma");
  mdns_resp_add_service(sta_netif(), "Robot Meeting Alert", "_rma", DNSSD_PROTO_TCP, kPort, mdns_txt,
                        nullptr);
}

rma3::WifiLink link_status() {
  switch (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA)) {
    case CYW43_LINK_UP: return rma3::WifiLink::Up;
    case CYW43_LINK_JOIN:
    case CYW43_LINK_NOIP: return rma3::WifiLink::Joining;
    case CYW43_LINK_FAIL:
    case CYW43_LINK_NONET:
    case CYW43_LINK_BADAUTH: return rma3::WifiLink::Failed;
    default: return rma3::WifiLink::Down;
  }
}

void join() {
  cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
  bool open = g_creds.pass[0] == '\0';
  cyw43_arch_wifi_connect_async(g_creds.ssid, open ? nullptr : g_creds.pass,
                                open ? CYW43_AUTH_OPEN : CYW43_AUTH_WPA2_MIXED_PSK);
}

// Onboard LED: off = no Wi-Fi settings, blinking = joining, solid = connected.
void status_led(uint32_t t) {
  static int last = -1;
  int on = 0;
  switch (g_sup.status()) {
    case rma3::kNetUp: on = 1; break;
    case rma3::kNetJoining:
    case rma3::kNetFailed: on = (t / 250) % 2; break;
    default: on = 0;
  }
  if (on != last) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
    last = on;
  }
}

void net_service(uint32_t t) {
  if (!g_wifi_ok) return;
  cyw43_arch_poll();
  if (g_sup.tick(t, link_status())) join();
  if (g_sup.take_came_up()) mdns_resp_restart(sta_netif());
  static rma3::NetStatus last = rma3::kNetNone;
  static uint32_t last_ip = 0;
  uint32_t ip = netif_ip4_addr(sta_netif())->addr;
  if (g_sup.status() != last || ip != last_ip) {
    last = g_sup.status();
    last_ip = ip;
    g_dev.set_net(last, last == rma3::kNetUp ? ip4addr_ntoa(netif_ip4_addr(sta_netif())) : "-");
  }
  status_led(t);
}

// ---------------------------------------------------------------- flash

const uint8_t* flash_record() {
  return reinterpret_cast<const uint8_t*>(XIP_BASE + kFlashOffset);
}

struct FlashJob {
  bool save;
  uint8_t page[rma3::kWifiRecordSize];
};

void flash_job(void* arg) {
  auto* job = static_cast<FlashJob*>(arg);
  flash_range_erase(kFlashOffset, FLASH_SECTOR_SIZE);
  if (job->save) flash_range_program(kFlashOffset, job->page, sizeof job->page);
}

// Only ever reached from the USB-only WIFIGO / WIFICLR commands.
void handle_wifi_action() {
  rma3::WifiCreds c;
  rma3::WifiAction a = g_dev.take_wifi_action(&c);
  if (a == rma3::WifiAction::None) return;
  FlashJob job{};
  job.save = (a == rma3::WifiAction::Save);
  if (job.save) rma3::encode_wifi_record(c, job.page);
  buzzer_set(rma3::Tone{});      // the coil must not sit on a held pin
  watchdog_enable(2000, true);   // a sector erase can take up to ~400 ms
  int rc = flash_safe_execute(flash_job, &job, 100);
  watchdog_enable(500, true);
  g_tone = rma3::Tone{};
  if (rc != PICO_OK) return;
  if (job.save) {
    g_creds = c;
    g_have_creds = true;
  } else {
    g_creds = rma3::WifiCreds{};
    g_have_creds = false;
    if (g_wifi_ok) cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
  }
  g_sup.set_have_creds(g_have_creds, now_ms());
}

// ---------------------------------------------------------------- loop

void service() {
  uint32_t t = now_ms();
  net_service(t);
  g_dev.tick(t, stdio_usb_connected() || g_client != nullptr);
  apply_outputs();
  watchdog_update();
}

}  // namespace

int main() {
  // Buzzer pin low before anything else so the coil never sees DC.
  gpio_init(pins::kBuzzer);
  gpio_put(pins::kBuzzer, false);
  gpio_set_dir(pins::kBuzzer, GPIO_OUT);

  led_init();
  buzzer_init();
  adc_init();
  adc_gpio_init(pins::kKnob);
  adc_select_input(pins::kKnobAdcInput);
  stdio_init_all();

  // Starting the Wi-Fi chip blocks for a while, so it happens before the
  // watchdog is armed, and always (so WIFIGO never starts it inside the loop).
  if (cyw43_arch_init_with_country(CYW43_COUNTRY_CANADA) == 0) {
    g_wifi_ok = true;
    cyw43_arch_enable_sta_mode();
    server_start();
    mdns_start();
    g_have_creds = rma3::decode_wifi_record(flash_record(), &g_creds);
    g_sup.set_have_creds(g_have_creds, now_ms());
  }
  watchdog_enable(500, true);  // a hung firmware resets, which releases the buzzer

  char reply[rma3::Device::kMaxReply];
  uint32_t next_knob = 0;
  for (;;) {
    uint32_t t = now_ms();
    if (static_cast<int32_t>(t - next_knob) >= 0) {
      g_dev.knob_sample(adc_read());
      next_knob = t + 10;
    }
    service();
    for (int n = 0; n < 128; ++n) {  // bounded USB drain per iteration
      int c = getchar_timeout_us(0);
      if (c == PICO_ERROR_TIMEOUT) break;
      if (g_dev.feed(rma3::Source::Usb, static_cast<char>(c), now_ms(), reply)) {
        service();
        std::printf("%s\n", reply);  // may block up to 20 ms
        service();
      }
    }
    for (int n = 0; n < 256 && g_rx_len > 0; ++n) {  // bounded network drain
      char c = static_cast<char>(g_rx[g_rx_head]);
      g_rx_head = (g_rx_head + 1) % sizeof g_rx;
      --g_rx_len;
      if (g_dev.feed(rma3::Source::Net, c, now_ms(), reply)) net_reply(reply);
    }
    handle_wifi_action();
    sleep_us(500);
  }
}
