#ifndef AI_JSON_HPP
#define AI_JSON_HPP

#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <sstream>
#include <cstdlib>

namespace aijson {

// Lightweight JSON value type
class Value {
public:
    enum Type { Null, Bool, Number, String, Array, Object };
    Type type = Null;
    bool boolVal = false;
    double numVal = 0;
    std::string strVal;
    std::vector<Value> arrVal;
    std::map<std::string, Value> objVal;

    static Value parse(const std::string& json);

    const Value& operator[](const std::string& key) const {
        static Value nullVal;
        auto it = objVal.find(key);
        return it != objVal.end() ? it->second : nullVal;
    }
    const Value& operator[](size_t i) const {
        static Value nullVal;
        return i < arrVal.size() ? arrVal[i] : nullVal;
    }
    // convenience getters
    std::string getString(const std::string& key, const std::string& def = "") const {
        auto it = objVal.find(key);
        return (it != objVal.end() && it->second.type == String) ? it->second.strVal : def;
    }
    double getNumber(const std::string& key, double def = 0) const {
        auto it = objVal.find(key);
        return (it != objVal.end() && it->second.type == Number) ? it->second.numVal : def;
    }
    int getInt(const std::string& key, int def = 0) const {
        return (int)getNumber(key, (double)def);
    }
    size_t size() const {
        if (type == Array) return arrVal.size();
        if (type == Object) return objVal.size();
        return 0;
    }
};

// Parser implementation
class Parser {
    const char* p;
    const char* end;
    void skipWS() { while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; }
    void expect(char c) { if (p >= end || *p != c) throw std::runtime_error("Expected char"); p++; }

    Value parseValue() {
        skipWS();
        if (p >= end) return {};
        if (*p == '"') return parseString();
        if (*p == '{') return parseObject();
        if (*p == '[') return parseArray();
        if (*p == 't' || *p == 'f') return parseBool();
        if (*p == 'n') return parseNull();
        return parseNumber();
    }

    Value parseNull() {
        if (p + 4 <= end && std::string(p, 4) == "null") { p += 4; return {}; }
        throw std::runtime_error("Expected null");
    }

    Value parseBool() {
        Value v; v.type = Value::Bool;
        if (p + 4 <= end && std::string(p, 4) == "true") { v.boolVal = true; p += 4; }
        else if (p + 5 <= end && std::string(p, 5) == "false") { v.boolVal = false; p += 5; }
        else throw std::runtime_error("Expected bool");
        return v;
    }

    Value parseString() {
        expect('"');
        Value v; v.type = Value::String;
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) { p++; /* skip escape for now */ }
            v.strVal += *p++;
        }
        if (p < end) p++; // skip closing "
        return v;
    }

    Value parseNumber() {
        Value v; v.type = Value::Number;
        const char* start = p;
        if (*p == '-') p++;
        while (p < end && isdigit(*p)) p++;
        if (p < end && *p == '.') { p++; while (p < end && isdigit(*p)) p++; }
        v.numVal = strtod(start, nullptr);
        return v;
    }

    Value parseArray() {
        expect('[');
        Value v; v.type = Value::Array;
        skipWS();
        if (*p != ']') {
            v.arrVal.push_back(parseValue());
            skipWS();
            while (*p == ',') {
                p++;
                skipWS();                       // 逗号后可能是空白 (与对象分支对称;
                                                // parseValue 本身也会 skip, 此处显式对齐)
                v.arrVal.push_back(parseValue());
                skipWS();
            }
        }
        expect(']');
        return v;
    }

    Value parseObject() {
        expect('{');
        Value v; v.type = Value::Object;
        skipWS();
        if (*p != '}') {
            auto key = parseString();
            skipWS(); expect(':');
            v.objVal[key.strVal] = parseValue();
            skipWS();
            while (*p == ',') {
                p++;
                skipWS();                       // 修复 2026-09-23: parseString() 内部是
                                                // expect('"') (不跳空白), 缺此次 skipWS 会让
                                                // 「逗号后带空格/换行」的格式化 JSON 抛
                                                // Expected char —— 只吃得下紧凑 JSON。
                auto k2 = parseString();
                skipWS(); expect(':');
                v.objVal[k2.strVal] = parseValue();
                skipWS();
            }
        }
        expect('}');
        return v;
    }

public:
    explicit Parser(const std::string& s) : p(s.c_str()), end(s.c_str() + s.size()) {}
    Value parse() { return parseValue(); }
};

inline Value Value::parse(const std::string& json) {
    return Parser(json).parse();
}

} // namespace aijson
#endif
