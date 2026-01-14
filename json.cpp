// -*- mode:c++;indent-tabs-mode:nil;c-basic-offset:4;coding:utf-8 -*-
// vi: set et ft=cpp ts=4 sts=4 sw=4 fenc=utf-8 :vi
//
// Copyright 2024 Mozilla Foundation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "json.h"
#include "jtckdint.h"

#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdlib>

#include "double-conversion/double-to-string.h"
#include "double-conversion/string-to-double.h"

#define KEY 1
#define COMMA 2
#define COLON 4
#define ARRAY 8
#define OBJECT 16
#define DEPTH 20

#define ASCII 0
#define C0 1
#define DQUOTE 2
#define BACKSLASH 3
#define UTF8_2 4
#define UTF8_3 5
#define UTF8_4 6
#define C1 7
#define UTF8_3_E0 8
#define UTF8_3_ED 9
#define UTF8_4_F0 10
#define BADUTF8 11
#define EVILUTF8 12

#define UTF16_MASK 0xfc00
#define UTF16_MOAR 0xd800 // 0xD800..0xDBFF
#define UTF16_CONT 0xdc00 // 0xDC00..0xDFFF

#define READ32LE(S) \
    ((uint_least32_t)(255 & (S)[3]) << 030 | \
     (uint_least32_t)(255 & (S)[2]) << 020 | \
     (uint_least32_t)(255 & (S)[1]) << 010 | \
     (uint_least32_t)(255 & (S)[0]) << 000)

#define ThomPikeCont(x) (0200 == (0300 & (x)))
#define ThomPikeByte(x) ((x) & (((1 << ThomPikeMsb(x)) - 1) | 3))
#define ThomPikeLen(x) (7 - ThomPikeMsb(x))
#define ThomPikeMsb(x) ((255 & (x)) < 252 ? Bsr(255 & ~(x)) : 1)
#define ThomPikeMerge(x, y) ((x) << 6 | (077 & (y)))

#define IsSurrogate(wc) ((0xf800 & (wc)) == 0xd800)
#define IsHighSurrogate(wc) (((wc) & UTF16_MASK) == UTF16_MOAR)
#define IsLowSurrogate(wc) (((wc) & UTF16_MASK) == UTF16_CONT)
#define MergeUtf16(hi, lo) ((((hi) - 0xD800) << 10) + ((lo) - 0xDC00) + 0x10000)
#define EncodeUtf16(wc) \
    ((0x0000 <= (wc) && (wc) <= 0xFFFF) || (0xE000 <= (wc) && (wc) <= 0xFFFF) \
       ? (wc) \
     : 0x10000 <= (wc) && (wc) <= 0x10FFFF \
       ? (((((wc) - 0x10000) >> 10) + 0xD800) | \
          (unsigned)((((wc) - 0x10000) & 1023) + 0xDC00) << 16) \
       : 0xFFFD)

