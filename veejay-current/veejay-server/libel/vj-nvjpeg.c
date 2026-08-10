/* 
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 */

#include <config.h>
#include <libel/vj-nvjpeg.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_NVJPEG

#include <cuda_runtime_api.h>
#include <nvjpeg.h>
#include <dlfcn.h>
#ifdef HAVE_NVJPEG_CUDA_KERNEL
#include <libel/vj-nvjpeg-kernel.h>
#endif

typedef nvjpegStatus_t (*vj_nvjpeg_create_simple_fn)(nvjpegHandle_t *);
typedef nvjpegStatus_t (*vj_nvjpeg_destroy_fn)(nvjpegHandle_t);
typedef nvjpegStatus_t (*vj_nvjpeg_state_create_fn)(nvjpegHandle_t,
                                                     nvjpegJpegState_t *);
typedef nvjpegStatus_t (*vj_nvjpeg_state_destroy_fn)(nvjpegJpegState_t);
typedef nvjpegStatus_t (*vj_nvjpeg_get_image_info_fn)(nvjpegHandle_t,
                                                       const unsigned char *,
                                                       size_t,
                                                       int *,
                                                       nvjpegChromaSubsampling_t *,
                                                       int *,
                                                       int *);
typedef nvjpegStatus_t (*vj_nvjpeg_decode_fn)(nvjpegHandle_t,
                                              nvjpegJpegState_t,
                                              const unsigned char *,
                                              size_t,
                                              nvjpegOutputFormat_t,
                                              nvjpegImage_t *,
                                              cudaStream_t);

typedef cudaError_t (*vj_cuda_get_device_fn)(int *);
typedef cudaError_t (*vj_cuda_set_device_fn)(int);
typedef cudaError_t (*vj_cuda_stream_create_fn)(cudaStream_t *, unsigned int);
typedef cudaError_t (*vj_cuda_stream_destroy_fn)(cudaStream_t);
typedef cudaError_t (*vj_cuda_stream_sync_fn)(cudaStream_t);
typedef cudaError_t (*vj_cuda_malloc_pitch_fn)(void **,
                                                size_t *,
                                                size_t,
                                                size_t);
typedef cudaError_t (*vj_cuda_free_fn)(void *);
typedef cudaError_t (*vj_cuda_host_alloc_fn)(void **, size_t, unsigned int);
typedef cudaError_t (*vj_cuda_free_host_fn)(void *);
typedef cudaError_t (*vj_cuda_memcpy_2d_async_fn)(void *,
                                                  size_t,
                                                  const void *,
                                                  size_t,
                                                  size_t,
                                                  size_t,
                                                  enum cudaMemcpyKind,
                                                  cudaStream_t);
typedef const char *(*vj_cuda_get_error_string_fn)(cudaError_t);

#define VJ_NVJPEG_PLANES 3
#define VJ_NVJPEG_ERROR_SIZE 256

typedef enum {
    VJ_NVJPEG_CONVERSION_NONE = 0,
    VJ_NVJPEG_CONVERSION_420_TO_422,
    VJ_NVJPEG_CONVERSION_420_TO_444,
    VJ_NVJPEG_CONVERSION_422_TO_444
} vj_nvjpeg_conversion;

struct vj_nvjpeg_decoder {
    int active;
    int device;
    int width;
    int height;
    int chroma_conversion_ready;
    int chroma_conversion_disabled;
    int plane_width[VJ_NVJPEG_PLANES];
    int plane_height[VJ_NVJPEG_PLANES];
    nvjpegChromaSubsampling_t last_subsampling;
    vj_nvjpeg_conversion last_conversion;
    char last_error[VJ_NVJPEG_ERROR_SIZE];

    void *nvjpeg_library;
    void *cudart_library;

    nvjpegHandle_t handle;
    nvjpegJpegState_t state;
    cudaStream_t stream;
    nvjpegImage_t device_image;
    uint8_t *device_chroma_output[2];
    size_t device_chroma_output_pitch[2];

    uint8_t *host_allocation;
    uint8_t *host_plane[VJ_NVJPEG_PLANES];
    size_t host_pitch[VJ_NVJPEG_PLANES];

#ifdef HAVE_NVJPEG_CUDA_KERNEL
    vj_nvjpeg_upsample_mode upsample_mode;
#endif

    vj_nvjpeg_create_simple_fn nvjpeg_create_simple;
    vj_nvjpeg_destroy_fn nvjpeg_destroy;
    vj_nvjpeg_state_create_fn nvjpeg_state_create;
    vj_nvjpeg_state_destroy_fn nvjpeg_state_destroy;
    vj_nvjpeg_get_image_info_fn nvjpeg_get_image_info;
    vj_nvjpeg_decode_fn nvjpeg_decode;

