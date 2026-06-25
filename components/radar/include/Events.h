#pragma once

#include <memory>
#include <string>
#include <cstring>

#include "JsonWrapper.h"

// Radar detection value types, ported from doorbell2/events.h to use JsonWrapper.
struct Value {
    virtual ~Value() = default;
    virtual const char* etype() const { return "und"; }
    virtual std::unique_ptr<Value> clone() const = 0;
    virtual float get_main() const { return 0.0f; }
    virtual float get_power() const { return 0.0f; }
    virtual void toJson(JsonWrapper& doc) const {
        doc.AddItem("type", std::string(etype()));
    }
    bool isTarget() const { return std::strcmp(etype(), "no") != 0; }
};

struct Range : public Value {
    float x = 0.0f, y = 0.0f, speed = 0.0f;
    int   reference = 0;

    Range(float x, float y, float speed, int reference = 0)
        : x(x), y(y), speed(speed), reference(reference) {}

    const char* etype() const override { return "rng"; }
    float get_main() const override { return speed; }
    std::unique_ptr<Value> clone() const override { return std::make_unique<Range>(*this); }

    void toJson(JsonWrapper& doc) const override {
        Value::toJson(doc);
        doc.AddItem("x", x);
        doc.AddItem("y", y);
        doc.AddItem("speed", speed);
        doc.AddItem("reference", reference);
    }
};

struct NoTarget : public Value {
    const char* etype() const override { return "no"; }
    std::unique_ptr<Value> clone() const override { return std::make_unique<NoTarget>(*this); }
};

class EventProc {
public:
    virtual ~EventProc() = default;
    virtual void Detected(Value* vv)       = 0;
    virtual void Cleared()                 = 0;
    virtual void TrackingUpdate(Value* cc) = 0;
    virtual void PresenceUpdate(Value* cc) = 0;
};
