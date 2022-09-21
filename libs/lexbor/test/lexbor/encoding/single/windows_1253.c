/*
 * Copyright (C) 2019 Alexander Borisov
 *
 * Author: Alexander Borisov <borisov@lexbor.com>
 */

/*
 * Caution!
 * This file generated by the script "utils/lexbor/encoding/single-byte.py"!
 * Do not change this file!
 */

#include <unit/test.h>

#include <lexbor/encoding/encoding.h>
#include <lexbor/encoding/single.h>


TEST_BEGIN(decode)
{
    size_t size;
    lxb_char_t data;
    lxb_codepoint_t cp;
    const lxb_char_t *ref;
    const lxb_encoding_data_t *enc_data;
    const lxb_encoding_multi_index_t *entry;

    enc_data = lxb_encoding_data(LXB_ENCODING_WINDOWS_1253);

    size = sizeof(lxb_encoding_single_index_windows_1253)
           / sizeof(lxb_encoding_multi_index_t);

    test_ne(size, 0);

    for (lxb_codepoint_t i = 0; i < 0x80; i++) {
        lxb_encoding_decode_t ctx = {0};

        data = (lxb_char_t) (i);
        ref = &data;

        cp = enc_data->decode_single(&ctx, &ref, ref + 1);
        test_eq(cp, i);
    }

    for (size_t i = 0; i < size; i++) {
        lxb_encoding_decode_t ctx = {0};

        entry = &lxb_encoding_single_index_windows_1253[i];
        if (entry->codepoint > LXB_ENCODING_DECODE_MAX_CODEPOINT) {
            continue;
        }

        data = (lxb_char_t) (i + 0x80);
        ref = &data;

        cp = enc_data->decode_single(&ctx, &ref, ref + 1);
        test_eq(cp, entry->codepoint);
    }
}
TEST_END

TEST_BEGIN(encode)
{
    int8_t len;
    size_t size;
    lxb_char_t *ref, data;
    const lxb_encoding_data_t *enc_data;
    const lxb_encoding_multi_index_t *entry;

    enc_data = lxb_encoding_data(LXB_ENCODING_WINDOWS_1253);

    size = sizeof(lxb_encoding_single_index_windows_1253)
           / sizeof(lxb_encoding_multi_index_t);

    test_ne(size, 0);

    for (lxb_codepoint_t i = 0; i < 0x80; i++) {
        lxb_encoding_encode_t ctx = {0};

        ref = &data;

        len = enc_data->encode_single(&ctx, &ref, (ref + 1), i);
        test_eq_u_int(len, 1);
        test_eq(data, (lxb_char_t) i);
    }

    for (size_t i = 0; i < size; i++) {
        lxb_encoding_encode_t ctx = {0};

        entry = &lxb_encoding_single_index_windows_1253[i];
        if (entry->codepoint > LXB_ENCODING_DECODE_MAX_CODEPOINT) {
            continue;
        }

        ref = &data;

        len = enc_data->encode_single(&ctx, &ref, (ref + 1), entry->codepoint);
        test_eq_u_int(len, 1);
        test_eq(data, (lxb_char_t) (i + 0x80));
    }
}
TEST_END

int
main(int argc, const char * argv[])
{
    TEST_INIT();

    TEST_ADD(decode);
    TEST_ADD(encode);

    TEST_RUN("lexbor/encoding/windows_1253");
    TEST_RELEASE();
}
