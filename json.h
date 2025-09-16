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

#pragma once
#include <map>
#include <string>
#include <vector>
#include <cstdio>
#include <iterator>
#include <type_traits>

#if defined(_WIN32) || defined(_WIN64) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__)
#define JTJSON_NOSTDEXCEPT 0
#else
#define JTJSON_NOSTDEXCEPT 1
#endif

#if (defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)) && !JTJSON_NOSTDEXCEPT
#include <stdexcept>
#define ON_LOGIC_ERROR(s) throw std::logic_error(s)
#else
#define ON_LOGIC_ERROR(s) abort()
#endif

namespace jt {

class Json
{
  public:
    class iterator;
    class const_iterator;

  public:
    enum Type
    {
        Null,
        Bool,
        Long,
        Float,
        Double,
        String,
        Array,
        Object
    };

    enum Status
    {
        success,
        bad_double,
        absent_value,
        bad_negative,
        bad_exponent,
        missing_comma,
        missing_colon,
        malformed_utf8,
        depth_exceeded,
        stack_overflow,
        unexpected_eof,
        overlong_ascii,
        unexpected_comma,
        unexpected_colon,
        unexpected_octal,
        trailing_content,
        illegal_character,
        invalid_hex_escape,
        overlong_utf8_0x7ff,
        overlong_utf8_0xffff,
        object_missing_value,
        illegal_utf8_character,
        invalid_unicode_escape,
        utf16_surrogate_in_utf8,
        unexpected_end_of_array,
        hex_escape_not_printable,
        invalid_escape_character,
        utf8_exceeds_utf16_range,
        unexpected_end_of_string,
        unexpected_end_of_object,
        object_key_must_be_string,
        c1_control_code_in_string,
        non_del_c0_control_code_in_string,
    };

  private:
    Type type_;
    union
    {
        bool bool_value;
        float float_value;
        double double_value;
        long long long_value;
        std::string string_value;
        std::vector<Json> array_value;
        struct {
            std::map<std::string, Json> object_value;
            std::vector<std::string> object_order;
            bool ordered = false;
        } object_data;
    };

  public:
    static const char* StatusToString(Status);
    static Json parse(const std::string&, bool store_object_order = false);
    static Json parse(FILE*, bool store_object_order = false);
    bool empty() const;

    Json(const Json&);
    Json(Json&&);
    Json(unsigned long);
    Json(unsigned long long);
    Json(const char*);
    Json(const std::string&);
    ~Json();

    Json(const std::nullptr_t = nullptr) : type_(Null)
    {
    }

    Json(bool value) : type_(Bool), bool_value(value)
    {
    }

    Json(int value) : type_(Long), long_value(value)
    {
    }

    Json(float value) : type_(Float), float_value(value)
    {
    }

    Json(unsigned value) : type_(Long), long_value(value)
    {
    }

    Json(long value) : type_(Long), long_value(value)
    {
    }

    Json(long long value) : type_(Long), long_value(value)
    {
    }

    Json(double value) : type_(Double), double_value(value)
    {
    }

    Json(std::string&& value) : type_(String), string_value(std::move(value))
    {
    }

    Type getType() const
    {
        return type_;
    }

    bool is_null() const
    {
        return type_ == Null;
    }

    bool is_boolean() const
    {
        return type_ == Bool;
    }

    bool is_number() const
    {
        return is_number_float() || is_number_double() || is_number_integer();
    }

    bool is_number_integer() const
    {
        return type_ == Long;
    }

    bool is_number_float() const
    {
        return type_ == Float;
    }

    bool is_number_double() const
    {
        return type_ == Double;
    }

    bool is_string() const
    {
        return type_ == String;
    }

    bool is_array() const
    {
        return type_ == Array;
    }

    bool is_object() const
    {
        return type_ == Object;
    }

    bool getBool() const;
    float getFloat() const;
    double getDouble() const;
    double getNumber() const;
    long long getLong() const;
    const std::string& getString() const;
    std::string& getString();
    std::vector<Json>& getArray();
    std::map<std::string, Json>& getObject();
    std::vector<std::string>& getObjectOrder();

    template<typename T>
    T get() const
    {
        if constexpr (std::is_same_v<T, bool>) {
            return getBool();
        } else if constexpr (std::is_same_v<T, float>) {
            return getFloat();
        } else if constexpr (std::is_same_v<T, double>) {
            return getDouble();
        } else if constexpr (std::is_same_v<T, std::string>) {
            return getString();
        } else if (is_number_integer()) {
            return static_cast<T>(getLong());
        } else {
            ON_LOGIC_ERROR("Unsupported type for Json::get<T>()");
        }
    }

    template<typename ValueType>
    ValueType value(const std::string& key, const ValueType& default_value) const
    {
        // value only works for objects
        if (is_object()) {
            // if key is found, return value and given default value otherwise
            const auto& obj = object_data.object_value;
            auto it = obj.find(key);
            if (it != obj.end()) {
                return it->second.template get<ValueType>();
            }
            return default_value;
        }

        ON_LOGIC_ERROR("cannot use value() with non-object type");
    }

    bool contains(const std::string&) const;

    void setArray();
    void setObject(bool ordered = false);

    template<typename T>
    void push_back(T&& value)
    {
        if (!is_array())
            setArray();
        array_value.emplace_back(std::forward<T>(value));
    }

    template<typename... Args>
    void emplace_back(Args&&... args)
    {
        if (!is_array())
            setArray();
        array_value.emplace_back(std::forward<Args>(args)...);
    }

