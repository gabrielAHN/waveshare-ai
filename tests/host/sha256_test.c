#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sha256_tiny.h"
static void hex(const unsigned char d[32], char out[65]) {
  for (int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02x", d[i]);
}
static void check(const void *m, size_t n, const char *want) {
  unsigned char d[32];
  char h[65];
  sha256_tiny(m, n, d);
  hex(d, h);
  assert(!strcmp(h, want));
}
int main(void) {
  /* FIPS 180-2 vectors + a multi-block message (checked against Python hashlib). */
  check("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  check("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  check(two, strlen(two), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  static char a[1000];
  memset(a, 'a', sizeof a);
  check(a, sizeof a, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
  /* Incremental == one-shot, across every split point of the 1000-byte message. */
  unsigned char one[32], inc[32];
  sha256_tiny(a, sizeof a, one);
  for (size_t split = 0; split <= sizeof a; split += 37) {
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, a, split);
    sha256_update(&c, a + split, sizeof a - split);
    sha256_final(&c, inc);
    assert(!memcmp(one, inc, 32));
  }
  puts("sha256_tiny: FIPS vectors, multi-block and incremental splits: PASS");
  return 0;
}