    vj_cuda_get_device_fn cuda_get_device;
    vj_cuda_set_device_fn cuda_set_device;
    vj_cuda_stream_create_fn cuda_stream_create;
    vj_cuda_stream_destroy_fn cuda_stream_destroy;
    vj_cuda_stream_sync_fn cuda_stream_sync;
    vj_cuda_malloc_pitch_fn cuda_malloc_pitch;
    vj_cuda_free_fn cuda_free;
    vj_cuda_host_alloc_fn cuda_host_alloc;
    vj_cuda_free_host_fn cuda_free_host;
    vj_cuda_memcpy_2d_async_fn cuda_memcpy_2d_async;
    vj_cuda_get_error_string_fn cuda_get_error_string;
};

static void vj_nvjpeg_copy_reason(char *dst,
                                  size_t dst_size,
                                  const char *reason)
{
    if(!dst || dst_size == 0)
        return;

    snprintf(dst, dst_size, "%s", reason ? reason : "unknown error");
}

static void vj_nvjpeg_set_error(vj_nvjpeg_decoder *decoder,
                                const char *message)
{
    if(decoder)
        vj_nvjpeg_copy_reason(decoder->last_error,
                              sizeof(decoder->last_error),
                              message);
}

static const char *vj_nvjpeg_status_name(nvjpegStatus_t status)
{
    switch(status) {
        case NVJPEG_STATUS_SUCCESS: return "success";
        case NVJPEG_STATUS_NOT_INITIALIZED: return "not initialized";
        case NVJPEG_STATUS_INVALID_PARAMETER: return "invalid parameter";
        case NVJPEG_STATUS_BAD_JPEG: return "bad JPEG";
        case NVJPEG_STATUS_JPEG_NOT_SUPPORTED: return "JPEG not supported";
        case NVJPEG_STATUS_ALLOCATOR_FAILURE: return "allocator failure";
        case NVJPEG_STATUS_EXECUTION_FAILED: return "execution failed";
        case NVJPEG_STATUS_ARCH_MISMATCH: return "architecture mismatch";
        case NVJPEG_STATUS_INTERNAL_ERROR: return "internal error";
        default:
            if((int)status == 9)
                return "implementation not supported";
            if((int)status == 10)
                return "incomplete bitstream";
            return "unknown nvJPEG error";
    }
}

static const char *vj_nvjpeg_subsampling_name(
    nvjpegChromaSubsampling_t subsampling)
{
    switch(subsampling) {
        case NVJPEG_CSS_444: return "4:4:4";
        case NVJPEG_CSS_422: return "4:2:2";
        case NVJPEG_CSS_420: return "4:2:0";
        case NVJPEG_CSS_440: return "4:4:0";
        case NVJPEG_CSS_411: return "4:1:1";
        case NVJPEG_CSS_410: return "4:1:0";
        case NVJPEG_CSS_GRAY: return "grayscale";
        default: return "unknown";
    }
}

static const char *vj_cuda_status_name(vj_nvjpeg_decoder *decoder,
                                       cudaError_t status)
{
    if(decoder && decoder->cuda_get_error_string) {
        const char *message = decoder->cuda_get_error_string(status);
        if(message)
            return message;
    }
    return "unknown CUDA error";
}

static void *vj_dlopen_first(const char *const *names)
{
    for(size_t i = 0; names[i]; i++) {
        void *handle = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
        if(handle)
            return handle;
    }
    return NULL;
}

static int vj_load_symbol(void *library,
                          const char *name,
                          void *destination,
                          size_t destination_size)
{
    void *symbol;

    if(!library || !name || !destination ||
       destination_size != sizeof(symbol))
        return 0;

    dlerror();
    symbol = dlsym(library, name);
    if(!symbol || dlerror() != NULL)
        return 0;

    memcpy(destination, &symbol, sizeof(symbol));
    return 1;
}

#define VJ_LOAD_REQUIRED(decoder, library, member, symbol_name)             \
    do {                                                                    \
        if(!vj_load_symbol((library),                                       \
                           (symbol_name),                                   \
                           &(decoder)->member,                              \
                           sizeof((decoder)->member))) {                    \
            char load_error[VJ_NVJPEG_ERROR_SIZE];                          \
            snprintf(load_error, sizeof(load_error),                        \
                     "missing runtime symbol %s", (symbol_name));          \
            vj_nvjpeg_set_error((decoder), load_error);                     \
            goto fail;                                                      \
        }                                                                   \
    } while(0)

static int vj_nvjpeg_select_device(vj_nvjpeg_decoder *decoder)
{
    int current_device = -1;
    cudaError_t status;

    status = decoder->cuda_get_device(&current_device);
    if(status != cudaSuccess) {
        char message[VJ_NVJPEG_ERROR_SIZE];
        snprintf(message, sizeof(message), "cudaGetDevice failed: %s",
                 vj_cuda_status_name(decoder, status));
        vj_nvjpeg_set_error(decoder, message);
        return 0;
    }

    if(current_device != decoder->device) {
        status = decoder->cuda_set_device(decoder->device);
        if(status != cudaSuccess) {
            char message[VJ_NVJPEG_ERROR_SIZE];
            snprintf(message, sizeof(message), "cudaSetDevice(%d) failed: %s",
                     decoder->device,
                     vj_cuda_status_name(decoder, status));
            vj_nvjpeg_set_error(decoder, message);
            return 0;
        }
    }

    return 1;
}

