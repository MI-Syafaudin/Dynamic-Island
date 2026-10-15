#pragma once

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <utility>

namespace di {

enum class JsonType {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object
};

class JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue>;

class JsonValue {
public:
    using ValueType = std::variant<
        std::monostate, // Null
        bool,           // Boolean
        double,         // Number
        std::string,    // String
        JsonArray,      // Array
        JsonObject      // Object
    >;

    ValueType value;

    JsonValue() : value(std::monostate{}) {}
    explicit JsonValue(bool b) : value(b) {}
    explicit JsonValue(double n) : value(n) {}
    explicit JsonValue(int n) : value(static_cast<double>(n)) {}
    explicit JsonValue(const std::string& s) : value(s) {}
    explicit JsonValue(std::string&& s) : value(std::move(s)) {}
    explicit JsonValue(const char* s) : value(std::string(s ? s : "")) {}
    explicit JsonValue(const JsonArray& a) : value(a) {}
    explicit JsonValue(JsonArray&& a) : value(std::move(a)) {}
    explicit JsonValue(const JsonObject& o) : value(o) {}
    explicit JsonValue(JsonObject&& o) : value(std::move(o)) {}

    bool is_null() const { return std::holds_alternative<std::monostate>(value); }
    bool is_bool() const { return std::holds_alternative<bool>(value); }
    bool is_number() const { return std::holds_alternative<double>(value); }
    bool is_string() const { return std::holds_alternative<std::string>(value); }
    bool is_array() const { return std::holds_alternative<JsonArray>(value); }
    bool is_object() const { return std::holds_alternative<JsonObject>(value); }

    JsonType type() const {
        if (is_null()) return JsonType::Null;
        if (is_bool()) return JsonType::Boolean;
        if (is_number()) return JsonType::Number;
        if (is_string()) return JsonType::String;
        if (is_array()) return JsonType::Array;
        return JsonType::Object;
    }

    bool as_bool(bool default_val = false) const {
        if (auto p = std::get_if<bool>(&value)) return *p;
        return default_val;
    }

    double as_double(double default_val = 0.0) const {
        if (auto p = std::get_if<double>(&value)) return *p;
        return default_val;
    }

    int as_int(int default_val = 0) const {
        if (auto p = std::get_if<double>(&value)) return static_cast<int>(*p);
        return default_val;
    }

    std::string as_string(const std::string& default_val = "") const {
        if (auto p = std::get_if<std::string>(&value)) return *p;
        return default_val;
    }

    const JsonArray& as_array() const {
        static const JsonArray empty_arr;
        if (auto p = std::get_if<JsonArray>(&value)) return *p;
        return empty_arr;
    }

    const JsonObject& as_object() const {
        static const JsonObject empty_obj;
        if (auto p = std::get_if<JsonObject>(&value)) return *p;
        return empty_obj;
    }

    bool has(const std::string& key) const {
        if (auto p = std::get_if<JsonObject>(&value)) {
            return p->find(key) != p->end();
        }
        return false;
    }

    const JsonValue& operator[](const std::string& key) const {
        static const JsonValue null_val;
        if (auto p = std::get_if<JsonObject>(&value)) {
            auto it = p->find(key);
            if (it != p->end()) return it->second;
        }
        return null_val;
    }

    const JsonValue& operator[](size_t index) const {
        static const JsonValue null_val;
        if (auto p = std::get_if<JsonArray>(&value)) {
            if (index < p->size()) return (*p)[index];
        }
        return null_val;
    }
};

class JsonParser {
public:
    static JsonValue parse(const std::string& text) {
        size_t pos = 0;
        skip_whitespace(text, pos);
        if (pos >= text.size()) return JsonValue();
        return parse_value(text, pos);
    }

private:
    static void skip_whitespace(const std::string& text, size_t& pos) {
        while (pos < text.size()) {
            char c = text[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                pos++;
            } else if (c == '/' && pos + 1 < text.size() && text[pos + 1] == '/') {
                pos += 2;
                while (pos < text.size() && text[pos] != '\n') pos++;
            } else {
                break;
            }
        }
    }

