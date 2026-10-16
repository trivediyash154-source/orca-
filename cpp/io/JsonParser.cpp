#include "JsonParser.hpp"

#include <cctype>
#include <cstdlib>

namespace orca::io {

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    JsonValue parseTopLevel() {
        skipWhitespace();
        JsonValue value = parseValue();
        skipWhitespace();
        if (pos_ != text_.size()) {
            fail("trailing content after JSON value");
        }
        return value;
    }

private:
    const std::string& text_;
    std::size_t pos_ = 0;

    [[noreturn]] void fail(const std::string& what) const {
        throw JsonError("JsonParser: " + what + " at offset " + std::to_string(pos_));
    }

    [[nodiscard]] char peek() const {
        if (pos_ >= text_.size()) fail("unexpected end of input");
        return text_[pos_];
    }

    char advance() {
        if (pos_ >= text_.size()) fail("unexpected end of input");
        return text_[pos_++];
    }

    void expect(char c) {
        if (advance() != c) fail(std::string("expected '") + c + "'");
    }

    void skipWhitespace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool consumeLiteral(const char* literal) {
        const std::size_t len = std::char_traits<char>::length(literal);
        if (text_.compare(pos_, len, literal) == 0) {
            pos_ += len;
            return true;
        }
        return false;
    }

    JsonValue parseValue() {
        skipWhitespace();
        if (pos_ >= text_.size()) fail("unexpected end of input");
        const char c = peek();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return JsonValue(parseString());
        if (c == 't') {
            if (!consumeLiteral("true")) fail("invalid literal");
            return JsonValue(true);
        }
        if (c == 'f') {
            if (!consumeLiteral("false")) fail("invalid literal");
            return JsonValue(false);
        }
        if (c == 'n') {
            if (!consumeLiteral("null")) fail("invalid literal");
            return JsonValue(nullptr);
        }
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        fail("unexpected character");
    }

    JsonValue parseObject() {
        expect('{');
        JsonValue obj = JsonValue::object();
        skipWhitespace();
        if (pos_ < text_.size() && peek() == '}') {
            ++pos_;
            return obj;
        }
        while (true) {
            skipWhitespace();
            if (peek() != '"') fail("expected string key");
            std::string key = parseString();
            skipWhitespace();
            expect(':');
            JsonValue value = parseValue();
            obj.set(std::move(key), std::move(value));
            skipWhitespace();
            const char next = advance();
            if (next == ',') continue;
            if (next == '}') break;
            fail("expected ',' or '}' in object");
        }
        return obj;
    }

    JsonValue parseArray() {
        expect('[');
        JsonValue arr = JsonValue::array();
        skipWhitespace();
        if (pos_ < text_.size() && peek() == ']') {
            ++pos_;
            return arr;
        }
        while (true) {
            arr.push(parseValue());
            skipWhitespace();
            const char next = advance();
            if (next == ',') continue;
            if (next == ']') break;
            fail("expected ',' or ']' in array");
        }
        return arr;
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (true) {
            if (pos_ >= text_.size()) fail("unterminated string");
            const char c = text_[pos_++];
            if (c == '"') break;
            if (c == '\\') {
                if (pos_ >= text_.size()) fail("unterminated escape");
                const char esc = text_[pos_++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': out.push_back('\r'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'u': {
                        // \uXXXX -- decoded as a raw code point truncated to one byte if
                        // outside ASCII, since no command or view field this project
                        // parses needs non-ASCII text. Documented narrowness, not a bug.
                        if (pos_ + 4 > text_.size()) fail("truncated \\u escape");
                        const unsigned code =
                            static_cast<unsigned>(std::strtoul(text_.substr(pos_, 4).c_str(), nullptr, 16));
                        pos_ += 4;
                        out.push_back(static_cast<char>(code & 0xFF));
                        break;
                    }
                    default:
                        fail("invalid escape sequence");
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    JsonValue parseNumber() {
        const std::size_t start = pos_;
        if (peek() == '-') ++pos_;
        while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }
        if (pos_ == start) fail("invalid number");
        const std::string token = text_.substr(start, pos_ - start);
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (end != token.c_str() + token.size()) fail("invalid number");
        return JsonValue(value);
    }
};

}  // namespace

JsonValue parseJson(const std::string& text) { return Parser(text).parseTopLevel(); }

}  // namespace orca::io