#ifdef HAVE_NVJPEG_CUDA_KERNEL
static vj_nvjpeg_upsample_mode vj_nvjpeg_select_upsample_mode(void)
{
    const char *mode = getenv("VEEJAY_SUPERSAMPLE_MODE");

    if(mode && strcmp(mode, "mitchell") == 0)
        return VJ_NVJPEG_UPSAMPLE_MITCHELL;

    return VJ_NVJPEG_UPSAMPLE_DUP;
}

static int vj_nvjpeg_prepare_chroma_output(vj_nvjpeg_decoder *decoder)
{
    cudaError_t status;

    if(decoder->chroma_conversion_ready)
        return 1;
    if(decoder->chroma_conversion_disabled)
        return 0;

    for(int plane = 0; plane < 2; plane++) {
        status = decoder->cuda_malloc_pitch(
            (void **)&decoder->device_chroma_output[plane],
            &decoder->device_chroma_output_pitch[plane],
            (size_t)decoder->width,
            (size_t)decoder->height);
        if(status != cudaSuccess) {
            char message[VJ_NVJPEG_ERROR_SIZE];
            snprintf(message, sizeof(message),
                     "CUDA chroma output allocation failed: %s",
                     vj_cuda_status_name(decoder, status));
            vj_nvjpeg_set_error(decoder, message);

            for(int allocated = 0; allocated <= plane; allocated++) {
                if(decoder->device_chroma_output[allocated]) {
                    decoder->cuda_free(decoder->device_chroma_output[allocated]);
                    decoder->device_chroma_output[allocated] = NULL;
                    decoder->device_chroma_output_pitch[allocated] = 0;
                }
            }
            decoder->chroma_conversion_disabled = 1;
            return 0;
        }
    }

    decoder->chroma_conversion_ready = 1;
    return 1;
}
#endif

static void vj_nvjpeg_release(vj_nvjpeg_decoder *decoder)
{
    if(!decoder)
        return;

    if(decoder->device >= 0 && decoder->cuda_set_device)
        decoder->cuda_set_device(decoder->device);

    if(decoder->stream && decoder->cuda_stream_sync)
        decoder->cuda_stream_sync(decoder->stream);

    for(int plane = 0; plane < 2; plane++) {
        if(decoder->device_chroma_output[plane] && decoder->cuda_free) {
            decoder->cuda_free(decoder->device_chroma_output[plane]);
            decoder->device_chroma_output[plane] = NULL;
            decoder->device_chroma_output_pitch[plane] = 0;
        }
    }
    decoder->chroma_conversion_ready = 0;

    for(int plane = 0; plane < VJ_NVJPEG_PLANES; plane++) {
        if(decoder->device_image.channel[plane] && decoder->cuda_free) {
            decoder->cuda_free(decoder->device_image.channel[plane]);
            decoder->device_image.channel[plane] = NULL;
            decoder->device_image.pitch[plane] = 0;
        }
    }

    if(decoder->host_allocation && decoder->cuda_free_host) {
        decoder->cuda_free_host(decoder->host_allocation);
        decoder->host_allocation = NULL;
    }

    if(decoder->state && decoder->nvjpeg_state_destroy) {
        decoder->nvjpeg_state_destroy(decoder->state);
        decoder->state = NULL;
    }
    if(decoder->handle && decoder->nvjpeg_destroy) {
        decoder->nvjpeg_destroy(decoder->handle);
        decoder->handle = NULL;
    }
    if(decoder->stream && decoder->cuda_stream_destroy) {
        decoder->cuda_stream_destroy(decoder->stream);
        decoder->stream = NULL;
    }

    if(decoder->nvjpeg_library) {
        dlclose(decoder->nvjpeg_library);
        decoder->nvjpeg_library = NULL;
    }
    if(decoder->cudart_library) {
        dlclose(decoder->cudart_library);
        decoder->cudart_library = NULL;
    }

    decoder->device = -1;
    decoder->active = 0;
}

