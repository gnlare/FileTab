// 설정 파일용 최소 JSON 파서/작성기 (외부 라이브러리 없이 System.Text.Json 형식과 호환)
#pragma once
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ft {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    static Json MakeBool(bool v) { Json j; j.type_ = Type::Bool; j.bool_ = v; return j; }
    static Json MakeNumber(double v) { Json j; j.type_ = Type::Number; j.number_ = v; return j; }
    static Json MakeString(std::string v) { Json j; j.type_ = Type::String; j.string_ = std::move(v); return j; }
    static Json MakeArray() { Json j; j.type_ = Type::Array; return j; }
    static Json MakeObject() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }
    bool IsObject() const { return type_ == Type::Object; }
    bool IsArray() const { return type_ == Type::Array; }

    bool AsBool(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
    double AsNumber(double fallback = 0) const { return type_ == Type::Number ? number_ : fallback; }
    int AsInt(int fallback = 0) const {
        if (type_ != Type::Number || !std::isfinite(number_) ||
            number_ < static_cast<double>(std::numeric_limits<int>::min()) ||
            number_ > static_cast<double>(std::numeric_limits<int>::max())) return fallback;
        return static_cast<int>(number_);
    }
    const std::string& AsString() const { return string_; }
    bool IsString() const { return type_ == Type::String; }
    bool IsNumber() const { return type_ == Type::Number; }

    const std::vector<Json>& Items() const { return array_; }
    const std::vector<std::pair<std::string, Json>>& Members() const { return members_; }

    // 객체에서 키를 찾는다(정확히 일치 → 대소문자 무시 순). 없으면 nullptr.
    const Json* Find(std::string_view key) const;

    Json& Add(Json value) { array_.push_back(std::move(value)); return array_.back(); }
    Json& Set(std::string key, Json value) {
        members_.emplace_back(std::move(key), std::move(value));
        return members_.back().second;
    }

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0;
    std::string string_;
    std::vector<Json> array_;
    std::vector<std::pair<std::string, Json>> members_;
};

struct JsonError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

Json ParseJson(std::string_view text);                       // 실패 시 JsonError
std::string DumpJson(const Json& value, int indentWidth = 2);

}  // namespace ft
