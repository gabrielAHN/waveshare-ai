/* mbedTLS glue for pair_verify_cert() (host-tested in tests/host/pair_state_test.c). esp-tls sets
 * MBEDTLS_SSL_VERIFY_REQUIRED before calling crt_bundle_attach; we install our verify callback and
 * an empty placeholder CA chain (same technique as ESP-IDF's certificate bundle), so the peer is
 * trusted iff its leaf certificate hashes to the pinned fingerprint. */
#include "pair_tls.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
static mbedtls_x509_crt placeholder_ca;
static pair_pin_ctx slots[PAIR_TLS_SLOTS];
pair_pin_ctx *pair_tls_ctx(pair_tls_slot slot) { return &slots[slot]; }
static int verify(void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags) {
  if (!crt) return -1;
  return pair_verify_cert(ctx, crt->raw.p, crt->raw.len, depth, flags);
}
static esp_err_t attach(pair_tls_slot slot, void *conf) {
  if (!conf) return ESP_ERR_INVALID_ARG;
  mbedtls_ssl_config *c = conf;
  mbedtls_ssl_conf_authmode(c, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_verify(c, verify, &slots[slot]);
  mbedtls_ssl_conf_ca_chain(c, &placeholder_ca, NULL);
  return ESP_OK;
}
esp_err_t pair_tls_attach_live(void *conf) { return attach(PAIR_TLS_LIVE, conf); }
esp_err_t pair_tls_attach_voice(void *conf) { return attach(PAIR_TLS_VOICE, conf); }
esp_err_t pair_tls_attach_enroll(void *conf) { return attach(PAIR_TLS_ENROLL, conf); }