namespace jt {

static const char kJsonStr[256] = {
    1,  1,  1,  1,  1,  1,  1,  1, // 0000 ascii (0)
    1,  1,  1,  1,  1,  1,  1,  1, // 0010
    1,  1,  1,  1,  1,  1,  1,  1, // 0020 c0 (1)
    1,  1,  1,  1,  1,  1,  1,  1, // 0030
    0,  0,  2,  0,  0,  0,  0,  0, // 0040 dquote (2)
    0,  0,  0,  0,  0,  0,  0,  0, // 0050
    0,  0,  0,  0,  0,  0,  0,  0, // 0060
    0,  0,  0,  0,  0,  0,  0,  0, // 0070
    0,  0,  0,  0,  0,  0,  0,  0, // 0100
    0,  0,  0,  0,  0,  0,  0,  0, // 0110
    0,  0,  0,  0,  0,  0,  0,  0, // 0120
    0,  0,  0,  0,  3,  0,  0,  0, // 0130 backslash (3)
    0,  0,  0,  0,  0,  0,  0,  0, // 0140
    0,  0,  0,  0,  0,  0,  0,  0, // 0150
    0,  0,  0,  0,  0,  0,  0,  0, // 0160
    0,  0,  0,  0,  0,  0,  0,  0, // 0170
    7,  7,  7,  7,  7,  7,  7,  7, // 0200 c1 (8)
    7,  7,  7,  7,  7,  7,  7,  7, // 0210
    7,  7,  7,  7,  7,  7,  7,  7, // 0220
    7,  7,  7,  7,  7,  7,  7,  7, // 0230
    11, 11, 11, 11, 11, 11, 11, 11, // 0240 latin1 (4)
    11, 11, 11, 11, 11, 11, 11, 11, // 0250
    11, 11, 11, 11, 11, 11, 11, 11, // 0260
    11, 11, 11, 11, 11, 11, 11, 11, // 0270
    12, 12, 4,  4,  4,  4,  4,  4, // 0300 utf8-2 (5)
    4,  4,  4,  4,  4,  4,  4,  4, // 0310
    4,  4,  4,  4,  4,  4,  4,  4, // 0320 utf8-2
    4,  4,  4,  4,  4,  4,  4,  4, // 0330
    8,  5,  5,  5,  5,  5,  5,  5, // 0340 utf8-3 (6)
    5,  5,  5,  5,  5,  9,  5,  5, // 0350
    10, 6,  6,  6,  6,  11, 11, 11, // 0360 utf8-4 (7)
    11, 11, 11, 11, 11, 11, 11, 11, // 0370
};

static const char kEscapeLiteral[128] = {
    9, 9, 9, 9, 9, 9, 9, 9, 9, 1, 2, 9, 4, 3, 9, 9, // 0x00
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, // 0x10
    0, 0, 7, 0, 0, 0, 9, 9, 0, 0, 0, 0, 0, 0, 0, 6, // 0x20
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 9, 9, 0, // 0x30
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0x40
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, // 0x50
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0x60
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, // 0x70
};

alignas(signed char) static const signed char kHexToInt[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0x00
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0x10
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0x20
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  -1, -1, -1, -1, -1, -1, // 0x30
    -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0x40
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0x50
    -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0x60
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0x70
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0x80
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0x90
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0xa0
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0xb0
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0xc0
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0xd0
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0xe0
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0xf0
};

static const double_conversion::DoubleToStringConverter kDoubleToJson(
  double_conversion::DoubleToStringConverter::UNIQUE_ZERO |
    double_conversion::DoubleToStringConverter::EMIT_POSITIVE_EXPONENT_SIGN,
  "1e5000",
  "null",
  'e',
  -6,
  21,
  6,
  0);

static const double_conversion::StringToDoubleConverter kJsonToDouble(
  double_conversion::StringToDoubleConverter::ALLOW_CASE_INSENSITIVITY |
    double_conversion::StringToDoubleConverter::ALLOW_LEADING_SPACES |
    double_conversion::StringToDoubleConverter::ALLOW_TRAILING_JUNK |
    double_conversion::StringToDoubleConverter::ALLOW_TRAILING_SPACES,
  0.0,
  1.0,
  "Infinity",
  "NaN");

#if defined(__GNUC__) || defined(__clang__)
#define Bsr(x) (__builtin_clz(x) ^ (sizeof(int) * CHAR_BIT - 1))
#else
static int
Bsr(int x)
{
    int r = 0;
    if (x & 0xFFFF0000u) {
        x >>= 16;
        r |= 16;
    }
    if (x & 0xFF00) {
        x >>= 8;
        r |= 8;
    }
    if (x & 0xF0) {
        x >>= 4;
        r |= 4;
    }
    if (x & 0xC) {
        x >>= 2;
        r |= 2;
    }
    if (x & 0x2) {
        r |= 1;
    }
    return r;
}
#endif

static double
StringToDouble(const char* s, size_t n, int* out_processed)
{
    if (n == (size_t)-1)
        n = strlen(s);
    int processed;
    double res = kJsonToDouble.StringToDouble(s, n, &processed);
    if (out_processed)
        *out_processed = processed;
    return res;
}

static char*
UlongToString(char* p, unsigned long long x)
{
    char t;
    size_t i, a, b;
    i = 0;
    do {
        p[i++] = x % 10 + '0';
        x = x / 10;
    } while (x > 0);
    p[i] = '\0';
    if (i) {
        for (a = 0, b = i - 1; a < b; ++a, --b) {
            t = p[a];
            p[a] = p[b];
            p[b] = t;
        }
    }
    return p + i;
}

static char*
LongToString(char* p, long long x)
{
    if (x < 0)
        *p++ = '-', x = 0 - (unsigned long long)x;
    return UlongToString(p, x);
}

Json::Json(unsigned long value)
{
    if (value <= LLONG_MAX) {
        type_ = Long;
        long_value = value;
    } else {
        type_ = Double;
        double_value = value;
    }
}

Json::Json(unsigned long long value)
{
    if (value <= LLONG_MAX) {
        type_ = Long;
        long_value = value;
    } else {
        type_ = Double;
        double_value = value;
    }
}

Json::Json(const char* value)
{
    if (value) {
        type_ = String;
        new (&string_value) std::string(value);
    } else {
        type_ = Null;
    }
}

Json::Json(const std::string& value) : type_(String), string_value(value)
{
}

Json::~Json()
{
    if (type_ >= String)
        clear();
}

void
Json::clear()
{
    switch (type_) {
        case String:
            string_value.~basic_string();
            break;
        case Array:
            array_value.~vector();
            break;
        case Object:
            object_data.ordered = false;
            object_data.object_value.~map();
            object_data.object_order.~vector();
            break;
        default:
            break;
    }
    type_ = Null;
}

Json::Json(const Json& other) : type_(other.type_)
{
    switch (type_) {
        case Null:
            break;
        case Bool:
            bool_value = other.bool_value;
            break;
        case Long:
            long_value = other.long_value;
            break;
        case Float:
            float_value = other.float_value;
            break;
        case Double:
            double_value = other.double_value;
            break;
        case String:
            new (&string_value) std::string(other.string_value);
            break;
        case Array:
            new (&array_value) std::vector<Json>(other.array_value);
            break;
        case Object:
            object_data.ordered = other.object_data.ordered;
            new (&object_data.object_value) std::map<std::string, Json>(other.object_data.object_value);
            new (&object_data.object_order) std::vector<std::string>(other.object_data.object_order);
            break;
        default:
            ON_LOGIC_ERROR("Unhandled JSON type.");
    }
}

Json&
Json::operator=(const Json& _other)
{
    if (this != &_other) {
        Json other(_other);
        if (type_ >= String)
            clear();
        type_ = other.type_;
        switch (type_) {
            case Null:
                break;
            case Bool:
                bool_value = other.bool_value;
                break;
            case Long:
                long_value = other.long_value;
                break;
            case Float:
                float_value = other.float_value;
                break;
            case Double:
                double_value = other.double_value;
                break;
            case String:
                new (&string_value) std::string(other.string_value);
                break;
            case Array:
                new (&array_value) std::vector<Json>(other.array_value);
                break;
            case Object:
                object_data.ordered = other.object_data.ordered;
                new (&object_data.object_value)
                  std::map<std::string, Json>(other.object_data.object_value);
                new (&object_data.object_order) std::vector<std::string>(other.object_data.object_order);
                break;
            default:
                ON_LOGIC_ERROR("Unhandled JSON type.");
        }
    }
    return *this;
}

Json::Json(Json&& other) : type_(other.type_)
{
    switch (type_) {
        case Null:
            break;
        case Bool:
            bool_value = other.bool_value;
            break;
        case Long:
            long_value = other.long_value;
            break;
        case Float:
            float_value = other.float_value;
            break;
        case Double:
            double_value = other.double_value;
            break;
        case String:
            new (&string_value) std::string(std::move(other.string_value));
            break;
        case Array:
            new (&array_value) std::vector<Json>(std::move(other.array_value));
            break;
        case Object:
            object_data.ordered = other.object_data.ordered;
            new (&object_data.object_value)
              std::map<std::string, Json>(std::move(other.object_data.object_value));
            new (&object_data.object_order) std::vector<std::string>(std::move(other.object_data.object_order));
            break;
        default:
            ON_LOGIC_ERROR("Unhandled JSON type.");
    }
    other.type_ = Null;
}

Json&
Json::operator=(Json&& _other)
{
    if (this != &_other) {
        Json other(_other);
        if (type_ >= String)
            clear();
        type_ = other.type_;
        switch (type_) {
            case Null:
                break;
            case Bool:
                bool_value = other.bool_value;
                break;
            case Long:
                long_value = other.long_value;
                break;
            case Float:
                float_value = other.float_value;
                break;
            case Double:
                double_value = other.double_value;
                break;
            case String:
                new (&string_value) std::string(std::move(other.string_value));
                break;
            case Array:
                new (&array_value)
                  std::vector<Json>(std::move(other.array_value));
                break;
            case Object:
                object_data.ordered = other.object_data.ordered;
                new (&object_data.object_value)
                  std::map<std::string, Json>(std::move(other.object_data.object_value));
                new (&object_data.object_order) std::vector<std::string>(std::move(other.object_data.object_order));
                break;
            default:
                ON_LOGIC_ERROR("Unhandled JSON type.");;
        }
        other.type_ = Null;
    }
    return *this;
}

double
Json::getNumber() const
{
    switch (type_) {
        case Long:
            return long_value;
        case Float:
            return float_value;
        case Double:
            return double_value;
        default:
            ON_LOGIC_ERROR("JSON value is not a number.");
    }
}

long long
Json::getLong() const
{
    switch (type_) {
        case Long:
            return long_value;
        default:
            ON_LOGIC_ERROR("JSON value is not a long.");
    }
}

bool
Json::getBool() const
{
    switch (type_) {
        case Bool:
            return bool_value;
        default:
            ON_LOGIC_ERROR("JSON value is not a bool.");
    }
}

float
Json::getFloat() const
{
    switch (type_) {
        case Float:
            return float_value;
        case Double:
            return double_value;
        default:
            ON_LOGIC_ERROR("JSON value is not a floating-point number.");
    }
}

double
Json::getDouble() const
{
    switch (type_) {
        case Float:
            return float_value;
        case Double:
            return double_value;
        default:
            ON_LOGIC_ERROR("JSON value is not a floating-point number.");
    }
}

const std::string&
Json::getString() const
{
    switch (type_) {
        case String:
            return string_value;
        default:
            ON_LOGIC_ERROR("JSON value is not a string.");
    }
}

std::string&
Json::getString()
{
    switch (type_) {
        case String:
            return string_value;
        default:
            ON_LOGIC_ERROR("JSON value is not a string.");
    }
}

std::vector<Json>&
Json::getArray()
{
    switch (type_) {
        case Array:
            return array_value;
        default:
            ON_LOGIC_ERROR("JSON value is not an array.");
    }
}

std::map<std::string, Json>&
Json::getObject()
{
    switch (type_) {
        case Object:
            return object_data.object_value;
        default:
            ON_LOGIC_ERROR("JSON value is not an object.");
    }
}

std::vector<std::string>&
Json::getObjectOrder()
{
    switch (type_) {
        case Object:
            return object_data.object_order;
        default:
            ON_LOGIC_ERROR("JSON value is not an object.");
    }
}

void
Json::setArray()
{
    if (type_ >= String)
        clear();
    type_ = Array;
    new (&array_value) std::vector<Json>();
}

void
Json::setObject(bool ordered)
{
    if (type_ >= String)
        clear();
    type_ = Object;
    object_data.ordered = ordered;
    new (&object_data.object_value) std::map<std::string, Json>();
    new (&object_data.object_order) std::vector<std::string>();
}

bool
Json::contains(const std::string& key) const
{
    if (!is_object())
        return false;
    return object_data.object_value.find(key) != object_data.object_value.end();
}

Json&
Json::operator[](size_t index)
{
    if (!is_array())
        setArray();
    if (index >= array_value.size()) {
        array_value.resize(index + 1);
    }
    return array_value[index];
}

Json&
Json::operator[](const std::string& key)
{
    if (!is_object())
        setObject();

    if (object_data.ordered)
    {
        auto it = object_data.object_value.find(key);
        if (it == object_data.object_value.end())
            object_data.object_order.push_back(key);
    }

    return object_data.object_value[key];
}

const Json&
Json::operator[](const std::string& key) const
{
    if (!is_object())
        ON_LOGIC_ERROR("cannot use operator[] with a string argument with non-object type");

    auto it = object_data.object_value.find(key);
    if (it != object_data.object_value.end())
        return it->second;
    else
        ON_LOGIC_ERROR("key not found in const object");
}

std::string
Json::toString(bool preserve_object_order) const
{
    if (type_ == String)
        return string_value;

    std::string b;
    marshal(b, false, 0, 0, preserve_object_order);
    return b;
}

std::string
Json::toStringPretty(bool preserve_object_order) const
{
    if (type_ == String)
        return string_value;

    std::string b;
    marshal(b, true, 0, 2, preserve_object_order);
    return b;
}

std::string
Json::dump(int indent, bool preserve_object_order) const
{
    std::string b;

    if (indent < 0)
        marshal(b, false, 0, 0, preserve_object_order);
    else
        marshal(b, true, 0, indent, preserve_object_order);

    return b;
}

void
Json::marshal(std::string& b, bool pretty, int current_indent, int indent_step, bool preserve_object_order) const
{
    switch (type_) {
        case Null:
            b += "null";
            break;
        case String:
            stringify(b, string_value);
            break;
        case Bool:
            b += bool_value ? "true" : "false";
            break;
        case Long: {
            char buf[64];
            b.append(buf, LongToString(buf, long_value) - buf);
            break;
        }
        case Float: {
            char buf[128];
            double_conversion::StringBuilder db(buf, 128);
            kDoubleToJson.ToShortestSingle(float_value, &db);
            db.Finalize();
            b += buf;
            break;
        }
        case Double: {
            char buf[128];
            double_conversion::StringBuilder db(buf, 128);
            kDoubleToJson.ToShortest(double_value, &db);
            db.Finalize();
            b += buf;
            break;
        }
        case Array: {
            bool once = false;
            b += '[';
            for (auto i = array_value.begin(); i != array_value.end(); ++i) {
                if (once) {
                    b += ',';
                } else {
                    once = true;
                }
                if (pretty) {
                    b += '\n';
                    for (int j = 0; j < current_indent + indent_step; ++j)
                        b += ' ';
                }
                i->marshal(b, pretty, current_indent + indent_step, indent_step, preserve_object_order);
            }
            if (pretty) {
                b += '\n';
                for (int j = 0; j < current_indent; ++j)
                    b += ' ';
            }
            b += ']';
            break;
        }
        case Object: {
            bool once = false;
            b += '{';
            if (preserve_object_order && object_data.ordered && object_data.object_order.size() == object_data.object_value.size())
            {
                for (auto it = object_data.object_order.begin(); it != object_data.object_order.end(); ++it) {
                    auto val = object_data.object_value.find(*it);
                    if (once) {
                        b += ',';
                    } else {
                        once = true;
                    }
                    if (pretty) {
                        b += '\n';
                        for (int j = 0; j < current_indent + indent_step; ++j)
                            b += ' ';
                    }
                    stringify(b, val->first);
                    b += ':';
                    if (pretty)
                        b += ' ';
                    val->second.marshal(b, pretty, current_indent + indent_step, indent_step, preserve_object_order);
                }
            } else {
                for (auto it = object_data.object_value.begin(); it != object_data.object_value.end(); ++it) {
                    if (once) {
                        b += ',';
                    } else {
                        once = true;
                    }
                    if (pretty) {
                        b += '\n';
                        for (int j = 0; j < current_indent + indent_step; ++j)
                            b += ' ';
                    }
                    stringify(b, it->first);
                    b += ':';
                    if (pretty)
                        b += ' ';
                    it->second.marshal(b, pretty, current_indent + indent_step, indent_step, preserve_object_order);
                }
            }
            if (pretty) {
                b += '\n';
                for (int j = 0; j < current_indent; ++j)
                    b += ' ';
            }
            b += '}';
            break;
        }
        default:
            ON_LOGIC_ERROR("Unhandled JSON type.");
    }
}

void
Json::stringify(std::string& b, const std::string& s)
{
    b += '"';
    serialize(b, s);
    b += '"';
}

void
Json::serialize(std::string& sb, const std::string& s)
{
    size_t i, j, m;
    wint_t x, a, b;
    unsigned long long w;
    for (i = 0; i < s.size();) {
        x = s[i++] & 255;
        if (x >= 0300) {
            a = ThomPikeByte(x);
            m = ThomPikeLen(x) - 1;
            if (i + m <= s.size()) {
                for (j = 0;;) {
                    b = s[i + j] & 0xff;
                    if (!ThomPikeCont(b))
                        break;
                    a = ThomPikeMerge(a, b);
                    if (++j == m) {
                        x = a;
                        i += j;
                        break;
                    }
                }
            }
        }
        switch (0 <= x && x <= 127 ? kEscapeLiteral[x] : 9) {
            case 0:
                sb += x;
                break;
            case 1:
                sb += "\\t";
                break;
            case 2:
                sb += "\\n";
                break;
            case 3:
                sb += "\\r";
                break;
            case 4:
                sb += "\\f";
                break;
            case 5:
                sb += "\\\\";
                break;
            case 6:
                sb += '/';
                break;
            case 7:
                sb += "\\\"";
                break;
            case 9:
                w = EncodeUtf16(x);
                do {
                    char esc[6];
                    esc[0] = '\\';
                    esc[1] = 'u';
                    esc[2] = "0123456789abcdef"[(w & 0xF000) >> 014];
                    esc[3] = "0123456789abcdef"[(w & 0x0F00) >> 010];
                    esc[4] = "0123456789abcdef"[(w & 0x00F0) >> 004];
                    esc[5] = "0123456789abcdef"[(w & 0x000F) >> 000];
                    sb.append(esc, 6);
                } while ((w >>= 16));
                break;
            default:
                ON_LOGIC_ERROR("Unhandled character escape code during string serialization.");
        }
    }
}

Json::Status
Json::parse(Json& json, const char*& p, const char* e, int context, int depth, bool store_object_order)
{
    char w[4];
    long long x;
    const char* a;
    int A, B, C, D, c, d, i, u;
    if (!depth)
        return depth_exceeded;
    for (a = p, d = +1; p < e;) {
        switch ((c = *p++ & 255)) {
            case ' ': // spaces
            case '\n':
            case '\r':
            case '\t':
                a = p;
                break;

            case ',': // present in list and object
                if (context & COMMA) {
                    context = 0;
                    a = p;
                    break;
                } else {
                    return unexpected_comma;
                }

            case ':': // present only in object after key
                if (context & COLON) {
                    context = 0;
                    a = p;
                    break;
                } else {
                    return unexpected_colon;
                }

            case 'n': // null
                if (context & (KEY | COLON | COMMA))
                    goto OnColonCommaKey;
                if (p + 3 <= e && READ32LE(p - 1) == READ32LE("null")) {
                    p += 3;
                    return success;
                } else {
                    return illegal_character;
                }

            case 'f': // false
                if (context & (KEY | COLON | COMMA))
                    goto OnColonCommaKey;
                if (p + 4 <= e && READ32LE(p) == READ32LE("alse")) {
                    json.type_ = Bool;
                    json.bool_value = false;
                    p += 4;
                    return success;
                } else {
                    return illegal_character;
                }

            case 't': // true
                if (context & (KEY | COLON | COMMA))
                    goto OnColonCommaKey;
                if (p + 3 <= e && READ32LE(p - 1) == READ32LE("true")) {
                    json.type_ = Bool;
                    json.bool_value = true;
                    p += 3;
                    return success;
                } else {
                    return illegal_character;
                }

            default:
                return illegal_character;

            OnColonCommaKey:
                if (context & KEY)
                    return object_key_must_be_string;
            OnColonComma:
                if (context & COLON)
                    return missing_colon;
                return missing_comma;

            case '-': // negative
                if (context & (COLON | COMMA | KEY))
                    goto OnColonCommaKey;
                if (p < e && isdigit(*p)) {
                    d = -1;
                    break;
                } else {
                    return bad_negative;
                }

            case '0': // zero or number
                if (context & (COLON | COMMA | KEY))
                    goto OnColonCommaKey;
                if (p < e) {
                    if (*p == '.') {
                        if (p + 1 == e || !isdigit(p[1]))
                            return bad_double;
                        goto UseDubble;
                    } else if (*p == 'e' || *p == 'E') {
                        goto UseDubble;
                    } else if (isdigit(*p)) {
                        return unexpected_octal;
                    }
                }
                json.type_ = Long;
                json.long_value = 0;
                return success;

            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9': // integer
                if (context & (COLON | COMMA | KEY))
                    goto OnColonCommaKey;
                for (x = (c - '0') * d; p < e; ++p) {
                    c = *p & 255;
                    if (isdigit(c)) {
                        if (ckd_mul(&x, x, 10) ||
                            ckd_add(&x, x, (c - '0') * d)) {
                            goto UseDubble;
                        }
                    } else if (c == '.') {
                        if (p + 1 == e || !isdigit(p[1]))
                            return bad_double;
                        goto UseDubble;
                    } else if (c == 'e' || c == 'E') {
                        goto UseDubble;
                    } else {
                        break;
                    }
                }
                json.type_ = Long;
                json.long_value = x;
                return success;

            UseDubble: // number
                json.type_ = Double;
                json.double_value = StringToDouble(a, e - a, &c);
                if (c <= 0)
                    return bad_double;
                if (a + c < e && (a[c] == 'e' || a[c] == 'E'))
                    return bad_exponent;
                p = a + c;
                return success;

            case '[': { // Array
                if (context & (COLON | COMMA | KEY))
                    goto OnColonCommaKey;
                json.setArray();
                Json value;
                for (context = ARRAY, i = 0;;) {
                    Status status = parse(value, p, e, context, depth - 1, store_object_order);
                    if (status == absent_value)
                        return success;
                    if (status != success)
                        return status;
                    json.array_value.emplace_back(std::move(value));
                    context = ARRAY | COMMA;
                }
            }

            case ']':
                if (context & ARRAY)
                    return absent_value;
                return unexpected_end_of_array;

            case '}':
                if (context & OBJECT)
                    return absent_value;
                return unexpected_end_of_object;

            case '{': { // Object
                if (context & (COLON | COMMA | KEY))
                    goto OnColonCommaKey;
                json.setObject(store_object_order);
                context = KEY | OBJECT;
                Json key, value;
                for (;;) {
                    Status status = parse(key, p, e, context, depth - 1, store_object_order);
                    if (status == absent_value)
                        return success;
                    if (status != success)
                        return status;
                    if (!key.is_string())
                        return object_key_must_be_string;
                    status = parse(value, p, e, COLON, depth - 1, store_object_order);
                    if (status == absent_value)
                        return object_missing_value;
                    if (status != success)
                        return status;
                    if (store_object_order)
                        json.object_data.object_order.push_back(key.getString());
                    json.object_data.object_value.emplace(std::move(key.string_value),
                                              std::move(value));
                    context = KEY | COMMA | OBJECT;
                    key.clear();
                }
            }

            case '"': { // string
                std::string b;
                if (context & (COLON | COMMA))
                    goto OnColonComma;
                for (;;) {
                    if (p >= e)
                        return unexpected_end_of_string;
                    switch (kJsonStr[(c = *p++ & 255)]) {

                        case ASCII:
                            b += c;
                            break;

                        case DQUOTE:
                            json.type_ = String;
                            new (&json.string_value) std::string(std::move(b));
                            return success;

                        case BACKSLASH:
                            if (p >= e)
                                return unexpected_end_of_string;
                            switch ((c = *p++ & 255)) {
                                case '"':
                                case '/':
                                case '\\':
                                    b += c;
                                    break;
                                case 'b':
                                    b += '\b';
                                    break;
                                case 'f':
                                    b += '\f';
                                    break;
                                case 'n':
                                    b += '\n';
                                    break;
                                case 'r':
                                    b += '\r';
                                    break;
                                case 't':
                                    b += '\t';
                                    break;
                                case 'x':
                                    if (p + 2 <= e && //
                                        (A = kHexToInt[p[0] & 255]) !=
                                          -1 && // HEX
                                        (B = kHexToInt[p[1] & 255]) != -1) { //
                                        c = A << 4 | B;
                                        if (!(0x20 <= c && c <= 0x7E))
                                            return hex_escape_not_printable;
                                        p += 2;
                                        b += c;
                                        break;
                                    } else {
                                        return invalid_hex_escape;
                                    }
                                case 'u':
                                    if (p + 4 <= e && //
                                        (A = kHexToInt[p[0] & 255]) != -1 && //
                                        (B = kHexToInt[p[1] & 255]) !=
                                          -1 && // UCS-2
                                        (C = kHexToInt[p[2] & 255]) != -1 && //
                                        (D = kHexToInt[p[3] & 255]) != -1) { //
                                        c = A << 12 | B << 8 | C << 4 | D;
                                        if (!IsSurrogate(c)) {
                                            p += 4;
                                        } else if (IsHighSurrogate(c)) {
                                            if (p + 4 + 6 <= e && //
                                                p[4] == '\\' && //
                                                p[5] == 'u' && //
                                                (A = kHexToInt[p[6] & 255]) !=
                                                  -1 && // UTF-16
                                                (B = kHexToInt[p[7] & 255]) !=
                                                  -1 && //
                                                (C = kHexToInt[p[8] & 255]) !=
                                                  -1 && //
                                                (D = kHexToInt[p[9] & 255]) !=
                                                  -1) { //
                                                u =
                                                  A << 12 | B << 8 | C << 4 | D;
                                                if (IsLowSurrogate(u)) {
                                                    p += 4 + 6;
                                                    c = MergeUtf16(c, u);
                                                } else {
                                                    goto BadUnicode;
                                                }
                                            } else {
                                                goto BadUnicode;
                                            }
                                        } else {
                                            goto BadUnicode;
                                        }
                                        // UTF-8
                                    EncodeUtf8:
                                        if (c <= 0x7f) {
                                            w[0] = c;
                                            i = 1;
                                        } else if (c <= 0x7ff) {
                                            w[0] = 0300 | (c >> 6);
                                            w[1] = 0200 | (c & 077);
                                            i = 2;
                                        } else if (c <= 0xffff) {
                                            if (IsSurrogate(c)) {
                                            ReplacementCharacter:
                                                c = 0xfffd;
                                            }
                                            w[0] = 0340 | (c >> 12);
                                            w[1] = 0200 | ((c >> 6) & 077);
                                            w[2] = 0200 | (c & 077);
                                            i = 3;
                                        } else if (~(c >> 18) & 007) {
                                            w[0] = 0360 | (c >> 18);
                                            w[1] = 0200 | ((c >> 12) & 077);
                                            w[2] = 0200 | ((c >> 6) & 077);
                                            w[3] = 0200 | (c & 077);
                                            i = 4;
                                        } else {
                                            goto ReplacementCharacter;
                                        }
                                        b.append(w, i);
                                    } else {
                                        return invalid_unicode_escape;
                                    BadUnicode:
                                        // Echo invalid \uXXXX sequences
                                        // Rather than corrupting UTF-8!
                                        b += "\\u";
                                    }
                                    break;
                                default:
                                    return invalid_escape_character;
                            }
                            break;

                        case UTF8_2:
                            if (p < e && //
                                (p[0] & 0300) == 0200) { //
                                c = (c & 037) << 6 | //
                                    (p[0] & 077); //
                                p += 1;
                                goto EncodeUtf8;
                            } else {
                                return malformed_utf8;
                            }

                        case UTF8_3_E0:
                            if (p + 2 <= e && //
                                (p[0] & 0377) < 0240 && //
                                (p[0] & 0300) == 0200 && //
                                (p[1] & 0300) == 0200) {
                                return overlong_utf8_0x7ff;
                            }
                            // fallthrough

                        case UTF8_3:
                        ThreeUtf8:
                            if (p + 2 <= e && //
                                (p[0] & 0300) == 0200 && //
                                (p[1] & 0300) == 0200) { //
                                c = (c & 017) << 12 | //
                                    (p[0] & 077) << 6 | //
                                    (p[1] & 077); //
                                p += 2;
                                goto EncodeUtf8;
                            } else {
                                return malformed_utf8;
                            }

                        case UTF8_3_ED:
                            if (p + 2 <= e && //
                                (p[0] & 0377) >= 0240) { //
                                if (p + 5 <= e && //
                                    (p[0] & 0377) >= 0256 && //
                                    (p[1] & 0300) == 0200 && //
                                    (p[2] & 0377) == 0355 && //
                                    (p[3] & 0377) >= 0260 && //
                                    (p[4] & 0300) == 0200) { //
                                    A = (0355 & 017) << 12 | // CESU-8
                                        (p[0] & 077) << 6 | //
                                        (p[1] & 077); //
                                    B = (0355 & 017) << 12 | //
                                        (p[3] & 077) << 6 | //
                                        (p[4] & 077); //
                                    c = ((A - 0xDB80) << 10) + //
                                        ((B - 0xDC00) + 0x10000); //
                                    goto EncodeUtf8;
                                } else if ((p[0] & 0300) == 0200 && //
                                           (p[1] & 0300) == 0200) { //
                                    return utf16_surrogate_in_utf8;
                                } else {
                                    return malformed_utf8;
                                }
                            }
                            goto ThreeUtf8;

                        case UTF8_4_F0:
                            if (p + 3 <= e && (p[0] & 0377) < 0220 &&
                                (((uint_least32_t)(p[+2] & 0377) << 030 |
                                  (uint_least32_t)(p[+1] & 0377) << 020 |
                                  (uint_least32_t)(p[+0] & 0377) << 010 |
                                  (uint_least32_t)(p[-1] & 0377) << 000) &
                                 0xC0C0C000) == 0x80808000) {
                                return overlong_utf8_0xffff;
                            }
                            // fallthrough
                        case UTF8_4:
                            if (p + 3 <= e && //
                                ((A =
                                    ((uint_least32_t)(p[+2] & 0377) << 030 | //
                                     (uint_least32_t)(p[+1] & 0377) << 020 | //
                                     (uint_least32_t)(p[+0] & 0377) << 010 | //
                                     (uint_least32_t)(p[-1] & 0377)
                                       << 000)) & //
                                 0xC0C0C000) == 0x80808000) { //
                                A = (A & 7) << 18 | //
                                    (A & (077 << 010)) << (12 - 010) | //
                                    (A & (077 << 020)) >> -(6 - 020) | //
                                    (A & (077 << 030)) >> 030; //
                                if (A <= 0x10FFFF) {
                                    c = A;
                                    p += 3;
                                    goto EncodeUtf8;
                                } else {
                                    return utf8_exceeds_utf16_range;
                                }
                            } else {
                                return malformed_utf8;
                            }

                        case EVILUTF8:
                            if (p < e && (p[0] & 0300) == 0200)
                                return overlong_ascii;
                            // fallthrough
                        case BADUTF8:
                            return illegal_utf8_character;
                        case C0:
                            return non_del_c0_control_code_in_string;
                        case C1:
                            return c1_control_code_in_string;
                        default:
                            ON_LOGIC_ERROR("Unhandled character category during string parsing.");
                    }
                }
            }
        }
    }
    if (depth == DEPTH)
        return absent_value;
    return unexpected_eof;
}

Json
Json::parse(const std::string& s, bool store_object_order)
{
    Json::Status s2;
    std::pair<Json::Status, Json> res;
    const char* p = s.data();
    const char* e = s.data() + s.size();
    res.first = parse(res.second, p, e, 0, DEPTH, store_object_order);
    if (res.first == Json::success) {
        Json j2;
        s2 = parse(j2, p, e, 0, DEPTH, store_object_order);
        if (s2 != absent_value)
            res.first = trailing_content;
    }
    return res.second;
}

Json
Json::parse(FILE* file, bool store_object_order)
{
    long current_pos = ftell(file);
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, current_pos, SEEK_SET);

