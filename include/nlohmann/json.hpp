#pragma once

#include <cctype>
#include <initializer_list>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace nlohmann {

class json {
public:
    using object_t = std::map<std::string, json>;
    using array_t = std::vector<json>;

    struct item {
        const std::string& key_ref;
        const json& value_ref;
        const std::string& key() const { return key_ref; }
        const json& value() const { return value_ref; }
    };

    class items_view {
    public:
        explicit items_view(const object_t& object) : object_(object) {}
        class iterator {
        public:
            using base_iter = object_t::const_iterator;
            explicit iterator(base_iter iter) : iter_(iter) {}
            iterator& operator++() { ++iter_; return *this; }
            bool operator!=(const iterator& other) const { return iter_ != other.iter_; }
            item operator*() const { return item{iter_->first, iter_->second}; }
        private:
            base_iter iter_;
        };
        iterator begin() const { return iterator(object_.begin()); }
        iterator end() const { return iterator(object_.end()); }
    private:
        const object_t& object_;
    };

    json() : value_(object_t{}) {}
    json(std::nullptr_t) : value_(nullptr) {}
    json(bool value) : value_(value) {}
    json(double value) : value_(value) {}
    json(int value) : value_(static_cast<double>(value)) {}
    json(const char* value) : value_(std::string(value)) {}
    json(const std::string& value) : value_(value) {}
    json(std::string&& value) : value_(std::move(value)) {}
    json(const array_t& value) : value_(value) {}
    json(array_t&& value) : value_(std::move(value)) {}
    json(const object_t& value) : value_(value) {}
    json(object_t&& value) : value_(std::move(value)) {}
    json(std::initializer_list<std::pair<const std::string, json>> init) : value_(object_t{}) {
        auto& obj = std::get<object_t>(value_);
        for (const auto& entry : init) {
            obj.emplace(entry.first, entry.second);
        }
    }

    static json array(std::initializer_list<json> init) {
        return json(array_t(init));
    }

    bool is_object() const { return std::holds_alternative<object_t>(value_); }
    bool is_array() const { return std::holds_alternative<array_t>(value_); }
    bool is_string() const { return std::holds_alternative<std::string>(value_); }
    bool is_number() const { return std::holds_alternative<double>(value_); }

    bool contains(const std::string& key) const {
        return is_object() && std::get<object_t>(value_).count(key) > 0;
    }

    const json& at(const std::string& key) const {
        if (!is_object()) throw std::runtime_error("json is not an object");
        return std::get<object_t>(value_).at(key);
    }

    json& operator[](const std::string& key) {
        if (!is_object()) {
            value_ = object_t{};
        }
        return std::get<object_t>(value_)[key];
    }

    template <typename T>
    T get() const {
        if constexpr (std::is_same_v<T, std::string>) {
            if (!is_string()) throw std::runtime_error("json is not string");
            return std::get<std::string>(value_);
        } else if constexpr (std::is_same_v<T, double>) {
            if (!is_number()) throw std::runtime_error("json is not number");
            return std::get<double>(value_);
        } else {
            static_assert(!sizeof(T), "Unsupported json::get type");
        }
    }

    items_view items() const {
        if (!is_object()) throw std::runtime_error("json is not an object");
        return items_view(std::get<object_t>(value_));
    }

    array_t::const_iterator begin() const {
        if (!is_array()) throw std::runtime_error("json is not an array");
        return std::get<array_t>(value_).begin();
    }

    array_t::const_iterator end() const {
        if (!is_array()) throw std::runtime_error("json is not an array");
        return std::get<array_t>(value_).end();
    }

    std::string dump(int = -1) const {
        return dump_impl(*this);
    }

    static json parse(const std::string& text) {
        std::size_t index = 0;
        const json parsed = parse_value(text, index);
        skip_ws(text, index);
        if (index != text.size()) throw std::runtime_error("unexpected trailing characters in json");
        return parsed;
    }

private:
    std::variant<std::nullptr_t, bool, double, std::string, array_t, object_t> value_;