vj_nvjpeg_decoder *vj_nvjpeg_decoder_create(int width,
                                             int height,
                                             char *reason,
                                             size_t reason_size)
{
    static const char *const nvjpeg_names[] = {
        "libnvjpeg.so", "libnvjpeg.so.13", "libnvjpeg.so.12",
        "libnvjpeg.so.11", "libnvjpeg.so.10", NULL
    };
    static const char *const cudart_names[] = {
        "libcudart.so", "libcudart.so.13", "libcudart.so.12",
        "libcudart.so.11.0", "libcudart.so.10.2", NULL
    };
    vj_nvjpeg_decoder *decoder = NULL;
    nvjpegStatus_t nv_status;
    cudaError_t cuda_status;
    size_t luma_size;
    size_t host_size;

    if(reason && reason_size > 0)
        reason[0] = '\0';

    if(width <= 0 || height <= 0) {
        vj_nvjpeg_copy_reason(reason, reason_size, "invalid frame geometry");
        return NULL;
    }
    if((width & 1) != 0) {
        vj_nvjpeg_copy_reason(reason, reason_size,
                              "planar 4:2:2 requires an even frame width");
        return NULL;
    }

    decoder = calloc(1, sizeof(*decoder));
    if(!decoder) {
        vj_nvjpeg_copy_reason(reason, reason_size, "out of host memory");
        return NULL;
    }

    decoder->device = -1;
    decoder->width = width;
    decoder->height = height;
    decoder->last_subsampling = NVJPEG_CSS_UNKNOWN;
    decoder->last_conversion = VJ_NVJPEG_CONVERSION_NONE;
    decoder->plane_width[0] = width;
    decoder->plane_width[1] = (width + 1) / 2;
    decoder->plane_width[2] = (width + 1) / 2;
    decoder->plane_height[0] = height;
    decoder->plane_height[1] = height;
    decoder->plane_height[2] = height;
#ifdef HAVE_NVJPEG_CUDA_KERNEL
    decoder->upsample_mode = vj_nvjpeg_select_upsample_mode();
#else
    decoder->chroma_conversion_disabled = 1;
#endif

    if((size_t)width > SIZE_MAX / (size_t)height) {
        vj_nvjpeg_set_error(decoder, "frame geometry overflows size_t");
        goto fail;
    }
    luma_size = (size_t)width * (size_t)height;
    if(luma_size > SIZE_MAX / 3u) {
        vj_nvjpeg_set_error(decoder, "staging allocation size overflows size_t");
        goto fail;
    }
    host_size = 3u * luma_size;

    decoder->nvjpeg_library = vj_dlopen_first(nvjpeg_names);
    if(!decoder->nvjpeg_library) {
        vj_nvjpeg_set_error(decoder, "libnvjpeg runtime not found");
        goto fail;
    }
    decoder->cudart_library = vj_dlopen_first(cudart_names);
    if(!decoder->cudart_library) {
        vj_nvjpeg_set_error(decoder, "CUDA runtime not found");
        goto fail;
    }

    VJ_LOAD_REQUIRED(decoder, decoder->nvjpeg_library,
                     nvjpeg_create_simple, "nvjpegCreateSimple");
    VJ_LOAD_REQUIRED(decoder, decoder->nvjpeg_library,
                     nvjpeg_destroy, "nvjpegDestroy");
    VJ_LOAD_REQUIRED(decoder, decoder->nvjpeg_library,
                     nvjpeg_state_create, "nvjpegJpegStateCreate");
    VJ_LOAD_REQUIRED(decoder, decoder->nvjpeg_library,
                     nvjpeg_state_destroy, "nvjpegJpegStateDestroy");
    VJ_LOAD_REQUIRED(decoder, decoder->nvjpeg_library,
                     nvjpeg_get_image_info, "nvjpegGetImageInfo");
    VJ_LOAD_REQUIRED(decoder, decoder->nvjpeg_library,
                     nvjpeg_decode, "nvjpegDecode");

    VJ_LOAD_REQUIRED(decoder, decoder->cudart_library,
                     cuda_get_device, "cudaGetDevice");
    VJ_LOAD_REQUIRED(decoder, decoder->cudart_library,
                     cuda_set_device, "cudaSetDevice");
    VJ_LOAD_REQUIRED(decoder, decoder->cudart_library,
                     cuda_stream_create, "cudaStreamCreateWithFlags");
    VJ_LOAD_REQUIRED(decoder, decoder->cudart_library,
                     cuda_stream_destroy, "cudaStreamDestroy");
    VJ_LOAD_REQUIRED(decoder, decoder->cudart_library,
                     cuda_stream_sync, "cudaStreamSynchronize");
    VJ_LOAD_REQUIRED(decoder, decoder->cudart_library,
                     cuda_malloc_pitch, "cudaMallocPitch");
    VJ_LOAD_REQUIRED(decoder, decoder->cudart_library,
                     cuda_free, "cudaFree");
    VJ_LOAD_REQUIRED(decoder, decoder->cudart_library,
                     cuda_host_alloc, "cudaHostAlloc");
    VJ_LOAD_REQUIRED(decoder, decoder->cudart_library,
                     cuda_free_host, "cudaFreeHost");
    VJ_LOAD_REQUIRED(decoder, decoder->cudart_library,
                     cuda_memcpy_2d_async, "cudaMemcpy2DAsync");
    VJ_LOAD_REQUIRED(decoder, decoder->cudart_library,
                     cuda_get_error_string, "cudaGetErrorString");

    cuda_status = decoder->cuda_get_device(&decoder->device);
    if(cuda_status != cudaSuccess) {
        char message[VJ_NVJPEG_ERROR_SIZE];
        snprintf(message, sizeof(message), "cudaGetDevice failed: %s",
                 vj_cuda_status_name(decoder, cuda_status));
        vj_nvjpeg_set_error(decoder, message);
        goto fail;
    }

    nv_status = decoder->nvjpeg_create_simple(&decoder->handle);
    if(nv_status != NVJPEG_STATUS_SUCCESS) {
        char message[VJ_NVJPEG_ERROR_SIZE];
        snprintf(message, sizeof(message), "nvjpegCreateSimple failed: %s",
                 vj_nvjpeg_status_name(nv_status));
        vj_nvjpeg_set_error(decoder, message);
        goto fail;
    }

    nv_status = decoder->nvjpeg_state_create(decoder->handle,
                                              &decoder->state);
    if(nv_status != NVJPEG_STATUS_SUCCESS) {
        char message[VJ_NVJPEG_ERROR_SIZE];
        snprintf(message, sizeof(message), "nvjpegJpegStateCreate failed: %s",
                 vj_nvjpeg_status_name(nv_status));
        vj_nvjpeg_set_error(decoder, message);
        goto fail;
    }

    cuda_status = decoder->cuda_stream_create(&decoder->stream,
                                               cudaStreamNonBlocking);
    if(cuda_status != cudaSuccess) {
        char message[VJ_NVJPEG_ERROR_SIZE];
        snprintf(message, sizeof(message), "cudaStreamCreateWithFlags failed: %s",
                 vj_cuda_status_name(decoder, cuda_status));
        vj_nvjpeg_set_error(decoder, message);
        goto fail;
    }

    for(int plane = 0; plane < VJ_NVJPEG_PLANES; plane++) {
        cuda_status = decoder->cuda_malloc_pitch(
            (void **)&decoder->device_image.channel[plane],
            &decoder->device_image.pitch[plane],
            (size_t)decoder->plane_width[plane],
            (size_t)decoder->plane_height[plane]);
        if(cuda_status != cudaSuccess) {
            char message[VJ_NVJPEG_ERROR_SIZE];
            snprintf(message, sizeof(message),
                     "cudaMallocPitch plane %d failed: %s",
                     plane, vj_cuda_status_name(decoder, cuda_status));
            vj_nvjpeg_set_error(decoder, message);
            goto fail;
        }
    }

    cuda_status = decoder->cuda_host_alloc((void **)&decoder->host_allocation,
                                            host_size,
                                            cudaHostAllocDefault);
    if(cuda_status != cudaSuccess) {
        char message[VJ_NVJPEG_ERROR_SIZE];
        snprintf(message, sizeof(message), "cudaHostAlloc failed: %s",
                 vj_cuda_status_name(decoder, cuda_status));
        vj_nvjpeg_set_error(decoder, message);
        goto fail;
    }

    decoder->host_plane[0] = decoder->host_allocation;
    decoder->host_plane[1] = decoder->host_plane[0] + luma_size;
    decoder->host_plane[2] = decoder->host_plane[1] + luma_size;
    decoder->host_pitch[0] = (size_t)decoder->width;
    decoder->host_pitch[1] = (size_t)decoder->width;
    decoder->host_pitch[2] = (size_t)decoder->width;
    decoder->active = 1;
    vj_nvjpeg_set_error(decoder, "none");
    vj_nvjpeg_copy_reason(reason, reason_size, "ready");
    return decoder;

fail:
    vj_nvjpeg_copy_reason(reason, reason_size, decoder->last_error);
    vj_nvjpeg_release(decoder);
    free(decoder);
    return NULL;
}

