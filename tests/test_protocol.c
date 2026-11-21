/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "cros_fp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #expr); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

struct fake {
    uint8_t mask[4];
    unsigned calls;
    size_t response_size;
    uint32_t controller_result;
    enum cfp_status status;
};

static enum cfp_status exchange(void *context, uint16_t command, uint8_t version,
    const uint8_t *request, size_t request_size, uint8_t *response,
    size_t capacity, size_t *response_size, uint32_t *controller_result)
{
    struct fake *fake = context;
    ++fake->calls;
    CHECK(command == 0x0008); /* GET_CMD_VERSIONS, not FP_INFO or FP_MODE */
    CHECK(version == 1);      /* Supports 16-bit command IDs */
    CHECK(request_size == 2);
    CHECK(request[0] == 0x03 && request[1] == 0x04); /* FP_INFO, LE */
    CHECK(capacity == 4);
    memcpy(response, fake->mask, 4);
    *response_size = fake->response_size;
    *controller_result = fake->controller_result;
    return fake->status;
}

static void selects_v3(void)
{
    struct fake fake = {.mask = {0x0c, 0, 0, 0}, .response_size = 4};
    struct cfp_transport transport = {exchange, &fake};
    uint8_t version = 0xff;
    CHECK(cfp_negotiate_info(&transport, &version) == CFP_OK);
    CHECK(version == 3);
    CHECK(fake.calls == 1);
}

static void selects_v2(void)
{
    struct fake fake = {.mask = {0x04, 0, 0, 0}, .response_size = 4};
    struct cfp_transport transport = {exchange, &fake};
    uint8_t version = 0xff;
    CHECK(cfp_negotiate_info(&transport, &version) == CFP_OK);
    CHECK(version == 2);
    CHECK(fake.calls == 1);
}

static void rejects_unknown_versions(void)
{
    const uint8_t masks[][4] = {{0}, {3, 0, 0, 0}, {0, 0, 0, 0x80}};
    for (size_t i = 0; i < sizeof(masks) / sizeof(masks[0]); ++i) {
        struct fake fake = {.response_size = 4};
        memcpy(fake.mask, masks[i], 4);
        struct cfp_transport transport = {exchange, &fake};
        uint8_t version = 0xff;
        CHECK(cfp_negotiate_info(&transport, &version) == CFP_UNSUPPORTED_VERSION);
        CHECK(version == 0xff);
        CHECK(fake.calls == 1);
    }
}

static void ignores_future_versions(void)
{
    struct fake fake = {.mask = {0x0c, 0, 0, 0x80}, .response_size = 4};
    struct cfp_transport transport = {exchange, &fake};
    uint8_t version = 0xff;
    CHECK(cfp_negotiate_info(&transport, &version) == CFP_OK);
    CHECK(version == 3);
}

static void rejects_bad_lengths(void)
{
    const size_t lengths[] = {0, 1, 2, 3, 5, SIZE_MAX};
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        struct fake fake = {.mask = {0x0c}, .response_size = lengths[i]};
        struct cfp_transport transport = {exchange, &fake};
        uint8_t version = 0xff;
        CHECK(cfp_negotiate_info(&transport, &version) == CFP_MALFORMED_RESPONSE);
        CHECK(version == 0xff);
        CHECK(fake.calls == 1);
    }
}

static void reports_controller_failure(void)
{
    struct fake fake = {.mask = {0x0c}, .controller_result = 1};
    struct cfp_transport transport = {exchange, &fake};
    uint8_t version = 0xff;
    CHECK(cfp_negotiate_info(&transport, &version) == CFP_CONTROLLER_ERROR);
    CHECK(version == 0xff);
    CHECK(fake.calls == 1);
}

static void reports_transport_failure(void)
{
    struct fake fake = {.status = CFP_TRANSPORT_ERROR};
    struct cfp_transport transport = {exchange, &fake};
    uint8_t version = 0xff;
    CHECK(cfp_negotiate_info(&transport, &version) == CFP_TRANSPORT_ERROR);
    CHECK(version == 0xff);
    CHECK(fake.calls == 1);
}

static void rejects_invalid_arguments(void)
{
    struct fake fake = {0};
    struct cfp_transport transport = {exchange, &fake};
    struct cfp_transport missing = {0};
    uint8_t version = 0xff;
    CHECK(cfp_negotiate_info(NULL, &version) == CFP_INVALID_ARGUMENT);
    CHECK(cfp_negotiate_info(&missing, &version) == CFP_INVALID_ARGUMENT);
    CHECK(cfp_negotiate_info(&transport, NULL) == CFP_INVALID_ARGUMENT);
    CHECK(version == 0xff);
    CHECK(fake.calls == 0);
}

int main(int argc, char **argv)
{
    const struct { const char *name; void (*run)(void); } tests[] = {
        {"selects_v3", selects_v3},
        {"selects_v2", selects_v2},
        {"rejects_unknown_versions", rejects_unknown_versions},
        {"ignores_future_versions", ignores_future_versions},
        {"rejects_bad_lengths", rejects_bad_lengths},
        {"reports_controller_failure", reports_controller_failure},
        {"reports_transport_failure", reports_transport_failure},
        {"rejects_invalid_arguments", rejects_invalid_arguments},
    };
    size_t ran = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (argc > 1 && strcmp(argv[1], tests[i].name) != 0)
            continue;
        tests[i].run();
        printf("PASS: %s\n", tests[i].name);
        ++ran;
    }
    CHECK(ran > 0);
    printf("%zu tests passed\n", ran);
    return EXIT_SUCCESS;
}
