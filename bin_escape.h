#pragma once

#define Z16 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
static const unsigned char escape_table_256[256] = {
    // 0    1    2    3    4    5    6    7    8    9    A    B    C    D    E    F
      '0',  0,   0,   0,   0,   0,   0,   0,  'b', 't', 'n',   0, 'f', 'r',  0,   0,    // 00~0F
       0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    // 10~1F
       0,   0, '\"',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  '/',   // 20~2F
     Z16,                                                                               // 30~3F
     Z16,                                                                               // 40~4F
       0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  '\\', 0,   0,   0,    // 50~5F
     Z16,                                                                               // 60~6F
     Z16,                                                                               // 70~7F
     Z16, Z16, Z16, Z16, Z16, Z16, Z16, Z16                                             // 80~FF
};
#undef Z16

#define O16 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
static const unsigned char unescape_table_256[256] = {
    // 0    1    2    3    4    5    6    7    8    9    A    B    C    D    E    F
     O16,                                                                               // 00~0F
     O16,                                                                               // 10~1F
       1,   1, '\"',  1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,  '/',   // 20~2F
     '\0',  1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,    // 30~3F
     O16,                                                                               // 40~4F
       1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,  '\\', 1,   1,   1,    // 50~5F
       1,   1, '\b',  1,   1,   1, '\f',  1,   1,   1,   1,   1,   1,   1, '\n',  1,    // 60~6F
       1,   1, '\r',  1, '\t',  1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,    // 70~7F
     O16, O16, O16, O16, O16, O16, O16, O16                                             // 80~FF
};
#undef O16

static inline
std::size_t bin_escape_get_encode_capacity(std::size_t data_length,
    bool is_twice_escape = false, bool add_quote = false) {
    std::size_t capacity;
    // One char maybe convent to two escape chars,
    // if is twice escape, one char maybe convent to four escape chars.
    if (!is_twice_escape)
        capacity = data_length * 2 + 1;
    else
        capacity = data_length * 4 + 1;
    // Add double quote and nullor format like: "xxxxxx" '\0'
    if (add_quote)
        capacity += 2;
    return capacity;
}

static inline
std::size_t bin_escape_get_decode_capacity(std::size_t data_length) {
    return data_length;
}

static std::size_t bin_escape_encode(const char * data, std::size_t data_len,
    char * dest, std::size_t max_size, bool fill_null = true) {
    unsigned char * src = reinterpret_cast<unsigned char *>(const_cast<char *>(data));
    unsigned char * src_end = src + data_len;
    char * dest_start = dest;
    char * dest_max = dest + max_size;
    if (fill_null)
        dest_max -= 1;
    // The json buffer size must be large than data length.
    assert(max_size >= data_len);
    // dest can not overflow dest_max forever.
    while (src < src_end) {
        unsigned char c = *src;
        unsigned char escape = escape_table_256[c];
        if (escape == 0) {
            *dest++ = c;
            assert(dest <= dest_max);
            src++;
        }
        else {
            *dest++ = '\\';
            *dest++ = escape;
            assert(dest <= dest_max);
            src++;
        }
    }
    assert(dest != nullptr);
    if (fill_null)
        *dest = '\0';
    assert(dest >= dest_start);
    return (dest - dest_start);
}

static std::size_t bin_escape_encode_twice(const char * data, std::size_t data_len,
    char * dest, std::size_t max_size, bool fill_null = true) {
    unsigned char * src = reinterpret_cast<unsigned char *>(const_cast<char *>(data));
    unsigned char * src_end = src + data_len;
    char * dest_start = dest;
    char * dest_max = dest + max_size;
    if (fill_null)
        dest_max -= 1;
    // The json buffer size must be large than data length.
    assert(max_size >= data_len);
    // dest can not overflow dest_max forever.
    while (src < src_end) {
        unsigned char c = *src;
        unsigned char escape = escape_table_256[c];
        if (escape == 0) {
            *dest++ = c;
            assert(dest <= dest_max);
            src++;
        }
        else {
            *dest++ = '\\';
            *dest++ = '\\';
            // '\\', '\"', '\/'
            if (c > 32)
                *dest++ = '\\';
            *dest++ = escape;
            assert(dest <= dest_max);
            src++;
        }
    }
    assert(dest != nullptr);
    if (fill_null)
        *dest = '\0';
    assert(dest >= dest_start);
    return (dest - dest_start);
}

static std::streamsize bin_escape_encode(const char * src, std::size_t src_len, std::string & dest,
    bool fill_null = true, bool is_twice_escape = false, bool add_quote = false) {
    std::size_t alloc_size = bin_escape_get_encode_capacity(src_len, is_twice_escape, add_quote);
    dest.resize(alloc_size);
    char * buffer = &dest[0];
    if (add_quote)
        *buffer++ = '\"';
    std::streamsize encode_size;
    if (!is_twice_escape)
        encode_size = bin_escape_encode(src, src_len, buffer, dest.capacity(), fill_null);
    else
        encode_size = bin_escape_encode_twice(src, src_len, buffer, dest.capacity(), fill_null);
    if (!add_quote) {
        if (encode_size >= 0)
            dest.resize(encode_size);
        else
            dest.clear();
    }
    else {
        if (encode_size >= 0) {
            dest += '\"';
            encode_size += 2;
            dest.resize(encode_size);
        }
        else
            dest = "\"\"";
    }
    return encode_size;
}