int vj_nvjpeg_decoder_decode(vj_nvjpeg_decoder *decoder,
                             const uint8_t *jpeg_data,
                             size_t jpeg_size,
                             vj_nvjpeg_output requested_output,
                             vj_nvjpeg_output *actual_output,
                             uint8_t *const dst[3],
                             const size_t dst_pitch[3])
{
    int components = 0;
    int widths[NVJPEG_MAX_COMPONENT] = { 0 };
    int heights[NVJPEG_MAX_COMPONENT] = { 0 };
    nvjpegChromaSubsampling_t subsampling = NVJPEG_CSS_UNKNOWN;
    nvjpegStatus_t nv_status;
    cudaError_t cuda_status;
    vj_nvjpeg_output output = VJ_NVJPEG_OUTPUT_422;
    int source_chroma_width;
    int source_chroma_height;
    int target_chroma_width;
    int converted_chroma = 0;
    const uint8_t *copy_source[VJ_NVJPEG_PLANES];
    size_t copy_pitch[VJ_NVJPEG_PLANES];
    size_t copy_width[VJ_NVJPEG_PLANES];

    if(actual_output)
        *actual_output = VJ_NVJPEG_OUTPUT_422;
    if(decoder)
        decoder->last_conversion = VJ_NVJPEG_CONVERSION_NONE;

    if(!decoder || !decoder->active) {
        vj_nvjpeg_set_error(decoder, "backend is not active");
        return -1;
    }
    if(!jpeg_data || jpeg_size == 0 || !dst || !dst_pitch) {
        vj_nvjpeg_set_error(decoder, "invalid decode arguments");
        return -1;
    }
    if(requested_output != VJ_NVJPEG_OUTPUT_422 &&
       requested_output != VJ_NVJPEG_OUTPUT_444) {
        vj_nvjpeg_set_error(decoder, "invalid output format request");
        return -1;
    }
    for(int plane = 0; plane < VJ_NVJPEG_PLANES; plane++) {
        size_t required_pitch = (plane == 0 ||
                                 requested_output == VJ_NVJPEG_OUTPUT_444)
            ? (size_t)decoder->width
            : (size_t)decoder->plane_width[plane];
        if(!dst[plane] || dst_pitch[plane] < required_pitch) {
            vj_nvjpeg_set_error(decoder, "destination plane or pitch is invalid");
            return -1;
        }
    }

    if(!vj_nvjpeg_select_device(decoder))
        return -1;

    nv_status = decoder->nvjpeg_get_image_info(decoder->handle,
                                                jpeg_data,
                                                jpeg_size,
                                                &components,
                                                &subsampling,
                                                widths,
                                                heights);
    if(nv_status != NVJPEG_STATUS_SUCCESS) {
        char message[VJ_NVJPEG_ERROR_SIZE];
        snprintf(message, sizeof(message), "nvjpegGetImageInfo failed: %s",
                 vj_nvjpeg_status_name(nv_status));
        vj_nvjpeg_set_error(decoder, message);
        return -1;
    }

    decoder->last_subsampling = subsampling;

    if(components != VJ_NVJPEG_PLANES ||
       (subsampling != NVJPEG_CSS_420 &&
        subsampling != NVJPEG_CSS_422)) {
        char message[VJ_NVJPEG_ERROR_SIZE];
        snprintf(message, sizeof(message),
                 "unsupported JPEG layout (components=%d subsampling=%s/%d); expected planar 4:2:0 or 4:2:2",
                 components,
                 vj_nvjpeg_subsampling_name(subsampling),
                 (int)subsampling);
        vj_nvjpeg_set_error(decoder, message);
        return -1;
    }

    source_chroma_width = decoder->plane_width[1];
    source_chroma_height = subsampling == NVJPEG_CSS_420
        ? decoder->height / 2 + decoder->height % 2
        : decoder->height;

    if(widths[0] != decoder->width || heights[0] != decoder->height ||
       widths[1] != source_chroma_width ||
       widths[2] != source_chroma_width ||
       heights[1] != source_chroma_height ||
       heights[2] != source_chroma_height) {
        char message[VJ_NVJPEG_ERROR_SIZE];
        snprintf(message, sizeof(message),
                 "JPEG geometry does not match %dx%d %s planes (Y=%dx%d U=%dx%d V=%dx%d)",
                 decoder->width,
                 decoder->height,
                 vj_nvjpeg_subsampling_name(subsampling),
                 widths[0], heights[0],
                 widths[1], heights[1],
                 widths[2], heights[2]);
        vj_nvjpeg_set_error(decoder, message);
        return -1;
    }

#ifndef HAVE_NVJPEG_CUDA_KERNEL
    if(subsampling == NVJPEG_CSS_420) {
        vj_nvjpeg_set_error(
            decoder,
            "planar JPEG 4:2:0 requires the CUDA chroma supersampling kernel");
        return -1;
    }
#endif

    nv_status = decoder->nvjpeg_decode(decoder->handle,
                                        decoder->state,
                                        jpeg_data,
                                        jpeg_size,
                                        NVJPEG_OUTPUT_UNCHANGED,
                                        &decoder->device_image,
                                        decoder->stream);
    if(nv_status != NVJPEG_STATUS_SUCCESS) {
        char message[VJ_NVJPEG_ERROR_SIZE];
        snprintf(message, sizeof(message), "nvjpegDecode failed: %s",
                 vj_nvjpeg_status_name(nv_status));
        vj_nvjpeg_set_error(decoder, message);
        return -1;
    }

    target_chroma_width = requested_output == VJ_NVJPEG_OUTPUT_444
        ? decoder->width
        : decoder->plane_width[1];

#ifdef HAVE_NVJPEG_CUDA_KERNEL
    if(source_chroma_width != target_chroma_width ||
       source_chroma_height != decoder->height) {
        if(vj_nvjpeg_prepare_chroma_output(decoder)) {
            cuda_status = vj_nvjpeg_upsample_chroma(
                decoder->device_image.channel[1],
                decoder->device_image.pitch[1],
                decoder->device_image.channel[2],
                decoder->device_image.pitch[2],
                decoder->device_chroma_output[0],
                decoder->device_chroma_output_pitch[0],
                decoder->device_chroma_output[1],
                decoder->device_chroma_output_pitch[1],
                source_chroma_width,
                source_chroma_height,
                target_chroma_width,
                decoder->height,
                decoder->upsample_mode,
                decoder->stream);
            if(cuda_status == cudaSuccess) {
                converted_chroma = 1;
                output = requested_output;
                if(subsampling == NVJPEG_CSS_420) {
                    decoder->last_conversion =
                        output == VJ_NVJPEG_OUTPUT_444
                            ? VJ_NVJPEG_CONVERSION_420_TO_444
                            : VJ_NVJPEG_CONVERSION_420_TO_422;
                }
                else {
                    decoder->last_conversion =
                        VJ_NVJPEG_CONVERSION_422_TO_444;
                }
            }
            else {
                char message[VJ_NVJPEG_ERROR_SIZE];
                snprintf(message, sizeof(message),
                         "CUDA chroma supersampling launch failed: %s",
                         vj_cuda_status_name(decoder, cuda_status));
                decoder->chroma_conversion_disabled = 1;
                vj_nvjpeg_set_error(decoder, message);
            }
        }

        if(!converted_chroma && subsampling == NVJPEG_CSS_420)
            return -1;
    }
#endif

    copy_source[0] = decoder->device_image.channel[0];
    copy_pitch[0] = decoder->device_image.pitch[0];
    copy_width[0] = (size_t)decoder->width;

    if(converted_chroma) {
        copy_source[1] = decoder->device_chroma_output[0];
        copy_source[2] = decoder->device_chroma_output[1];
        copy_pitch[1] = decoder->device_chroma_output_pitch[0];
        copy_pitch[2] = decoder->device_chroma_output_pitch[1];
        copy_width[1] = (size_t)target_chroma_width;
        copy_width[2] = (size_t)target_chroma_width;
    }
    else {
        copy_source[1] = decoder->device_image.channel[1];
        copy_source[2] = decoder->device_image.channel[2];
        copy_pitch[1] = decoder->device_image.pitch[1];
        copy_pitch[2] = decoder->device_image.pitch[2];
        copy_width[1] = (size_t)decoder->plane_width[1];
        copy_width[2] = (size_t)decoder->plane_width[2];
    }

    for(int plane = 0; plane < VJ_NVJPEG_PLANES; plane++) {
        /* Pack the persistent pinned staging plane at the actual output
         * width. The common delivery path can then use one contiguous host
         * copy per plane instead of one memcpy call per scanline. */
        decoder->host_pitch[plane] = copy_width[plane];
        cuda_status = decoder->cuda_memcpy_2d_async(
            decoder->host_plane[plane],
            decoder->host_pitch[plane],
            copy_source[plane],
            copy_pitch[plane],
            copy_width[plane],
            (size_t)decoder->height,
            cudaMemcpyDeviceToHost,
            decoder->stream);
        if(cuda_status != cudaSuccess) {
            char message[VJ_NVJPEG_ERROR_SIZE];
            snprintf(message, sizeof(message),
                     "cudaMemcpy2DAsync plane %d failed: %s",
                     plane, vj_cuda_status_name(decoder, cuda_status));
            vj_nvjpeg_set_error(decoder, message);
            return -1;
        }
    }

    cuda_status = decoder->cuda_stream_sync(decoder->stream);
    if(cuda_status != cudaSuccess) {
        char message[VJ_NVJPEG_ERROR_SIZE];
        snprintf(message, sizeof(message), "cudaStreamSynchronize failed: %s",
                 vj_cuda_status_name(decoder, cuda_status));
        vj_nvjpeg_set_error(decoder, message);
        return -1;
    }

    for(int plane = 0; plane < VJ_NVJPEG_PLANES; plane++) {
        size_t row_bytes = copy_width[plane];
        size_t output_pitch = dst_pitch[plane];
        if(plane > 0 && output == VJ_NVJPEG_OUTPUT_422 &&
           requested_output == VJ_NVJPEG_OUTPUT_444)
            output_pitch = (size_t)decoder->plane_width[plane];
        if(output_pitch == row_bytes &&
           decoder->host_pitch[plane] == row_bytes) {
            memcpy(dst[plane], decoder->host_plane[plane],
                   row_bytes * (size_t)decoder->height);
        }
        else {
            for(int row = 0; row < decoder->height; row++) {
                memcpy(dst[plane] + (size_t)row * output_pitch,
                       decoder->host_plane[plane] +
                           (size_t)row * decoder->host_pitch[plane],
                       row_bytes);
            }
        }
    }

    if(actual_output)
        *actual_output = output;
    vj_nvjpeg_set_error(decoder, "none");
    return 1;
}

