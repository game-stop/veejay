#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

/*
 * Simulated packet processing logic mirroring the vulnerable pattern in packet.c
 * The invariant: pointer arithmetic using attacker-controlled 'len' must never
 * result in a destination pointer that wraps around or points outside the
 * allocated buffer before memcpy is called.
 */

#define BUFFER_SIZE 4096
#define HEADER_SIZE 64

/* Simulated safe packet processing function that enforces bounds */
static int safe_packet_process(uint8_t *dst, size_t dst_size,
                                const uint8_t *header, size_t header_size,
                                const uint8_t *plane, size_t plane_size,
                                size_t len)
{
    /* Security invariant checks:
     * 1. len must not exceed dst_size
     * 2. len + plane_size must not overflow
     * 3. len + plane_size must not exceed dst_size
     * 4. dst + len must not wrap around
     */

    /* Check for integer overflow in len + plane_size */
    if (len > SIZE_MAX - plane_size) {
        return -1; /* overflow detected */
    }

    /* Check that len fits within destination buffer */
    if (len > dst_size) {
        return -1; /* out of bounds */
    }

    /* Check that len + plane_size fits within destination buffer */
    if (len + plane_size > dst_size) {
        return -1; /* out of bounds */
    }

    /* Check pointer arithmetic overflow: dst + len must not wrap */
    if ((uintptr_t)dst > UINTPTR_MAX - len) {
        return -1; /* pointer overflow */
    }

    /* Check that header_size does not exceed len */
    if (header_size > len) {
        return -1; /* header larger than claimed len */
    }

    /* Safe to proceed */
    memcpy(dst, header, header_size < len ? header_size : len);
    memcpy(dst + len, plane, plane_size);

    return 0;
}

/* Test that adversarial 'len' values near integer boundaries are rejected */
START_TEST(test_packet_len_overflow_invariant)
{
    /* Invariant: pointer arithmetic with attacker-controlled 'len' must never
     * overflow or write outside the allocated destination buffer */

    uint8_t dst[BUFFER_SIZE];
    uint8_t header[HEADER_SIZE];
    uint8_t plane[64];

    memset(dst, 0, sizeof(dst));
    memset(header, 0xAA, sizeof(header));
    memset(plane, 0xBB, sizeof(plane));

    /* Adversarial len values that could cause pointer arithmetic overflow */
    size_t adversarial_lens[] = {
        SIZE_MAX,
        SIZE_MAX - 1,
        SIZE_MAX - 63,
        SIZE_MAX / 2,
        SIZE_MAX / 2 + 1,
        (size_t)INT_MAX,
        (size_t)INT_MAX + 1,
        (size_t)UINT_MAX,
        BUFFER_SIZE,          /* exactly at boundary - should fail for plane */
        BUFFER_SIZE + 1,      /* just over boundary */
        BUFFER_SIZE * 2,
        0xFFFFFF00,
        0xFFFFFFF0,
        0x80000000,
        0x7FFFFFFF,
        0xDEADBEEF,
        0xFFFFFFFF,
    };

    int num_lens = sizeof(adversarial_lens) / sizeof(adversarial_lens[0]);

    for (int i = 0; i < num_lens; i++) {
        size_t len = adversarial_lens[i];
        int result = safe_packet_process(dst, BUFFER_SIZE,
                                          header, HEADER_SIZE,
                                          plane, sizeof(plane),
                                          len);

        /* For adversarial large values, the function MUST reject them */
        if (len > BUFFER_SIZE - sizeof(plane)) {
            ck_assert_msg(result == -1,
                "SECURITY VIOLATION: adversarial len=0x%zx was not rejected "
                "(would cause out-of-bounds write)", len);
        }

        /* Verify destination buffer integrity - no corruption beyond bounds */
        /* The buffer should remain zeroed in the region we didn't write to */
        if (result == -1) {
            /* On rejection, dst should be unchanged */
            for (size_t j = 0; j < BUFFER_SIZE; j++) {
                ck_assert_msg(dst[j] == 0,
                    "SECURITY VIOLATION: buffer corrupted at index %zu "
                    "after rejected len=0x%zx", j, len);
            }
        }
    }
}
END_TEST

