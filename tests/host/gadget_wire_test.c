/* gadget_wire.h: the board side of the Hermes Gadget SDK protocol (hello/auth, audio frames,
 * gateway messages). Vectors below were produced by the pinned SDK's Python protocol module. */
#include "gadget_wire.h"
#include <assert.h>
#include <stdio.h>

static unsigned char token[32];

static void test_identity_matches_sdk(void) {
  for (int i = 0; i < 32; i++) token[i] = (unsigned char)i;
  gw_identity id;
  gw_identity_from_token(token, &id);
  char b64[64];
  assert(gw_b64(id.key, 32, b64, sizeof b64) == 44);
  assert(!strcmp(b64, "mF66eloZu8zvpX0S3IF31/ETwgCsfokJLgOV7k++U2A="));
  assert(!strcmp(id.device_id, "hg-98730b5fff9164f0"));
  char mac[64];
  assert(gw_auth_mac(&id, "bm9uY2Utbm9uY2U=", mac, sizeof mac));
  assert(!strcmp(mac, "bTqDyn1oCiaDK8mdUkynNbVSeZ7hVZuahF9OgHl9FxY="));
  char bearer[96];
  assert(gw_bearer_header(token, bearer, sizeof bearer));
  assert(!strncmp(bearer, "Authorization: Bearer 000102", 28) && strstr(bearer, "1e1f\r\n"));
}

static void test_request_ids_carry_identity_and_counter(void) {
  gw_identity id;
  gw_identity_from_token(token, &id);
  char rid[33];
  gw_request_id(&id, 0x1234, rid);
  assert(!strcmp(rid, "98730b5fff9164f00000000000001234"));
}

static void test_frames_and_messages(void) {
  unsigned char h[4];
  gw_audio_header(0x1234, h);
  assert(h[0] == 1 && h[1] == 1 && h[2] == 0x34 && h[3] == 0x12);
}

static void test_json_reader(void) {
  const char *j = "{\"type\":\"challenge\",\"nonce\":\"abc=\",\"enrolled\":false,\"nest\":{\"type\":\"x\"},\"arr\":[1,{\"a\":2}]}";
  char s[32];
  bool b = true;
  assert(gw_json_string(j, strlen(j), "type", s, sizeof s) && !strcmp(s, "challenge"));
  assert(gw_json_string(j, strlen(j), "nonce", s, sizeof s) && !strcmp(s, "abc="));
  assert(gw_json_bool(j, strlen(j), "enrolled", &b) && !b);
  assert(!gw_json_string(j, strlen(j), "missing", s, sizeof s));
  assert(!gw_json_string(j, strlen(j), "a", s, sizeof s));  /* nested keys are not top-level */
  const char *esc = "{\"t\":\"a\\\"b\\\\c\\n\\u00e9\\u20ac\"}";
  assert(gw_json_string(esc, strlen(esc), "t", s, sizeof s) && !strcmp(s, "a\"b\\c\n\xc3\xa9\xe2\x82\xac"));
  assert(!gw_json_string(esc, strlen(esc), "t", s, 4));  /* too long for the buffer: refused, not truncated */
  const char *bad[] = {"", "{", "{\"type\":}", "[1]", "{\"type\":\"x\"", "{\"type\":\"\\q\"}"};
  for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) assert(!gw_json_string(bad[i], strlen(bad[i]), "type", s, sizeof s));
}

static void test_url_from_bridge_base(void) {
  char host[100];
  assert(gw_host_from_base("https://10.99.0.4:8098", host, sizeof host) && !strcmp(host, "10.99.0.4"));
}