void vj_nvjpeg_decoder_retire(vj_nvjpeg_decoder *decoder)
{
    vj_nvjpeg_release(decoder);
}

void vj_nvjpeg_decoder_destroy(vj_nvjpeg_decoder *decoder)
{
    if(!decoder)
        return;
    vj_nvjpeg_release(decoder);
    free(decoder);
}

int vj_nvjpeg_decoder_is_active(const vj_nvjpeg_decoder *decoder)
{
    return decoder && decoder->active;
}

int vj_nvjpeg_decoder_supports_444(const vj_nvjpeg_decoder *decoder)
{
#ifdef HAVE_NVJPEG_CUDA_KERNEL
    return decoder && decoder->active &&
        !decoder->chroma_conversion_disabled;
#else
    (void)decoder;
    return 0;
#endif
}

const char *vj_nvjpeg_decoder_engine(const vj_nvjpeg_decoder *decoder)
{
    return decoder && decoder->active ? "nvjpeg" : "unavailable";
}

const char *vj_nvjpeg_decoder_upsampler(const vj_nvjpeg_decoder *decoder)
{
#ifdef HAVE_NVJPEG_CUDA_KERNEL
    if(!decoder || !decoder->active || decoder->chroma_conversion_disabled)
        return "unavailable";
    return decoder->upsample_mode == VJ_NVJPEG_UPSAMPLE_MITCHELL
        ? "mitchell"
        : "dup";
#else
    (void)decoder;
    return "unavailable";
#endif
}

