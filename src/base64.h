#ifndef _JS_INC_BASE64_H
#define _JS_INC_BASE64_H

// returns bytes written to output not including null terminator
// returns 0 if output buffer is not big enough
// output is a 0 terminated string
size_t base64_encode(char *output, size_t outputLen, const unsigned char *input, size_t inputLen);

// returns bytes written to output
// returns 0 if output buffer is not big enough or input is not a valid base64 string
size_t base64_decode(unsigned char *output, size_t outputLen, const char *input, size_t inputLen);

// returns the length of a string that would encode the passed amount of bytes
// includes null terminator
#define base64_strlen(byteLen) (size_t)(((byteLen) + 2) / 3 * 4 + 1)

#ifdef JS_BASE64_IMPLEMENTATION

#include <assert.h>

static const char BASE64_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char BASE64_PADDING = '=';
static const char BASE64_DECODE_LOOKUP[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0-15
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 16-31
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, // 32-47 ('+', '/')
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1, // 48-63 ('0'-'9')
    -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, // 64-79 ('A'-'O')
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, // 80-95 ('P'-'Z')
    -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, // 96-111 ('a'-'o')
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, // 112-127 ('p'-'z')
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 128-143
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 144-159
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 160-175
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 176-191
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 192-207
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 208-223
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 224-239
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1  // 240-255
};

size_t base64_encode(char *output, size_t outputLen, const unsigned char *input, size_t inputLen)
{
    assert(output);
    assert(input);
    assert(base64_strlen(inputLen) > inputLen); // test for rollover

    // size check (base64 encodes 3 bytes as 4 chars + null terminator)
    if (outputLen < base64_strlen(inputLen))
        return 0;

    size_t idx;
    size_t written = 0;
    for (idx = 0; idx+2 < inputLen; idx += 3)
    {
        unsigned int value = (input[idx] << 16) | (input[idx+1] << 8) | input[idx+2];
        output[written++] = BASE64_CHARS[(value >> 18) & 0b00111111];
        output[written++] = BASE64_CHARS[(value >> 12) & 0b00111111];
        output[written++] = BASE64_CHARS[(value >>  6) & 0b00111111];
        output[written++] = BASE64_CHARS[ value        & 0b00111111];
    }
    // remainder bytes need padding
    if (idx+1 < inputLen)
    {
        unsigned int value = (input[idx] << 16) | (input[idx+1] << 8);
        output[written++] = BASE64_CHARS[(value >> 18) & 0b00111111];
        output[written++] = BASE64_CHARS[(value >> 12) & 0b00111111];
        output[written++] = BASE64_CHARS[(value >>  6) & 0b00111111];
        output[written++] = BASE64_PADDING;
    }
    else if (idx < inputLen)
    {
        unsigned int value = input[idx] << 16;
        output[written++] = BASE64_CHARS[(value >> 18) & 0b00111111];
        output[written++] = BASE64_CHARS[(value >> 12) & 0b00111111];
        output[written++] = BASE64_PADDING;
        output[written++] = BASE64_PADDING;
    }

    output[written] = 0;
    assert(written < outputLen);

    return written;
}

size_t base64_decode(unsigned char *output, size_t outputLen, const char *input, size_t inputLen)
{
    assert(output);
    assert(input);

    if (inputLen % 4 != 0) // invalid base64 string
        return 0;
    if (outputLen < inputLen / 4 * 3)
        return 0;

    // Check for padding
    if (input[inputLen-1] == BASE64_PADDING)
        inputLen -= 1;
    if (input[inputLen-1] == BASE64_PADDING)
        inputLen -= 1;

    size_t written = 0;
    size_t idx;
    for (idx = 0; idx+3 < inputLen; idx += 4)
    {
        char firstSix  = BASE64_DECODE_LOOKUP[(unsigned char)input[idx]];
        char secondSix = BASE64_DECODE_LOOKUP[(unsigned char)input[idx+1]];
        char thirdSix  = BASE64_DECODE_LOOKUP[(unsigned char)input[idx+2]];
        char fourthSix = BASE64_DECODE_LOOKUP[(unsigned char)input[idx+3]];
        if (firstSix == -1 || secondSix == -1 || thirdSix == -1 || fourthSix == -1)
            return 0;
        unsigned int value = (firstSix << 18) | (secondSix << 12) | (thirdSix << 6) | fourthSix;
        output[written++] = ((unsigned char*)&value)[2];
        output[written++] = ((unsigned char*)&value)[1];
        output[written++] = ((unsigned char*)&value)[0];
    }
    // Handle padding
    if (inputLen - idx == 3)
    {
        char firstSix  = BASE64_DECODE_LOOKUP[(unsigned char)input[idx]];
        char secondSix = BASE64_DECODE_LOOKUP[(unsigned char)input[idx+1]];
        char thirdSix  = BASE64_DECODE_LOOKUP[(unsigned char)input[idx+2]];
        if (firstSix == -1 || secondSix == -1 || thirdSix == -1)
            return 0;
        unsigned int value = (firstSix << 18) | (secondSix << 12) | (thirdSix << 6);
        output[written++] = ((unsigned char*)&value)[2];
        output[written++] = ((unsigned char*)&value)[1];
    }
    else if (inputLen - idx == 2)
    {
        char firstSix  = BASE64_DECODE_LOOKUP[(unsigned char)input[idx]];
        char secondSix = BASE64_DECODE_LOOKUP[(unsigned char)input[idx+1]];
        if (firstSix == -1 || secondSix == -1)
            return 0;
        unsigned int value = (firstSix << 18) | (secondSix << 12);
        output[written++] = ((unsigned char*)&value)[2];
    }

    assert(written < outputLen);

    return written;
}

