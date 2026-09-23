#pragma once

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <stdexcept>
#include <cctype>

namespace di {

enum class JsonType {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object
};

class JsonValue {
public:
    JsonType type = JsonType::Null;
    bool bool_val = false;
    double num_val = 0.0;
    std::string str_val;
    std::vector<JsonValue> arr_val;
    std::map<std::string, JsonValue> obj_val;

    JsonValue() : type(JsonType::Null) {}
    explicit JsonValue(bool b) : type(JsonType::Boolean), bool_val(b) {}
    explicit JsonValue(double n) : type(JsonType::Number), num_val(n) {}
    explicit JsonValue(int n) : type(JsonType::Number), num_val(static_cast<double>(n)) {}
    explicit JsonValue(const std::string& s) : type(JsonType::String), str_val(s) {}
    explicit JsonValue(const char* s) : type(JsonType::String), str_val(s ? s : "") {}
    explicit JsonValue(const std::vector<JsonValue>& a) : type(JsonType::Array), arr_val(a) {}
    explicit JsonValue(const std::map<std::string, JsonValue>& o) : type(JsonType::Object), obj_val(o) {}

    bool is_null() const { return type == JsonType::Null; }
    bool is_bool() const { return type == JsonType::Boolean; }
    bool is_number() const { return type == JsonType::Number; }
    bool is_string() const { return type == JsonType::String; }
    bool is_array() const { return type == JsonType::Array; }
    bool is_object() const { return type == JsonType::Object; }

    bool as_bool(bool default_val = false) const {
        return (type == JsonType::Boolean) ? bool_val : default_val;
    }

    double as_double(double default_val = 0.0) const {
        return (type == JsonType::Number) ? num_val : default_val;
    }

    int as_int(int default_val = 0) const {
        return (type == JsonType::Number) ? static_cast<int>(num_val) : default_val;
    }

    std::string as_string(const std::string& default_val = "") const {
        return (type == JsonType::String) ? str_val : default_val;
    }

    bool has(const std::string& key) const {
        if (type != JsonType::Object) return false;
        return obj_val.find(key) != obj_val.end();
    }

    const JsonValue& operator[](const std::string& key) const {
        static JsonValue null_val;
        if (type != JsonType::Object) return null_val;
        auto it = obj_val.find(key);
        if (it != obj_val.end()) return it->second;
        return null_val;
    }

    const JsonValue& operator[](size_t index) const {
        static JsonValue null_val;
        if (type != JsonType::Array || index >= arr_val.size()) return null_val;
        return arr_val[index];
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
            if (c == '"') return JsonValue(s);
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
        return JsonValue(s);
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
        std::vector<JsonValue> arr;
        while (pos < text.size()) {
            skip_whitespace(text, pos);
            if (pos < text.size() && text[pos] == ']') {
                pos++;
                return JsonValue(arr);
            }
            arr.push_back(parse_value(text, pos));
            skip_whitespace(text, pos);
            if (pos < text.size() && text[pos] == ',') {
                pos++;
            }
        }
        return JsonValue(arr);
    }

    static JsonValue parse_object(const std::string& text, size_t& pos) {
        pos++; // skip '{'
        std::map<std::string, JsonValue> obj;
        while (pos < text.size()) {
            skip_whitespace(text, pos);
            if (pos < text.size() && text[pos] == '}') {
                pos++;
                return JsonValue(obj);
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
            obj[key_val.as_string()] = val;
            skip_whitespace(text, pos);
            if (pos < text.size() && text[pos] == ',') {
                pos++;
            }
        }
        return JsonValue(obj);
    }
};

} // namespace di