static std::string bin_escape_encode(const char * src, std::size_t src_len,
    bool fill_null = true, bool is_twice_escape = false, bool add_quote = false) {
    std::string dest;
    std::size_t alloc_size = bin_escape_get_encode_capacity(src_len, is_twice_escape, add_quote);
    dest.resize(alloc_size);
    char * buffer = &dest[0];
    if (add_quote)
        *buffer++ = '\"';
    std::streamsize encode_size;
    if (!is_twice_escape)
        encode_size = bin_escape_encode(src, src_len, buffer, dest.capacity(), fill_null);
    else
        encode_size = bin_escape_encode_twice(src, src_len, buffer, dest.capacity(), fill_null);
    if (!add_quote) {
        if (encode_size >= 0)
            dest.resize(encode_size);
        else
            dest.clear();
    }
    else {
        if (encode_size >= 0) {
            dest += '\"';
            encode_size += 2;
            dest.resize(encode_size);
        }
        else
            dest = "\"\"";
    }
    return dest;
}

static std::size_t bin_escape_decode(const char * data, std::size_t data_len,
    char * dest, std::size_t max_size, bool fill_null = true, bool skip_quote = false) {
    unsigned char * src = reinterpret_cast<unsigned char *>(const_cast<char *>(data));
    unsigned char * src_end = src + data_len;
    char * dest_start = dest;
    char * dest_max = dest + max_size;
    if (fill_null)
        dest_max -= 1;
    // The dest json decode buffer size must be less than src json length.
    assert(max_size >= data_len);
    // dest can not overflow dest_max forever.
    while (src < src_end) {
        unsigned char c = *src;
        if (c != '\\') {
            *dest++ = c;
            src++;
        }
        else {
            // if (c == '\\')
            src++;
            unsigned char e = *src;
            unsigned char unescape = unescape_table_256[e];
            if (unescape != 1) {
                // It's a valid unescape char.
                *dest++ = unescape;
                src++;
            }
            else {
                // Error: Parse string escape invalid.
            }
        }
    }
    assert(dest != nullptr);
    if (fill_null)
        *dest = '\0';
    assert(dest >= dest_start);
    return (dest - dest_start);
}

static std::size_t bin_escape_decode_twice(const char * data, std::size_t data_len,
    char * dest, std::size_t max_size, bool fill_null = true, bool skip_quote = false) {
    unsigned char * src = reinterpret_cast<unsigned char *>(const_cast<char *>(data));
    unsigned char * src_end = src + data_len;
    char * dest_start = dest;
    char * dest_max = dest + max_size;
    if (fill_null)
        dest_max -= 1;
    // The dest json decode buffer size must be less than src json length.
    assert(max_size >= data_len);
    // dest can not overflow dest_max forever.
    if (skip_quote && (*src == '\"'))
        src++;
    while (src < src_end) {
        unsigned char c = *src;
        unsigned char e, unescape;
        if (c != '\\') {
            *dest++ = c;
            src++;
        }
        else if (skip_quote && (c == '\"')) {
            src++;
            break;
        }
        else {
            src++;
            c = *src;
            if (c == '\\') {
                src++;
                e = *src;
                if (e != '\\') {
                    // "\\x" -> "\x", "\n", "\r"
                    unescape = unescape_table_256[e];
                    if (unescape != 1) {
                        // It's a valid unescape char.
                        *dest++ = unescape;
                    }
                    else {
                        // Error: Parse string escape invalid.
                    }
                    src++;
                }
                else {
                    // "\\\x" -> "\/", "\"", "\\"
                    src++;
                    e = *src;
                    unescape = unescape_table_256[e];
                    if (unescape != 1) {
                        // It's a valid unescape char.
                        *dest++ = unescape;
                    }
                    else {
                        // Error: Parse string escape invalid.
                    }
                    src++;
                }
            }
            else {
                // Error: Parse string escape invalid. --> "\x"
            }
        }
    }
    assert(dest != nullptr);
    if (fill_null)
        *dest = '\0';
    assert(dest >= dest_start);
    return (dest - dest_start);
}

static std::streamsize bin_escape_decode(const std::string & src, std::string & dest,
    bool fill_null = true, bool is_twice_escape = false, bool skip_quote = false) {
    std::size_t alloc_size = bin_escape_get_decode_capacity(src.length());
    dest.resize(alloc_size);
    char * buffer = &dest[0];
    std::streamsize decode_size;
    if (!is_twice_escape)
        decode_size = bin_escape_decode(src.c_str(), src.length(), buffer, dest.capacity(), fill_null, skip_quote);
    else
        decode_size = bin_escape_decode_twice(src.c_str(), src.length(), buffer, dest.capacity(), fill_null, skip_quote);
    if (decode_size >= 0)
        dest.resize(decode_size);
    else
        dest.clear();
    return decode_size;
}

static std::string bin_escape_decode(const std::string & src, bool fill_null = true,
    bool is_twice_escape = false, bool skip_quote = false) {
    std::string dest;
    std::size_t alloc_size = bin_escape_get_decode_capacity(src.length());
    dest.resize(alloc_size);
    char * buffer = &dest[0];
    std::streamsize decode_size;
    if (!is_twice_escape)
        decode_size = bin_escape_decode(src.c_str(), src.length(), buffer, dest.capacity(), fill_null, skip_quote);
    else
        decode_size = bin_escape_decode_twice(src.c_str(), src.length(), buffer, dest.capacity(), fill_null, skip_quote);
    if (decode_size >= 0)
        dest.resize(decode_size);
    else
        dest.clear();
    return dest;
}