    static void skip_ws(const std::string& text, std::size_t& i) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    }

    static json parse_string(const std::string& text, std::size_t& i) {
        if (text[i] != '"') throw std::runtime_error("expected string");
        ++i;
        std::string out;
        while (i < text.size()) {
            char ch = text[i++];
            if (ch == '"') return json(out);
            if (ch == '\\') {
                if (i >= text.size()) throw std::runtime_error("invalid escape");
                const char esc = text[i++];
                if (esc == '"' || esc == '\\' || esc == '/') out.push_back(esc);
                else if (esc == 'b') out.push_back('\b');
                else if (esc == 'f') out.push_back('\f');
                else if (esc == 'n') out.push_back('\n');
                else if (esc == 'r') out.push_back('\r');
                else if (esc == 't') out.push_back('\t');
                else throw std::runtime_error("unsupported escape");
                continue;
            }
            out.push_back(ch);
        }
        throw std::runtime_error("unterminated string");
    }

    static json parse_number(const std::string& text, std::size_t& i) {
        const std::size_t start = i;
        if (text[i] == '-') ++i;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
        if (i < text.size() && text[i] == '.') {
            ++i;
            while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
        }
        return json(std::stod(text.substr(start, i - start)));
    }

    static json parse_value(const std::string& text, std::size_t& i) {
        skip_ws(text, i);
        if (i >= text.size()) throw std::runtime_error("unexpected end");
        if (text[i] == '{') {
            ++i;
            object_t obj;
            skip_ws(text, i);
            if (i < text.size() && text[i] == '}') { ++i; return json(obj); }
            while (true) {
                skip_ws(text, i);
                const std::string key = parse_string(text, i).get<std::string>();
                skip_ws(text, i);
                if (text[i++] != ':') throw std::runtime_error("expected :");
                obj.emplace(key, parse_value(text, i));
                skip_ws(text, i);
                if (text[i] == '}') { ++i; break; }
                if (text[i++] != ',') throw std::runtime_error("expected ,");
            }
            return json(obj);
        }
        if (text[i] == '[') {
            ++i;
            array_t arr;
            skip_ws(text, i);
            if (i < text.size() && text[i] == ']') { ++i; return json(arr); }
            while (true) {
                arr.push_back(parse_value(text, i));
                skip_ws(text, i);
                if (text[i] == ']') { ++i; break; }
                if (text[i++] != ',') throw std::runtime_error("expected ,");
            }
            return json(arr);
        }
        if (text[i] == '"') return parse_string(text, i);
        if (text.compare(i, 4, "true") == 0) { i += 4; return json(true); }
        if (text.compare(i, 5, "false") == 0) { i += 5; return json(false); }
        if (text.compare(i, 4, "null") == 0) { i += 4; return json(nullptr); }
        return parse_number(text, i);
    }

    static std::string escape(const std::string& value) {
        std::string out = "\"";
        for (char ch : value) {
            if (ch == '"' || ch == '\\') {
                out.push_back('\\');
                out.push_back(ch);
            } else if (ch == '\n') {
                out += "\\n";
            } else {
                out.push_back(ch);
            }
        }
        out.push_back('"');
        return out;
    }

    static std::string dump_impl(const json& j) {
        if (std::holds_alternative<std::nullptr_t>(j.value_)) return "null";
        if (std::holds_alternative<bool>(j.value_)) return std::get<bool>(j.value_) ? "true" : "false";
        if (std::holds_alternative<double>(j.value_)) {
            std::ostringstream out;
            out << std::get<double>(j.value_);
            return out.str();
        }
        if (std::holds_alternative<std::string>(j.value_)) return escape(std::get<std::string>(j.value_));
        if (std::holds_alternative<array_t>(j.value_)) {
            std::ostringstream out;
            out << "[";
            bool first = true;
            for (const auto& e : std::get<array_t>(j.value_)) {
                if (!first) out << ",";
                first = false;
                out << dump_impl(e);
            }
            out << "]";
            return out.str();
        }
        std::ostringstream out;
        out << "{";
        bool first = true;
        for (const auto& [k, v] : std::get<object_t>(j.value_)) {
            if (!first) out << ",";
            first = false;
            out << escape(k) << ":" << dump_impl(v);
        }
        out << "}";
        return out.str();
    }
};


}  // namespace nlohmann