    long bytes_to_read = file_size - current_pos;
    std::string content;
    content.resize(static_cast<size_t>(bytes_to_read));

    size_t bytes_read = fread(&content[0], 1, static_cast<size_t>(bytes_to_read), file);
    content.resize(bytes_read);

    return parse(content, store_object_order);
}

const char*
Json::StatusToString(Json::Status status)
{
    switch (status) {
        case success:
            return "success";
        case bad_double:
            return "bad_double";
        case absent_value:
            return "absent_value";
        case bad_negative:
            return "bad_negative";
        case bad_exponent:
            return "bad_exponent";
        case missing_comma:
            return "missing_comma";
        case missing_colon:
            return "missing_colon";
        case malformed_utf8:
            return "malformed_utf8";
        case depth_exceeded:
            return "depth_exceeded";
        case stack_overflow:
            return "stack_overflow";
        case unexpected_eof:
            return "unexpected_eof";
        case overlong_ascii:
            return "overlong_ascii";
        case unexpected_comma:
            return "unexpected_comma";
        case unexpected_colon:
            return "unexpected_colon";
        case unexpected_octal:
            return "unexpected_octal";
        case trailing_content:
            return "trailing_content";
        case illegal_character:
            return "illegal_character";
        case invalid_hex_escape:
            return "invalid_hex_escape";
        case overlong_utf8_0x7ff:
            return "overlong_utf8_0x7ff";
        case overlong_utf8_0xffff:
            return "overlong_utf8_0xffff";
        case object_missing_value:
            return "object_missing_value";
        case illegal_utf8_character:
            return "illegal_utf8_character";
        case invalid_unicode_escape:
            return "invalid_unicode_escape";
        case utf16_surrogate_in_utf8:
            return "utf16_surrogate_in_utf8";
        case unexpected_end_of_array:
            return "unexpected_end_of_array";
        case hex_escape_not_printable:
            return "hex_escape_not_printable";
        case invalid_escape_character:
            return "invalid_escape_character";
        case utf8_exceeds_utf16_range:
            return "utf8_exceeds_utf16_range";
        case unexpected_end_of_string:
            return "unexpected_end_of_string";
        case unexpected_end_of_object:
            return "unexpected_end_of_object";
        case object_key_must_be_string:
            return "object_key_must_be_string";
        case c1_control_code_in_string:
            return "c1_control_code_in_string";
        case non_del_c0_control_code_in_string:
            return "non_del_c0_control_code_in_string";
        default:
            ON_LOGIC_ERROR("Unhandled Json status value.");
    }
}