/* Hermes gateway (stock Gadget SDK adapter) messages: the Hermes tile. */
static void test_gateway_messages(void) {
  gw_identity id;
  gw_identity_from_token(token, &id);
  char out[512];
  assert(gw_hello_gateway_json(&id, out, sizeof out));
  assert(strstr(out, "\"device_id\":\"hg-98730b5fff9164f0\""));
  assert(strstr(out, "\"name\":\"Waveshare AI\""));
  assert(strstr(out, "\"board\":\"waveshare-esp32s3-amoled-1.8\""));
  assert(strstr(out, "\"firmware\":\"waveshare-ai\""));
  assert(strstr(out, "\"charset\":\"ascii\""));
  assert(strstr(out, "\"mic\":{\"rate\":16000,\"format\":\"pcm16\"}"));
  assert(!strstr(out, "speaker"));  /* no speaker: the gateway never streams audio to the board */
  char path[48];
  assert(gw_gateway_path("atlas", path, sizeof path) && !strcmp(path, "/gadget/atlas"));
  assert(!gw_gateway_path("../x", path, sizeof path) && !gw_gateway_path("", path, sizeof path));
  const char rid[] = "00112233445566778899aabbccddeeff";
  assert(gw_gateway_audio_start_json(rid, out, sizeof out));
  assert(!strcmp(out, "{\"type\":\"audio.start\",\"id\":\"00112233445566778899aabbccddeeff\",\"stream\":1,"
                      "\"rate\":16000,\"format\":\"pcm16\",\"mode\":\"hold\"}"));
  assert(gw_gateway_audio_end_json(rid, 2100, out, sizeof out));
  assert(!strcmp(out, "{\"type\":\"audio.end\",\"id\":\"00112233445566778899aabbccddeeff\",\"stream\":1,\"duration_ms\":2100}"));
  assert(!gw_gateway_audio_start_json("not-hex", out, sizeof out));
  assert(gw_prompt_reply_json("q1", false, out, sizeof out) && !strcmp(out, "{\"type\":\"prompt.reply\",\"id\":\"q1\",\"answer\":\"no\"}"));
  assert(!gw_prompt_reply_json("q\"1", false, out, sizeof out));
  const char *ping = "{\"type\":\"ping\",\"ts\":1727950000000}";
  assert(gw_pong_json(ping, strlen(ping), out, sizeof out) &&
         !strcmp(out, "{\"type\":\"pong\",\"ts\":1727950000000}"));
  assert(gw_pong_json("{\"type\":\"ping\"}", 15, out, sizeof out) && !strcmp(out, "{\"type\":\"pong\"}"));
  /* Long replies are cut at a character boundary with an ellipsis instead of being dropped. */
  char text[12];
  const char *j = "{\"type\":\"reply\",\"text\":\"abcdefgh\\u00e9xyz\"}";
  assert(gw_json_text(j, strlen(j), "text", text, sizeof text) && !strcmp(text, "abcdefgh..."));
  const char *k = "{\"type\":\"reply\",\"text\":\"ab\\u00e9\"}";
  assert(gw_json_text(k, strlen(k), "text", text, sizeof text) && !strcmp(text, "ab\xc3\xa9"));
  assert(!gw_json_text(k, strlen(k), "missing", text, sizeof text));
}

static void test_turn_end(void) {
  static voice_command c;
  memset(&c, 0, sizeof c);
  gw_turn_end("success", false, &c);
  assert(c.status == VOICE_DONE && !strcmp(c.text, "Done."));
  memset(&c, 0, sizeof c);
  snprintf(c.text, sizeof c.text, "Paris");
  gw_turn_end("success", true, &c);
  assert(c.status == VOICE_DONE && !strcmp(c.text, "Paris"));
  gw_turn_end("cancelled", true, &c);
  assert(c.status == VOICE_INTERRUPTED);
  /* Failure keeps Hermes's own full reply (quota / provider errors), never a partial delta. */
  snprintf(c.text, sizeof c.text, "Provider quota exhausted until 3 PM");
  gw_turn_end("failure", true, &c);
  assert(c.status == VOICE_ERROR && !strcmp(c.text, "Provider quota exhausted until 3 PM"));
  snprintf(c.text, sizeof c.text, "Half a sent");
  gw_turn_end("failure", false, &c);
  assert(c.status == VOICE_ERROR && !strcmp(c.text, "Hermes could not finish"));
  memset(&c, 0, sizeof c);
  gw_turn_end("weird", true, &c);
  assert(c.status == VOICE_ERROR && !strcmp(c.text, "Hermes could not finish"));
  gw_turn_end(NULL, false, &c);
  assert(c.status == VOICE_ERROR);
}

static void test_keepalive(void) {
  /* Due well inside the hub's silence limit (3 x heartbeat_s, 15 s at its 5 s minimum). */
  assert(GW_KEEPALIVE_US * 3 <= 15LL * 1000 * 1000);
  assert(!gw_keepalive_due(1000, 1000) && !gw_keepalive_due(GW_KEEPALIVE_US - 1, 0));
  assert(gw_keepalive_due(GW_KEEPALIVE_US, 0) && gw_keepalive_due(90LL * 1000 * 1000, 0));
}

int main(void) {
  test_identity_matches_sdk();
  test_keepalive();
  test_turn_end();
  test_request_ids_carry_identity_and_counter();
  test_frames_and_messages();
  test_json_reader();
  test_url_from_bridge_base();
  test_gateway_messages();
  puts("gadget_wire_test ok");
  return 0;
}
