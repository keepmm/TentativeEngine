/*****************************************************************//**
 * \file   Vector2.hpp
 * \brief  二次元ベクトル
 * 
 * \author keepm
 * \date   October 2026
 *********************************************************************/
#pragma once

#include "Mathf.hpp"

namespace TE
{
	struct Vector2
	{
		float x = 0.0f;
		float y = 0.0f;

		static const Vector2 zero;
		static const Vector2 one;
		static const Vector2 up;
		static const Vector2 down;
		static const Vector2 left;
		static const Vector2 right;

		// ---- 演算子 ---- //

		friend constexpr Vector2 operator+(const Vector2& a, const Vector2& b) { return { a.x + b.x, a.y + b.y }; }
		friend constexpr Vector2 operator-(const Vector2& a, const Vector2& b) { return { a.x - b.x, a.y - b.y }; }
		friend constexpr Vector2 operator-(const Vector2& v) { return { -v.x, -v.y }; }
		friend constexpr Vector2 operator*(const Vector2& v, float s) { return { v.x * s, v.y * s }; }
		friend constexpr Vector2 operator*(float s, const Vector2& v) { return v * s; }
		friend constexpr Vector2 operator/(const Vector2& v, float s) { return { v.x / s, v.y / s }; }
		friend constexpr Vector2& operator+=(Vector2& a, const Vector2& b) { return a = a + b; }
		friend constexpr Vector2& operator-=(Vector2& a, const Vector2& b) { return a = a - b; }
		friend constexpr Vector2& operator*=(Vector2& v, float s) { return v = v * s; }
		friend constexpr Vector2& operator/=(Vector2& v, float s) { return v = v / s; }

		constexpr float& operator[](int i) { return i == 0 ? x : y; }
		constexpr float operator[](int i) const { return i == 0 ? x : y; }

		constexpr float SqrMagnitude() const { return Dot(*this, *this); }

		/// @brief ベクトルの大きさを計算します。
		/// @return ベクトルの大きさを表す浮動小数点値。
		float Magnitude() const { return Mathf::Sqrt(SqrMagnitude()); }

		/// @brief 長さを 1 にしたベクトルを返します。
		/// @return 長さがほぼ 0 なら 0 ベクトル（0 で割って NaN が広がるのを防ぐ）
		Vector2 Normalized() const
		{
			const float magnitude = Magnitude();
			return magnitude > 1e-5f ? *this / magnitude : Vector2{};
		}

		/// @brief このベクトル自身の長さを 1 にします。
		void Normalize() { *this = Normalized(); }

		/// @brief 2次元ベクトルの内積を計算します。
		/// @param a 内積を計算する最初のベクトル。
		/// @param b 内積を計算する2番目のベクトル。
		/// @return 2つのベクトルの内積。
		static constexpr float Dot(const Vector2& a, const Vector2& b)
		{
			return a.x * b.x + a.y * b.y;
		}

		/// @brief 二つのVector間の距離を求める
		/// @return Vector間の距離
		static float Distance(const Vector2& a, const Vector2& b)
		{
			return (a - b).Magnitude();
		}

		/// @brief 2つのベクトルの各成分を乗算します。
		/// @param a 乗算する最初のベクトル。
		/// @param b 乗算する2番目のベクトル。
		/// @return aとbの各成分を乗算したVector2。
		static constexpr Vector2 Scale(const Vector2& a, const Vector2& b)
		{
			return { a.x * b.x, a.y * b.y };
		}

		/// @brief 2つのVector2の各成分の最小値で構成されたベクトルを返します。
		/// @param a 比較する最初のVector2
		/// @param b 比較する2番目のVector2
		/// @return aとbの各成分の最小値で構成されたVector2
		static constexpr Vector2 Min(const Vector2& a, const Vector2& b)
		{
			return { Mathf::Min(a.x, b.x), Mathf::Min(a.y, b.y) };
		}

		/// @brief 2つのVector2の各成分の最大値からなるVector2を返します。
		/// @param a 比較する最初のVector2です。
		/// @param b 比較する2番目のVector2です。
		/// @return aとbの各x成分およびy成分のうち、大きい値で構成されたVector2です。
		static constexpr Vector2 Max(const Vector2& a, const Vector2& b)
		{
			return { Mathf::Max(a.x, b.x), Mathf::Max(a.y, b.y) };
		}