    static JsonValue parse_value(const std::string& text, size_t& pos) {
        skip_whitespace(text, pos);
        if (pos >= text.size()) return JsonValue();

        char c = text[pos];
        if (c == 'n') return parse_null(text, pos);
        if (c == 't' || c == 'f') return parse_bool(text, pos);
        if (c == '"') return parse_string(text, pos);
        if (c == '[') return parse_array(text, pos);
        if (c == '{') return parse_object(text, pos);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number(text, pos);

        pos++;
        return JsonValue();
    }

    static JsonValue parse_null(const std::string& text, size_t& pos) {
        if (text.substr(pos, 4) == "null") {
            pos += 4;
            return JsonValue();
        }
        pos++;
        return JsonValue();
    }

    static JsonValue parse_bool(const std::string& text, size_t& pos) {
        if (text.substr(pos, 4) == "true") {
            pos += 4;
            return JsonValue(true);
        }
        if (text.substr(pos, 5) == "false") {
            pos += 5;
            return JsonValue(false);
        }
        pos++;
        return JsonValue(false);
    }

    static JsonValue parse_string(const std::string& text, size_t& pos) {
        pos++; // skip opening quote
        std::string s;
        while (pos < text.size()) {
            char c = text[pos++];
            if (c == '"') return JsonValue(std::move(s));
            if (c == '\\' && pos < text.size()) {
                char esc = text[pos++];
                if (esc == '"') s += '"';
                else if (esc == '\\') s += '\\';
                else if (esc == '/') s += '/';
                else if (esc == 'b') s += '\b';
                else if (esc == 'f') s += '\f';
                else if (esc == 'n') s += '\n';
                else if (esc == 'r') s += '\r';
                else if (esc == 't') s += '\t';
                else s += esc;
            } else {
                s += c;
            }
        }
        return JsonValue(std::move(s));
    }

    static JsonValue parse_number(const std::string& text, size_t& pos) {
        size_t start = pos;
        if (text[pos] == '-') pos++;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) pos++;
        if (pos < text.size() && text[pos] == '.') {
            pos++;
            while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) pos++;
        }
        if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
            pos++;
            if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) pos++;
            while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) pos++;
        }
        std::string num_str = text.substr(start, pos - start);
        try {
            double val = std::stod(num_str);
            return JsonValue(val);
        } catch (...) {
            return JsonValue(0.0);
        }
    }

    static JsonValue parse_array(const std::string& text, size_t& pos) {
        pos++; // skip '['
        JsonArray arr;
        while (pos < text.size()) {
            skip_whitespace(text, pos);
            if (pos < text.size() && text[pos] == ']') {
                pos++;
                return JsonValue(std::move(arr));
            }
            arr.push_back(parse_value(text, pos));
            skip_whitespace(text, pos);
            if (pos < text.size() && text[pos] == ',') {
                pos++;
            }
        }
        return JsonValue(std::move(arr));
    }

    static JsonValue parse_object(const std::string& text, size_t& pos) {
        pos++; // skip '{'
        JsonObject obj;
        while (pos < text.size()) {
            skip_whitespace(text, pos);
            if (pos < text.size() && text[pos] == '}') {
                pos++;
                return JsonValue(std::move(obj));
            }
            if (text[pos] != '"') {
                pos++;
                continue;
            }
            JsonValue key_val = parse_string(text, pos);
            skip_whitespace(text, pos);
            if (pos < text.size() && text[pos] == ':') {
                pos++;
            }
            JsonValue val = parse_value(text, pos);
            obj.emplace(key_val.as_string(), std::move(val));
            skip_whitespace(text, pos);
            if (pos < text.size() && text[pos] == ',') {
                pos++;
            }
        }
        return JsonValue(std::move(obj));
    }
};

} // namespace di
