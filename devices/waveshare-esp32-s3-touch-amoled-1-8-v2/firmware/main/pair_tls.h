#pragma once
/* Certificate-pinned TLS for esp_http_client: pass one of the attach functions as
 * esp_http_client_config_t.crt_bundle_attach. The configured pin (SHA-256 of the bridge's leaf
 * certificate DER) is the ONLY trust anchor; no CA is compiled into the firmware. One slot per
 * task so concurrent connections never share pin state. */
#include "esp_err.h"
#include "pair_state.h"
typedef enum {PAIR_TLS_LIVE,PAIR_TLS_VOICE,PAIR_TLS_ENROLL,PAIR_TLS_SLOTS} pair_tls_slot;
pair_pin_ctx *pair_tls_ctx(pair_tls_slot slot);
esp_err_t pair_tls_attach_live(void *conf);
esp_err_t pair_tls_attach_voice(void *conf);
esp_err_t pair_tls_attach_enroll(void *conf);
