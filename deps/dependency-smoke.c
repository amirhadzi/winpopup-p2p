#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <tox/tox.h>
#include <tox/toxencryptsave.h>
#include <sodium.h>
int main(void) {
    if (sodium_init() < 0) return 1;
    printf("Tox %u.%u.%u; libsodium %s\n", tox_version_major(), tox_version_minor(), tox_version_patch(), sodium_version_string());
    const unsigned char clear[] = "dependency round-trip";
    const unsigned char pass[] = "non-production test passphrase";
    unsigned char sealed[sizeof clear + TOX_PASS_ENCRYPTION_EXTRA_LENGTH];
    unsigned char opened[sizeof clear];
    Tox_Err_Encryption ee;
    Tox_Err_Decryption de;
    if (!tox_pass_encrypt(clear, sizeof clear, pass, sizeof pass - 1, sealed, &ee)) return 2;
    if (!tox_pass_decrypt(sealed, sizeof sealed, pass, sizeof pass - 1, opened, &de)) return 3;
    if (memcmp(clear, opened, sizeof clear)) return 4;
    Tox_Err_Options_New oe;
    Tox_Options *o = tox_options_new(&oe);
    tox_options_set_udp_enabled(o, false);
    Tox_Err_New ne;
    Tox *t = tox_new(o, &ne);
    tox_options_free(o);
    if (!t) { printf("tox_new failed: %d\n", ne); return 5; }
    for (int i = 0; i < 10; ++i) tox_iterate(t, NULL);
    tox_kill(t);
    puts("Encryption and Tox instance smoke test passed.");
    return 0;
}