/* Test that integer overflow in len + plane_size is always caught */
START_TEST(test_packet_combined_overflow_invariant)
{
    /* Invariant: len + plane_size overflow must always be detected */

    uint8_t dst[BUFFER_SIZE];
    uint8_t header[HEADER_SIZE];
    uint8_t plane[64];

    memset(dst, 0, sizeof(dst));
    memset(header, 0xCC, sizeof(header));
    memset(plane, 0xDD, sizeof(plane));

    /* Pairs of (len, plane_size) that together overflow size_t */
    struct { size_t len; size_t plane_size; } overflow_pairs[] = {
        { SIZE_MAX,     1 },
        { SIZE_MAX - 1, 2 },
        { SIZE_MAX / 2 + 1, SIZE_MAX / 2 + 1 },
        { SIZE_MAX - 63, 64 },
        { SIZE_MAX - sizeof(plane) + 1, sizeof(plane) },
        { (size_t)UINT_MAX, (size_t)UINT_MAX },
        { 0x7FFFFFFF, 0x80000001 },
        { 0xFFFFFFF0, 0x10 },
        { 0xFFFFFF00, 0x100 },
    };

    int num_pairs = sizeof(overflow_pairs) / sizeof(overflow_pairs[0]);

    for (int i = 0; i < num_pairs; i++) {
        size_t len = overflow_pairs[i].len;
        size_t ps  = overflow_pairs[i].plane_size;

        int result = safe_packet_process(dst, BUFFER_SIZE,
                                          header, HEADER_SIZE,
                                          plane, ps,
                                          len);

        /* These must ALL be rejected - they would overflow */
        ck_assert_msg(result == -1,
            "SECURITY VIOLATION: overflow pair (len=0x%zx, plane_size=0x%zx) "
            "was not rejected", len, ps);

        /* Buffer must remain untouched */
        for (size_t j = 0; j < BUFFER_SIZE; j++) {
            ck_assert_msg(dst[j] == 0,
                "SECURITY VIOLATION: buffer corrupted at index %zu "
                "after rejected overflow pair", j);
        }
    }
}
END_TEST

/* Test that valid small values are accepted and written correctly */
START_TEST(test_packet_valid_inputs_accepted)
{
    /* Invariant: valid, safe inputs must be processed correctly */

    uint8_t dst[BUFFER_SIZE];
    uint8_t header[HEADER_SIZE];
    uint8_t plane[64];

    memset(header, 0xAA, sizeof(header));
    memset(plane, 0xBB, sizeof(plane));

    struct { size_t len; size_t plane_size; } valid_cases[] = {
        { 0,    0 },
        { 0,    64 },
        { 64,   64 },
        { 128,  64 },
        { BUFFER_SIZE - 64, 64 },
        { BUFFER_SIZE - 1,  0 },
        { 1,    1 },
        { 256,  256 },
    };

    int num_cases = sizeof(valid_cases) / sizeof(valid_cases[0]);

    for (int i = 0; i < num_cases; i++) {
        size_t len = valid_cases[i].len;
        size_t ps  = valid_cases[i].plane_size;

        memset(dst, 0, BUFFER_SIZE);

        int result = safe_packet_process(dst, BUFFER_SIZE,
                                          header, HEADER_SIZE,
                                          plane, ps,
                                          len);

        ck_assert_msg(result == 0,
            "Valid input rejected: len=%zu, plane_size=%zu", len, ps);

        /* Verify plane data was written at correct offset */
        if (ps > 0 && len + ps <= BUFFER_SIZE) {
            ck_assert_msg(memcmp(dst + len, plane, ps) == 0,
                "Plane data not written correctly at offset %zu", len);
        }
    }
}
END_TEST

/* Test pointer arithmetic overflow detection specifically */
START_TEST(test_pointer_arithmetic_overflow_invariant)
{
    /* Invariant: dst + len must never wrap around in pointer arithmetic */

    uint8_t dst[BUFFER_SIZE];
    uint8_t header[HEADER_SIZE];
    uint8_t plane[64];

    memset(dst, 0, sizeof(dst));
    memset(header, 0xEE, sizeof(header));
    memset(plane, 0xFF, sizeof(plane));

    /* Values designed to cause pointer wrap-around */
    size_t wrap_lens[] = {
        UINTPTR_MAX,
        UINTPTR_MAX - 1,
        UINTPTR_MAX - (uintptr_t)dst,
        UINTPTR_MAX - (uintptr_t)dst + 1,
        SIZE_MAX,
        SIZE_MAX - (size_t)((uintptr_t)dst & 0xFFFF),
    };

    int num_lens = sizeof(wrap_lens) / sizeof(wrap_lens[0]);

    for (int i = 0; i < num_lens; i++) {
        size_t len = wrap_lens[i];

        int result = safe_packet_process(dst, BUFFER_SIZE,
                                          header, HEADER_SIZE,
                                          plane, sizeof(plane),
                                          len);

        ck_assert_msg(result == -1,
            "SECURITY VIOLATION: pointer-wrapping len=0x%zx was not rejected",
            len);

        /* Buffer integrity check */
        for (size_t j = 0; j < BUFFER_SIZE; j++) {
            ck_assert_msg(dst[j] == 0,
                "SECURITY VIOLATION: buffer corrupted at index %zu "
                "after pointer-wrap attempt with len=0x%zx", j, len);
        }
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_packet_len_overflow_invariant);
    tcase_add_test(tc_core, test_packet_combined_overflow_invariant);
    tcase_add_test(tc_core, test_packet_valid_inputs_accepted);
    tcase_add_test(tc_core, test_pointer_arithmetic_overflow_invariant);

    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}