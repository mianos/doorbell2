#pragma once
#include <ArduinoJson.h>

inline double rr(double value) {
      return (int)(value * 100.0 + 0.5) / 100.0;
}

struct Value {
  virtual const char* etype() const { return "und"; }
  virtual void print() const { Serial.printf("un-overridden '%s'\n", etype()); }
  virtual std::unique_ptr<Value> clone() const = 0;
  virtual bool isEqual(const Value& other) const = 0;
  virtual float get_main() const { return 0.0; }
  virtual float get_power() const { return 0.0; }
  virtual void toJson(JsonDocument &doc) const {
    doc["type"] = etype();
  }
};


struct Range : public Value {
  float x = 0.0;
  float y = 0.0;
  float speed;
  int reference;

  const char* etype() const override { return "rng"; }

  Range(float x, float y, float speed, int reference=0) : x(x), y(y), speed(speed), reference(reference) {}
  virtual float get_main() { return speed; }

  virtual void print() const override {
    Serial.printf("speed %1.2f x pos %1.2f Y pos %1.2f %2d\n", speed, x, y, reference);
  }

  std::unique_ptr<Value> clone() const override {
    return std::unique_ptr<Value>(new Range(*this));
  }

  bool isEqual(const Value& other) const override {
    const Range& o = static_cast<const Range&>(other);
    return x == o.x && y == o.y && speed == o.speed && reference == o.reference;
  }
  virtual void toJson(JsonDocument &doc) const {
    Value::toJson(doc);
    doc["x"] = rr(x);
    doc["y"] = rr(y);
    doc["speed"] = rr(speed);
    doc["reference"] = reference;
  }
};

struct NoTarget : public Value {
  const char* etype() const override { return "no"; }

  virtual void print() const override {
    Serial.printf("no target\n");
  }

  std::unique_ptr<Value> clone() const override {
    return std::unique_ptr<Value>(new NoTarget(*this));
  }

  bool isEqual(const Value& other) const override {
    return true;
  }
};


class EventProc {
public:
  virtual void Detected(Value *vv) = 0;
  virtual void Cleared() = 0;
  virtual void TrackingUpdate(Value *cc) = 0;
  virtual void PresenceUpdate(Value *cc) = 0;
};

