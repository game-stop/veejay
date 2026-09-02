#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libsubsample/subsample-arch.h>

#if defined(SUBSAMPLE_HAVE_ESP32)
#define backend_444_to_422 ss_444_to_422_drop_esp32
#define backend_422_to_444 tr_422_to_444_dup_esp32
#define backend_444_to_420 ss_444_to_420jpeg_esp32
#define backend_420_to_444 ss_420jpeg_to_444_esp32
#define BACKEND_NAME "ESP32"
#elif defined(SUBSAMPLE_HAVE_NEON)
#define backend_444_to_422 ss_444_to_422_drop_neon
#define backend_422_to_444 tr_422_to_444_dup_neon
#define backend_444_to_420 ss_444_to_420jpeg_neon
#define backend_420_to_444 ss_420jpeg_to_444_neon
#define BACKEND_NAME "NEON"
#elif defined(SUBSAMPLE_HAVE_ALTIVEC)
#define backend_444_to_422 ss_444_to_422_drop_altivec
#define backend_422_to_444 tr_422_to_444_dup_altivec
#define backend_444_to_420 ss_444_to_420jpeg_altivec
#define backend_420_to_444 ss_420jpeg_to_444_altivec
#define BACKEND_NAME "AltiVec"
#else
#error "Enable an architecture backend when compiling this test"
#endif

#define GUARD_BYTES 32

static uint32_t random_state = 0x9e3779b9u;

static uint8_t next_byte(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return (uint8_t)random_state;
}

static void fill_random(uint8_t *buffer, size_t length)
{
    for (size_t index = 0; index < length; index++)
        buffer[index] = next_byte();
}

static void reference_444_to_422(uint8_t *restrict U, uint8_t *restrict V,
                                 int width, int height)
{
    const size_t destination_pixels = ((size_t)width * height) >> 1;

    for (size_t pixel = 0; pixel < destination_pixels; pixel++) {
        const size_t source = pixel << 1;
        U[pixel] = U[source];
        V[pixel] = V[source];
    }
}

static void reference_422_to_444(uint8_t *chroma, int width, int height)
{
    const int source_width = width >> 1;

    for (int row = height - 1; row >= 0; row--) {
        uint8_t *source = chroma + (size_t)row * source_width;
        uint8_t *destination = chroma + (size_t)row * width;

        for (int column = source_width - 1; column >= 0; column--) {
            const uint8_t pixel = source[column];
            destination[column << 1] = pixel;
            destination[(column << 1) + 1] = pixel;
        }
    }
}

static void reference_444_to_420(uint8_t *buffer, int width, int height)
{
    uint8_t *destination = buffer;

    for (int row = 0; row + 1 < height; row += 2) {
        const uint8_t *top = buffer + (size_t)row * width;
        const uint8_t *bottom = top + width;

        for (int column = 0; column + 1 < width; column += 2) {
            *destination++ = (uint8_t)((top[column] +
                                        3 * (top[column + 1] + bottom[column]) +
                                        9 * bottom[column + 1] + 8) >> 4);
        }
    }
}

static void reference_420_to_444(uint8_t *buffer, int width, int height)
{
    const int source_width = width >> 1;
    const int source_height = height >> 1;

    for (int row = source_height - 1; row >= 0; row--) {
        uint8_t *source = buffer + (size_t)row * source_width;
        uint8_t *top = buffer + (size_t)(row << 1) * width;
        uint8_t *bottom = top + width;

        for (int column = source_width - 1; column >= 0; column--) {
            const uint8_t pixel = source[column];
            top[column << 1] = pixel;
            top[(column << 1) + 1] = pixel;
            bottom[column << 1] = pixel;
            bottom[(column << 1) + 1] = pixel;
        }
    }
}