const char *vj_nvjpeg_decoder_source_format(const vj_nvjpeg_decoder *decoder)
{
    if(!decoder)
        return "unknown";

    switch(decoder->last_subsampling) {
        case NVJPEG_CSS_420: return "yuvj420p";
        case NVJPEG_CSS_422: return "yuvj422p";
        default: return "unknown";
    }
}

const char *vj_nvjpeg_decoder_conversion(const vj_nvjpeg_decoder *decoder)
{
    if(!decoder)
        return "none";

#ifdef HAVE_NVJPEG_CUDA_KERNEL
    switch(decoder->last_conversion) {
        case VJ_NVJPEG_CONVERSION_420_TO_422:
            return decoder->upsample_mode == VJ_NVJPEG_UPSAMPLE_MITCHELL
                ? "gpu/420-to-422/mitchell"
                : "gpu/420-to-422/dup";
        case VJ_NVJPEG_CONVERSION_420_TO_444:
            return decoder->upsample_mode == VJ_NVJPEG_UPSAMPLE_MITCHELL
                ? "gpu/420-to-444/mitchell"
                : "gpu/420-to-444/dup";
        case VJ_NVJPEG_CONVERSION_422_TO_444:
            return decoder->upsample_mode == VJ_NVJPEG_UPSAMPLE_MITCHELL
                ? "gpu/422-to-444/mitchell"
                : "gpu/422-to-444/dup";
        default:
            break;
    }
#endif

    return "none";
}