		/// @brief 2つのベクトル間を、範囲制限なしで線形補間します。
		/// @param a 補間の始点となるベクトル。
		/// @param b 補間の終点となるベクトル。
		/// @param t 補間係数。0未満または1を超える値も使用できます。
		/// @return 指定された補間係数に基づく線形補間結果のベクトル。
		static constexpr Vector2 LerpUnclamped(const Vector2& a, const Vector2& b, float t)
		{
			return a + (b - a) * t;
		}

		/// @brief 2つのベクトル間を線形補間します。
		/// @param a 補間の始点となるベクトル。
		/// @param b 補間の終点となるベクトル。
		/// @param t 補間位置を指定する値。0から1の範囲にクランプされます。
		/// @return 指定された補間位置に対応するベクトル。
		static constexpr Vector2 Lerp(const Vector2& a, const Vector2& b, float t)
		{
			return LerpUnclamped(a, b, Mathf::Clamp01(t));
		}

		/// @brief 2次元ベクトルに垂直なベクトルを計算します。
		/// @param v 垂直なベクトルを計算する対象の2次元ベクトル。
		/// @return vを反時計回りに90度回転した垂直な2次元ベクトル。
		static constexpr Vector2 Perpendicular(const Vector2& v) { return { -v.y,v.x };}

		/// @brief 指定された方向ベクトルを法線ベクトルに基づいて反射します。
		/// @param inDirection 反射させる入力方向ベクトル。
		/// @param inNormal 反射面の法線ベクトル。
		/// @return 反射後の方向ベクトル。
		static constexpr Vector2 Reflect(const Vector2& inDirection, const Vector2& inNormal)
		{
			return inDirection - inNormal * (2.0f * Dot(inDirection, inNormal));
		}

		/// @brief 2つのベクトル間の角度を度単位で計算します。
		/// @param from 角度の始点となる2次元ベクトル。
		/// @param to 角度の終点となる2次元ベクトル。
		/// @return 2つのベクトル間の角度を度単位で返します。いずれかのベクトルの長さがほぼゼロの場合は0.0を返します。
		static float Angle(const Vector2& from, const Vector2& to)
		{
			const float denominator = Mathf::Sqrt(from.SqrMagnitude() * to.SqrMagnitude());
			if (denominator < 1e-15f)
			{
				return 0.0f;
			}
			// 誤差で 1 をわずかに超えると acos が NaN になるので、範囲に収めてから渡す
			const float cosine = Mathf::Clamp(Dot(from, to) / denominator, -1.0f, 1.0f);
			return Mathf::Acos(cosine) * Mathf::Rad2Deg;
		}

		// 符号付きの角度（度、-180〜180）。反時計回りが正
		static float SignedAngle(const Vector2& from, const Vector2& to) {
			// 2 次元の外積（z 成分だけ）の符号で回る向きがわかる
			const float cross = from.x * to.y - from.y * to.x;
			return Angle(from, to) * Mathf::Sign(cross);
		}

		// 長さを maxLength までに抑える
		static Vector2 ClampMagnitude(const Vector2& v, float maxLength) {
			const float sqr = v.SqrMagnitude();
			if (sqr <= maxLength * maxLength) {
				return v;
			}
			return v / Mathf::Sqrt(sqr) * maxLength;
		}

		// target へ向かって最大 maxDistanceDelta だけ進む。行き過ぎない
		static Vector2 MoveTowards(const Vector2& current, const Vector2& target, float maxDistanceDelta) {
			const Vector2 toTarget = target - current;
			const float distance = toTarget.Magnitude();
			if (distance <= maxDistanceDelta || distance == 0.0f) {
				return target;
			}
			return current + toTarget / distance * maxDistanceDelta;
		}
	};

	inline constexpr Vector2 Vector2::zero{ 0.0f, 0.0f };
	inline constexpr Vector2 Vector2::one{ 1.0f, 1.0f };
	inline constexpr Vector2 Vector2::up{ 0.0f, 1.0f };
	inline constexpr Vector2 Vector2::down{ 0.0f, -1.0f };
	inline constexpr Vector2 Vector2::left{ -1.0f, 0.0f };
	inline constexpr Vector2 Vector2::right{ 1.0f, 0.0f };
}