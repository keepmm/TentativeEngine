/*****************************************************************//**
 * \file   Mathf.hpp
 * \brief  float の数学関数
 * 
 * \author keepm
 * \date   October 2026
 *********************************************************************/
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace TE::Mathf
{
	inline constexpr float PI = 3.14159265358979323846f;
	inline constexpr float Deg2Rad = PI / 180.0f;
	inline constexpr float Rad2Deg = 180.0f / PI;
	inline constexpr float Infinity = std::numeric_limits<float>::infinity();
	inline constexpr float NegativeInfinity = -std::numeric_limits<float>::infinity();

	inline float Sin(float radians) { return  std::sin(radians); }
	inline float Cos(float radians) { return  std::cos(radians); }
	inline float Tan(float radians) { return  std::tan(radians); }
	inline float Asin(float f) { return std::asin(f); }
	inline float Acos(float f) { return std::acos(f); }
	inline float Atan(float f) { return std::atan(f); }
	// 引数は y が先（std::atan2 と同じ）。点 (x, y) の角度を返す
	inline float Atan2(float y, float x) { return std::atan2(y, x); }
	inline float Sqrt(float f) { return std::sqrt(f); };
	inline float Pow(float f, float p) { return std::pow(f, p); }
	inline float Exp(float power) { return std::exp(power); }
	inline float Log(float f) { return std::log(f);}
	inline float Log(float f, float base) { return std::log(f) / std::log(base); }
	inline float Log10(float f){ return std::log10(f); }
	inline float Floor(float f) { return std::floor(f); }
	inline float Ceil(float f) { return std::ceil(f); }
	
	// 0.5は偶数側に丸める
	inline float Round(float f) { return std::nearbyint(f); }
	inline int FloorToInt(float f) { return static_cast<int>(Floor(f)); }
	inline int CeilToInt(float f) { return static_cast<int>(Ceil(f)); }
	inline int RoundToInt(float f) { return static_cast<int>(Round(f)); }

	constexpr float Abs(float f) { return f < 0.0f ? -f : f; }
	constexpr int Abs(int i) { return i < 0 ? -i : i; }
	constexpr float Min(float a, float b) { return a < b ? a : b; }
	constexpr int Min(int a, int b) { return a < b ? a : b; }
	constexpr float Max(float a, float b) { return a > b ? a : b; }
	constexpr int Max(int a, int b) { return a > b ? a : b; }

	constexpr float Sign(float f) { return f >= 0.0f ? 1.0f : -1.0f; }

	constexpr float Clamp(float value, float min, float max)
	{
		return value < min ? min : (value > max ? max : value);
	}

	constexpr int Clamp(int value, int min, int max)
	{
		return value < min ? min : (value > max ? max : value);
	}

	constexpr float Clamp01(float value) { return Clamp(value, 0.0f, 1.0f); }

	constexpr float LerpUnclamped(float a, float b, float t) { return a + (b - a) * t; }
	constexpr float Lerp(float a, float b, float t) { return LerpUnclamped(a, b, Clamp01(t)); }
	
	constexpr float InverseLerp(float a, float b, float value)
	{
		return a != b ? Clamp01((value - a) / (b - a)) : 0.0f;
	}

	constexpr float SmoothStep(float from, float to, float t)
	{
		t = Clamp01(t);
		t = t * t * (3.0f - 2.0f * t);
		return from + (to - from) * t;
	}

	constexpr bool Approximately(float a, float b)
	{
		return Abs(b - a) < Max(1e-6f * Max(Abs(a), Abs(b)), 1e-5f);
	}

	constexpr float MoveTowards(float current, float target, float maxDelta)
	{
		if (Abs(target - current) <= maxDelta)
		{
			return target;
		}
		return current + Sign(target - current) * maxDelta;
	}

	inline float Repeat(float t, float length)
	{
		return Clamp(t - Floor(t / length) * length, 0.0f, length);
	}

	inline float PingPong(float t, float length)
	{
		t = Repeat(t, length * 2.0f);
		return length - Abs(t - length);
	}

	inline float DeltaAngle(float current, float target)
	{
		float delta = Repeat(target - current, 360.0f);
		if (delta > 180.0f)
		{
			delta -= 360.0f;
		}
		return delta;
	}
	
	inline float LerpAngle(float a, float b, float t) {
		return a + DeltaAngle(a, b) * Clamp01(t);
	}

	inline float MoveTowardsAngle(float current, float target, float maxDelta)
	{
		const float delta = DeltaAngle(current, target);
		if (-maxDelta < delta && delta < maxDelta) {
			return target;
		}
		return MoveTowards(current, current + delta, maxDelta);
	}

	inline float SmoothDamp(float current, float target, float& currentVelocity,
		float smoothTime, float deltaTime, float maxSpeed = Infinity)
	{
		// 臨界減衰ばね(行き過ぎずに最短で止まるばね)を多項式で近似して解く
		smoothTime = Max(0.0001f, smoothTime);
		const float omega = 2.0f / smoothTime;
		const float x = omega * deltaTime;
		const float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

		const float maxChange = maxSpeed * smoothTime;
		const float change = Clamp(current - target, -maxChange, maxChange);
		const float clampedTarget = current - change;

		const float temp = (currentVelocity + omega * change) * deltaTime;
		currentVelocity = (currentVelocity - omega * temp) * decay;
		float output = clampedTarget + (change + temp) * decay;

		// 本来の目標を通り過ぎたら、そこで止める
		if ((target - current > 0.0f) == (output > target)) {
			output = target;
			currentVelocity = 0.0f;
		}
		return output;
	}
	constexpr bool IsPowerOfTwo(int value) { return value > 0 && (value & (value - 1)) == 0; }

	// value 以上で最小の 2 のべき乗。0 以下なら 0
	constexpr int NextPowerOfTwo(int value) {
		if (value <= 0) {
			return 0;
		}
		// 一番上の 1 より下のビットをすべて 1 にしてから 1 を足すと、次の 2 のべき乗になる
		std::uint32_t v = static_cast<std::uint32_t>(value) - 1u;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		return static_cast<int>(v + 1u);
	}

}