const char *vj_nvjpeg_decoder_last_error(const vj_nvjpeg_decoder *decoder)
{
    return decoder && decoder->last_error[0]
        ? decoder->last_error
        : "unknown error";
}

#else /* !HAVE_NVJPEG */

struct vj_nvjpeg_decoder {
    int unused;
};

static void vj_nvjpeg_copy_reason(char *dst,
                                  size_t dst_size,
                                  const char *reason)
{
    if(dst && dst_size > 0)
        snprintf(dst, dst_size, "%s", reason);
}

vj_nvjpeg_decoder *vj_nvjpeg_decoder_create(int width,
                                             int height,
                                             char *reason,
                                             size_t reason_size)
{
    (void)width;
    (void)height;
    vj_nvjpeg_copy_reason(reason, reason_size,
                          "nvJPEG support was not built");
    return NULL;
}

void vj_nvjpeg_decoder_destroy(vj_nvjpeg_decoder *decoder)
{
    (void)decoder;
}

int vj_nvjpeg_decoder_decode(vj_nvjpeg_decoder *decoder,
                             const uint8_t *jpeg_data,
                             size_t jpeg_size,
                             vj_nvjpeg_output requested_output,
                             vj_nvjpeg_output *actual_output,
                             uint8_t *const dst[3],
                             const size_t dst_pitch[3])
{
    (void)decoder;
    (void)jpeg_data;
    (void)jpeg_size;
    (void)requested_output;
    if(actual_output)
        *actual_output = VJ_NVJPEG_OUTPUT_422;
    (void)dst;
    (void)dst_pitch;
    return -1;
}

void vj_nvjpeg_decoder_retire(vj_nvjpeg_decoder *decoder)
{
    (void)decoder;
}

int vj_nvjpeg_decoder_is_active(const vj_nvjpeg_decoder *decoder)
{
    (void)decoder;
    return 0;
}

int vj_nvjpeg_decoder_supports_444(const vj_nvjpeg_decoder *decoder)
{
    (void)decoder;
    return 0;
}

const char *vj_nvjpeg_decoder_engine(const vj_nvjpeg_decoder *decoder)
{
    (void)decoder;
    return "unavailable";
}

const char *vj_nvjpeg_decoder_upsampler(const vj_nvjpeg_decoder *decoder)
{
    (void)decoder;
    return "unavailable";
}

const char *vj_nvjpeg_decoder_source_format(const vj_nvjpeg_decoder *decoder)
{
    (void)decoder;
    return "unknown";
}

const char *vj_nvjpeg_decoder_conversion(const vj_nvjpeg_decoder *decoder)
{
    (void)decoder;
    return "none";
}

const char *vj_nvjpeg_decoder_last_error(const vj_nvjpeg_decoder *decoder)
{
    (void)decoder;
    return "nvJPEG support was not built";
}

#endif /* HAVE_NVJPEG */