    std::string toString(bool preserve_object_order = false) const;
    std::string toStringPretty(bool preserve_object_order = false) const;
    std::string dump(int indent = -1, bool preserve_object_order = false) const;

    Json& operator=(const Json&);
    Json& operator=(Json&&);

    Json& operator[](size_t);
    Json& operator[](const std::string&);
    const Json& operator[](const std::string&) const;

    operator std::map<std::string, Json>&()
    {
        if (!is_object())
            setObject();
        return getObject();
    }

    operator std::vector<Json>&()
    {
        if (!is_array())
            setArray();
        return getArray();
    }

    operator std::string() const
    {
        return toString();
    }

    iterator begin();
    iterator end();
    const_iterator begin() const;
    const_iterator end() const;
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

    class iterator
    {
      public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Json;
        using difference_type = std::ptrdiff_t;
        using pointer = Json*;
        using reference = Json&;

      private:
        Json* json_ptr_;
        std::vector<Json>::iterator array_it_;
        std::map<std::string, Json>::iterator object_it_;

        friend class const_iterator;

      public:
        iterator() : json_ptr_(nullptr) {}

        iterator(Json* json) : json_ptr_(json)
        {
            if (json_ptr_ && json_ptr_->is_array()) {
                array_it_ = json_ptr_->array_value.begin();
            } else if (json_ptr_ && json_ptr_->is_object()) {
                object_it_ = json_ptr_->object_data.object_value.begin();
            }
        }

        iterator(Json* json, std::vector<Json>::iterator it) : json_ptr_(json), array_it_(it) {}
        iterator(Json* json, std::map<std::string, Json>::iterator it) : json_ptr_(json), object_it_(it) {}

        reference operator*()
        {
            if (json_ptr_->is_array()) {
                return *array_it_;
            } else if (json_ptr_->is_object()) {
                return object_it_->second;
            } else {
                return *json_ptr_;
            }
        }

        pointer operator->() { return &(operator*()); }

        iterator& operator++()
        {
            if (json_ptr_->is_array()) {
                ++array_it_;
            } else if (json_ptr_->is_object()) {
                ++object_it_;
            }
            return *this;
        }

        iterator operator++(int)
        {
            iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const iterator& other) const
        {
            if (json_ptr_ != other.json_ptr_) return false;
            if (!json_ptr_) return true;

            if (json_ptr_->is_array()) {
                return array_it_ == other.array_it_;
            } else if (json_ptr_->is_object()) {
                return object_it_ == other.object_it_;
            }

            return true;
        }

        bool operator!=(const iterator& other) const
        {
            return !(*this == other);
        }

        const std::string& key() const
        {
            if (json_ptr_ && json_ptr_->is_object())
                return object_it_->first;

            ON_LOGIC_ERROR("cannot use key() with non-object iterator");
        }
    };

    class const_iterator
    {
      public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = const Json;
        using difference_type = std::ptrdiff_t;
        using pointer = const Json*;
        using reference = const Json&;

      private:
        const Json* json_ptr_;
        std::vector<Json>::const_iterator array_it_;
        std::map<std::string, Json>::const_iterator object_it_;

      public:
        const_iterator() : json_ptr_(nullptr) {}

        const_iterator(const Json* json) : json_ptr_(json)
        {
            if (json_ptr_ && json_ptr_->is_array()) {
                array_it_ = json_ptr_->array_value.begin();
            } else if (json_ptr_ && json_ptr_->is_object()) {
                object_it_ = json_ptr_->object_data.object_value.begin();
            }
        }

        const_iterator(const Json* json, std::vector<Json>::const_iterator it) : json_ptr_(json), array_it_(it) {}
        const_iterator(const Json* json, std::map<std::string, Json>::const_iterator it) : json_ptr_(json), object_it_(it) {}

        const_iterator(const iterator& it) : json_ptr_(it.json_ptr_)
        {
            if (json_ptr_ && json_ptr_->is_array()) {
                array_it_ = it.array_it_;
            } else if (json_ptr_ && json_ptr_->is_object()) {
                object_it_ = it.object_it_;
            }
        }

        reference operator*() const
        {
            if (json_ptr_->is_array()) {
                return *array_it_;
            } else if (json_ptr_->is_object()) {
                return object_it_->second;
            } else {
                return *json_ptr_;
            }
        }

        pointer operator->() const { return &(operator*()); }

        const_iterator& operator++()
        {
            if (json_ptr_->is_array()) {
                ++array_it_;
            } else if (json_ptr_->is_object()) {
                ++object_it_;
            }
            return *this;
        }

        const_iterator operator++(int)
        {
            const_iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const const_iterator& other) const
        {
            if (json_ptr_ != other.json_ptr_) return false;
            if (!json_ptr_) return true;

            if (json_ptr_->is_array()) {
                return array_it_ == other.array_it_;
            } else if (json_ptr_->is_object()) {
                return object_it_ == other.object_it_;
            }

            return true;
        }

        bool operator!=(const const_iterator& other) const
        {
            return !(*this == other);
        }

        const std::string& key() const
        {
            if (json_ptr_ && json_ptr_->is_object())
                return object_it_->first;

            ON_LOGIC_ERROR("cannot use key() with non-object iterator");
        }

        friend class iterator;
    };

  private:
    void clear();
    void marshal(std::string&, bool, int, int, bool preserve_object_order = false) const;
    static void stringify(std::string&, const std::string&);
    static void serialize(std::string&, const std::string&);
    static Status parse(Json&, const char*&, const char*, int, int, bool store_object_order = false);
};

} // namespace jt