#ifdef JS_BASE64_TEST

#include <string.h>
#include <assert.h>
#include <stdio.h>

static void _base64_enc_test_single(const unsigned char *input, int inputLen, const char *expectOut, int expectOutLen)
{
    char base64out[256] = "";
    int len = base64_encode(base64out, sizeof(base64out), input, inputLen);
    assert(len == expectOutLen);
    assert(memcmp(base64out, expectOut, expectOutLen) == 0);
}

static void _base64_dec_test_single(const char *input, int inputLen, const unsigned char *expectOut, int expectOutLen)
{
    unsigned char base64out[256] = {0};
    int len = base64_decode(base64out, sizeof(base64out), input, inputLen);
    assert(len == expectOutLen);
    assert(memcmp(base64out, expectOut, expectOutLen) == 0);
}

void base64_test()
{
    // basic encode tests
    unsigned char base64in[8] = { 123, 255, 64, 0, 128, 177, 42, 99 };
    _base64_enc_test_single("Hello", 5, "SGVsbG8=", 8);
    _base64_enc_test_single("💩", 4, "8J+SqQ==", 8);
    _base64_enc_test_single(base64in, 1, "ew==", 4);
    _base64_enc_test_single(base64in, 3, "e/9A", 4);
    _base64_enc_test_single(base64in, 8, "e/9AAICxKmM=", 12);
    
    // insufficient encode output length tests
    char base64out[256] = "";
    int len = base64_encode(base64out, 3, "H", 1);
    assert(len == 0);
    len = base64_encode(base64out, 4, "H", 1); // not enough because of null terminator
    assert(len == 0);
    len = base64_encode(base64out, 7, "Hello", 5);
    assert(len == 0);
    len = base64_encode(base64out, 8, "Hello", 5); // not enough because of null terminator
    assert(len == 0);

    // basic decode tests
    _base64_dec_test_single("SGVsbG8=", 8, "Hello", 5);
    _base64_dec_test_single("8J+SqQ==", 8, "💩", 4);
    _base64_dec_test_single("ew==", 4, base64in, 1);
    _base64_dec_test_single("e/9A", 4, base64in, 3);
    _base64_dec_test_single("e/9AAICxKmM=", 12, base64in, 8);

    // insufficient decode output length tests
    len = base64_decode(base64out, 2, "e/9A", 4);
    assert(len == 0);
    len = base64_decode(base64out, 7, "e/9AAICxKmM=", 12);
    assert(len == 0);

    // invalid character tests
    len = base64_decode(base64out, sizeof(base64out), "ew_=", 4);
    assert(len == 0);
    len = base64_decode(base64out, sizeof(base64out), "ew=_", 4);
    assert(len == 0);
    len = base64_decode(base64out, sizeof(base64out), "eww_", 4);
    assert(len == 0);
    len = base64_decode(base64out, sizeof(base64out), "/===", 4);
    assert(len == 0);
    len = base64_decode(base64out, sizeof(base64out), "e/9A\0\t\r\n", 8);
    assert(len == 0);
    len = base64_decode(base64out, sizeof(base64out), "💩", 4);
    assert(len == 0);

    // invalid length tests
    len = base64_decode(base64out, sizeof(base64out), "asd", 3);
    assert(len == 0);
    len = base64_decode(base64out, sizeof(base64out), "asdfghi", 7);
    assert(len == 0);

    printf("Base64 tests successful!\n");
}

#else

void base64_test() { }

#endif // JS_BASE64_TEST
#endif // JS_BASE64_IMPLEMENTATION
#endif // _JS_INC_BASE64_H