static int compare_buffers(const char *conversion, const uint8_t *expected,
                           const uint8_t *actual, size_t length,
                           int width, int height)
{
    for (size_t index = 0; index < length; index++) {
        if (expected[index] != actual[index]) {
            fprintf(stderr,
                    "%s %s failed at %dx%d byte %zu: expected %u, got %u\n",
                    BACKEND_NAME, conversion, width, height, index,
                    expected[index], actual[index]);
            return 1;
        }
    }

    return 0;
}

static int test_dimensions(int width, int height, size_t offset)
{
    const size_t frame_size = (size_t)width * height;
    const size_t allocation_size = offset + frame_size + GUARD_BYTES;
    uint8_t *expected_u_base = malloc(allocation_size);
    uint8_t *actual_u_base = malloc(allocation_size);
    uint8_t *expected_v_base = malloc(allocation_size);
    uint8_t *actual_v_base = malloc(allocation_size);

    if (expected_u_base == NULL || actual_u_base == NULL ||
        expected_v_base == NULL || actual_v_base == NULL) {
        fprintf(stderr, "Unable to allocate test planes\n");
        free(expected_u_base);
        free(actual_u_base);
        free(expected_v_base);
        free(actual_v_base);
        return 1;
    }

    uint8_t *expected_u = expected_u_base + offset;
    uint8_t *actual_u = actual_u_base + offset;
    uint8_t *expected_v = expected_v_base + offset;
    uint8_t *actual_v = actual_v_base + offset;
    int failed = 0;

    fill_random(actual_u_base, allocation_size);
    fill_random(actual_v_base, allocation_size);
    memcpy(expected_u_base, actual_u_base, allocation_size);
    memcpy(expected_v_base, actual_v_base, allocation_size);
    reference_444_to_422(expected_u, expected_v, width, height);
    backend_444_to_422(actual_u, actual_v, width, height);
    failed |= compare_buffers("4:4:4 -> 4:2:2 U", expected_u_base, actual_u_base,
                              allocation_size, width, height);
    failed |= compare_buffers("4:4:4 -> 4:2:2 V", expected_v_base, actual_v_base,
                              allocation_size, width, height);

    fill_random(actual_u_base, allocation_size);
    memcpy(expected_u_base, actual_u_base, allocation_size);
    reference_422_to_444(expected_u, width, height);
    backend_422_to_444(actual_u, width, height);
    failed |= compare_buffers("4:2:2 -> 4:4:4", expected_u_base, actual_u_base,
                              allocation_size, width, height);

    fill_random(actual_u_base, allocation_size);
    memcpy(expected_u_base, actual_u_base, allocation_size);
    reference_444_to_420(expected_u, width, height);
    backend_444_to_420(actual_u, width, height);
    failed |= compare_buffers("4:4:4 -> 4:2:0", expected_u_base, actual_u_base,
                              allocation_size, width, height);

    fill_random(actual_u_base, allocation_size);
    memcpy(expected_u_base, actual_u_base, allocation_size);
    reference_420_to_444(expected_u, width, height);
    backend_420_to_444(actual_u, width, height);
    failed |= compare_buffers("4:2:0 -> 4:4:4", expected_u_base, actual_u_base,
                              allocation_size, width, height);

    free(expected_u_base);
    free(actual_u_base);
    free(expected_v_base);
    free(actual_v_base);
    return failed;
}

int main(void)
{
    static const struct {
        int width;
        int height;
    } dimensions[] = {
        { 2, 2 }, { 6, 4 }, { 8, 6 }, { 30, 8 }, { 32, 4 },
        { 34, 6 }, { 62, 10 }, { 64, 8 }, { 66, 6 }
    };

    for (size_t index = 0; index < sizeof(dimensions) / sizeof(dimensions[0]); index++) {
        if (test_dimensions(dimensions[index].width, dimensions[index].height,
                            (index % 3) + 1) != 0)
            return EXIT_FAILURE;
    }

    printf("%s subsampling backend passed\n", BACKEND_NAME);
    return EXIT_SUCCESS;
}