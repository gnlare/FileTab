#include "Json.h"

#include <charconv>
#include <cmath>
#include <cstdlib>

namespace ft {

const Json* Json::Find(std::string_view key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& m : members_)
        if (m.first == key) return &m.second;
    for (const auto& m : members_) {
        if (m.first.size() != key.size()) continue;
        bool same = true;
        for (size_t i = 0; i < key.size(); ++i) {
            char a = m.first[i], b = key[i];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
            if (a != b) { same = false; break; }
        }
        if (same) return &m.second;
    }
    return nullptr;
}

namespace {

class Parser {
public:
    explicit Parser(std::string_view s) : s_(s) {}

    Json ParseDocument() {
        if (s_.size() >= 3 && static_cast<unsigned char>(s_[0]) == 0xEF && static_cast<unsigned char>(s_[1]) == 0xBB &&
            static_cast<unsigned char>(s_[2]) == 0xBF)
            pos_ = 3;
        Json v = ParseValue(0);
        SkipWs();
        if (pos_ != s_.size()) Fail("문서 끝에 예상하지 못한 내용이 있습니다");
        return v;
    }

private:
    [[noreturn]] void Fail(const char* msg) const {
        throw JsonError(std::string("JSON 오류(위치 ") + std::to_string(pos_) + "): " + msg);
    }
    void SkipWs() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\r' || s_[pos_] == '\n')) ++pos_;
    }
    bool Consume(char c) {
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == c) { ++pos_; return true; }
        return false;
    }
    bool ConsumeLiteral(const char* lit) {
        size_t n = std::char_traits<char>::length(lit);
        if (s_.compare(pos_, n, lit) == 0) { pos_ += n; return true; }
        return false;
    }

    Json ParseValue(int depth) {
        if (depth > 64) Fail("중첩이 너무 깊습니다");
        SkipWs();
        if (pos_ >= s_.size()) Fail("값이 필요합니다");
        char c = s_[pos_];
        if (c == '{') return ParseObject(depth);
        if (c == '[') return ParseArray(depth);
        if (c == '"') return Json::MakeString(ParseString());
        if (ConsumeLiteral("true")) return Json::MakeBool(true);
        if (ConsumeLiteral("false")) return Json::MakeBool(false);
        if (ConsumeLiteral("null")) return Json();
        return ParseNumber();
    }

    Json ParseObject(int depth) {
        ++pos_;  // '{'
        Json obj = Json::MakeObject();
        if (Consume('}')) return obj;
        for (;;) {
            SkipWs();
            if (pos_ >= s_.size() || s_[pos_] != '"') Fail("속성 이름이 필요합니다");
            std::string key = ParseString();
            if (!Consume(':')) Fail("':' 가 필요합니다");
            obj.Set(std::move(key), ParseValue(depth + 1));
            if (Consume(',')) continue;
            if (Consume('}')) break;
            Fail("',' 또는 '}' 가 필요합니다");
        }
        return obj;
    }

    Json ParseArray(int depth) {
        ++pos_;  // '['
        Json arr = Json::MakeArray();
        if (Consume(']')) return arr;
        for (;;) {
            arr.Add(ParseValue(depth + 1));
            if (Consume(',')) continue;
            if (Consume(']')) break;
            Fail("',' 또는 ']' 가 필요합니다");
        }
        return arr;
    }

    unsigned ParseHex4() {
        if (pos_ + 4 > s_.size()) Fail("\\u 이스케이프가 잘렸습니다");
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else Fail("잘못된 \\u 이스케이프");
        }
        return v;
    }

    static void AppendCodePoint(std::string& out, unsigned cp) {
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    std::string ParseString() {
        ++pos_;  // opening quote
        std::string out;
        for (;;) {
            if (pos_ >= s_.size()) Fail("문자열이 닫히지 않았습니다");
            char c = s_[pos_++];
            if (c == '"') break;
            if (c != '\\') {
                if (static_cast<unsigned char>(c) < 0x20) Fail("문자열에 제어 문자가 있습니다");
                out.push_back(c);
                continue;
            }
            if (pos_ >= s_.size()) Fail("이스케이프가 잘렸습니다");
            char e = s_[pos_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned cp = ParseHex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                        size_t save = pos_;
                        pos_ += 2;
                        unsigned lo = ParseHex4();
                        if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else pos_ = save;
                    }
                    AppendCodePoint(out, cp);
                    break;
                }
                default: Fail("알 수 없는 이스케이프");
            }
        }
        return out;
    }

    Json ParseNumber() {
        size_t start = pos_;
        if (pos_ < s_.size() && s_[pos_] == '-') ++pos_;
        while (pos_ < s_.size() && ((s_[pos_] >= '0' && s_[pos_] <= '9') || s_[pos_] == '.' || s_[pos_] == 'e' ||
                                    s_[pos_] == 'E' || s_[pos_] == '+' || s_[pos_] == '-'))
            ++pos_;
        if (pos_ == start) Fail("알 수 없는 값");
        std::string text(s_.substr(start, pos_ - start));
        char* end = nullptr;
        double v = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0') Fail("잘못된 숫자");
        return Json::MakeNumber(v);
    }

    std::string_view s_;
    size_t pos_ = 0;
};

void EscapeInto(std::string& out, const std::string& s) {
    out.push_back('"');
    for (char ch : s) {
        unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[c >> 4]);
                    out.push_back(hex[c & 15]);
                } else {
                    out.push_back(ch);
                }
        }
    }
    out.push_back('"');
}

std::string NumberToString(double v) {
    if (!std::isfinite(v)) return "null";
    if (std::floor(v) == v && std::fabs(v) < 1e15) return std::to_string(static_cast<long long>(v));
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof buf, v);
    return std::string(buf, res.ptr);
}

void DumpValue(std::string& out, const Json& v, int indentWidth, int level) {
    auto newline = [&](int lvl) {
        out.push_back('\n');
        out.append(static_cast<size_t>(lvl * indentWidth), ' ');
    };
    switch (v.type()) {
        case Json::Type::Null: out += "null"; break;
        case Json::Type::Bool: out += v.AsBool() ? "true" : "false"; break;
        case Json::Type::Number: out += NumberToString(v.AsNumber()); break;
        case Json::Type::String: EscapeInto(out, v.AsString()); break;
        case Json::Type::Array: {
            if (v.Items().empty()) { out += "[]"; break; }
            out.push_back('[');
            bool first = true;
            for (const auto& item : v.Items()) {
                if (!first) out.push_back(',');
                first = false;
                newline(level + 1);
                DumpValue(out, item, indentWidth, level + 1);
            }
            newline(level);
            out.push_back(']');
            break;
        }
        case Json::Type::Object: {
            if (v.Members().empty()) { out += "{}"; break; }
            out.push_back('{');
            bool first = true;
            for (const auto& m : v.Members()) {
                if (!first) out.push_back(',');
                first = false;
                newline(level + 1);
                EscapeInto(out, m.first);
                out += ": ";
                DumpValue(out, m.second, indentWidth, level + 1);
            }
            newline(level);
            out.push_back('}');
            break;
        }
    }
}

}  // namespace

Json ParseJson(std::string_view text) { return Parser(text).ParseDocument(); }

std::string DumpJson(const Json& value, int indentWidth) {
    std::string out;
    DumpValue(out, value, indentWidth, 0);
    return out;
}

}  // namespace ft