bool Json::empty() const
{
    switch (type_)
    {
        case Null:
        {
            return true;
        }

        case Array:
        {
            return array_value.empty();
        }

        case Object:
        {
            return object_data.object_value.empty();
        }

        case String:
        case Bool:
        case Long:
        case Float:
        case Double:
        default:
        {
            return false;
        }
    }
}

Json::iterator Json::begin()
{
    if (is_array()) {
        return iterator(this, array_value.begin());
    } else if (is_object()) {
        return iterator(this, object_data.object_value.begin());
    } else {
        return iterator(this);
    }
}

Json::iterator Json::end()
{
    if (is_array()) {
        return iterator(this, array_value.end());
    } else if (is_object()) {
        return iterator(this, object_data.object_value.end());
    } else {
        return iterator(nullptr);
    }
}

Json::const_iterator Json::begin() const
{
    if (is_array()) {
        return const_iterator(this, array_value.begin());
    } else if (is_object()) {
        return const_iterator(this, object_data.object_value.begin());
    } else {
        return const_iterator(this);
    }
}

Json::const_iterator Json::end() const
{
    if (is_array()) {
        return const_iterator(this, array_value.end());
    } else if (is_object()) {
        return const_iterator(this, object_data.object_value.end());
    } else {
        return const_iterator(nullptr);
    }
}

} // namespace jt
