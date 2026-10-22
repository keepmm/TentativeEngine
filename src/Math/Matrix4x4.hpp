/*****************************************************************//**
 * \file   Matrix4x4.hpp
 * \brief  4x4行列
 * 
 * \author keepm
 * \date   October 2026
 *********************************************************************/
#pragma once

#include "Quaternion.hpp"
#include "Vector4.hpp"

namespace TE
{
	struct Matrix4x4
	{
		// 初期値は単位行列
		float m[4][4] = {
			{1.0f,0.0f,0.0f,0.0f},
			{0.0f,1.0f,0.0f,0.0f},
			{0.0f,0.0f,1.0f,0.0f},
			{0.0f,0.0f,0.0f,1.0f},
		};

		static const Matrix4x4 identity;
		static const Matrix4x4 zero;

		friend constexpr Matrix4x4 operator*(const Matrix4x4& a, const Matrix4x4& b) {
			Matrix4x4 r;
			for (int row = 0; row < 4; ++row) {
				for (int col = 0; col < 4; ++col) {
					float sum = 0.0f;
					for (int k = 0; k < 4; ++k) {
						sum += a.m[row][k] * b.m[k][col];
					}
					r.m[row][col] = sum;
				}
			}
			return r;
		}

		// 行ベクトル v * M

		friend constexpr Vector4 operator*(const Vector4& v, const Matrix4x4& a)
		{
			return {
				v.x * a.m[0][0] + v.y * a.m[1][0] + v.z * a.m[2][0] + v.w * a.m[3][0],
				v.x * a.m[0][1] + v.y * a.m[1][1] + v.z * a.m[2][1] + v.w * a.m[3][1],
				v.x * a.m[0][2] + v.y * a.m[1][2] + v.z * a.m[2][2] + v.w * a.m[3][2],
				v.x * a.m[0][3] + v.y * a.m[1][3] + v.z * a.m[2][3] + v.w * a.m[3][3],
			};
		}

		constexpr Vector4 GetRow(int row)const 
		{
			return {
				m[row][0],m[row][1],m[row][2],m[row][3]
			};
		}

		constexpr Vector4 GetColumn(int col) const
		{
			return {
				m[0][col],m[1][col],m[2][col],m[3][col]
			};
		}

		constexpr Vector3 GetPosition() const 
		{
			return {
				m[3][0],m[3][1], m[3][2]
			};
		}

		/// @brief 位置として変換し、wで割る
		constexpr Vector3 MultiplyPoint(const Vector3& v)const
		{
			const Vector4 r = Vector4::Point(v) * *this;
			return { r.x / r.w, r.y / r.w, r.z / r.w };
		}

		constexpr Vector3 MultiplyPoint3x4(const Vector3& v) const
		{
			return {
				v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0] + m[3][0],
				v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1] + m[3][1],
				v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2] + m[3][2],
			};
		}

		constexpr Vector3 MultiplyVector(const Vector3& v) const
		{
			return {
				v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0],
				v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1],
				v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2],
			};
		}

		/// @brief 4×4行列を転置します。
		/// @return 行と列を入れ替えた4×4行列。
		constexpr Matrix4x4 Transposed() const
		{
			Matrix4x4 r;
			for (int row = 0; row < 4; ++row)
			{
				for (int col = 0; col < 4; ++col)
				{
					r.m[row][col] = m[col][row];
				}
			}
			return r;
		}

		constexpr float Determinant() const {
			const float s0 = m[0][0] * m[1][1] - m[1][0] * m[0][1];
			const float s1 = m[0][0] * m[1][2] - m[1][0] * m[0][2];
			const float s2 = m[0][0] * m[1][3] - m[1][0] * m[0][3];
			const float s3 = m[0][1] * m[1][2] - m[1][1] * m[0][2];
			const float s4 = m[0][1] * m[1][3] - m[1][1] * m[0][3];
			const float s5 = m[0][2] * m[1][3] - m[1][2] * m[0][3];
			const float c5 = m[2][2] * m[3][3] - m[3][2] * m[2][3];
			const float c4 = m[2][1] * m[3][3] - m[3][1] * m[2][3];
			const float c3 = m[2][1] * m[3][2] - m[3][1] * m[2][2];
			const float c2 = m[2][0] * m[3][3] - m[3][0] * m[2][3];
			const float c1 = m[2][0] * m[3][2] - m[3][0] * m[2][2];
			const float c0 = m[2][0] * m[3][1] - m[3][0] * m[2][1];
			return s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
		}

		constexpr Matrix4x4 Inverse() const
		{
			const float s0 = m[0][0] * m[1][1] - m[1][0] * m[0][1];
			const float s1 = m[0][0] * m[1][2] - m[1][0] * m[0][2];
			const float s2 = m[0][0] * m[1][3] - m[1][0] * m[0][3];
			const float s3 = m[0][1] * m[1][2] - m[1][1] * m[0][2];
			const float s4 = m[0][1] * m[1][3] - m[1][1] * m[0][3];
			const float s5 = m[0][2] * m[1][3] - m[1][2] * m[0][3];
			const float c5 = m[2][2] * m[3][3] - m[3][2] * m[2][3];
			const float c4 = m[2][1] * m[3][3] - m[3][1] * m[2][3];
			const float c3 = m[2][1] * m[3][2] - m[3][1] * m[2][2];
			const float c2 = m[2][0] * m[3][3] - m[3][0] * m[2][3];
			const float c1 = m[2][0] * m[3][2] - m[3][0] * m[2][2];
			const float c0 = m[2][0] * m[3][1] - m[3][0] * m[2][1];

			const float det = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;

			if (det == 0.0f)
			{
				return Matrix4x4{
					0.0f,0.0f,0.0f,0.0f,
					0.0f,0.0f,0.0f,0.0f,
					0.0f,0.0f,0.0f,0.0f,
					0.0f,0.0f,0.0f,0.0f,
				};
			}
			const float inv = 1.0f / det;

			// 余因子行列の転置 / 行列式
			Matrix4x4 r;
			r.m[0][0] = ( m[1][1] * c5 - m[1][2] * c4 + m[1][3] * c3) * inv;
			r.m[0][1] = (-m[0][1] * c5 + m[0][2] * c4 - m[0][3] * c3) * inv;
			r.m[0][2] = ( m[3][1] * s5 - m[3][2] * s4 + m[3][3] * s3) * inv;
			r.m[0][3] = (-m[2][1] * s5 + m[2][2] * s4 - m[2][3] * s3) * inv;
			r.m[1][0] = (-m[1][0] * c5 + m[1][2] * c2 - m[1][3] * c1) * inv;
			r.m[1][1] = ( m[0][0] * c5 - m[0][2] * c2 + m[0][3] * c1) * inv;
			r.m[1][2] = (-m[3][0] * s5 + m[3][2] * s2 - m[3][3] * s1) * inv;
			r.m[1][3] = ( m[2][0] * s5 - m[2][2] * s2 + m[2][3] * s1) * inv;
			r.m[2][0] = ( m[1][0] * c4 - m[1][1] * c2 + m[1][3] * c0) * inv;
			r.m[2][1] = (-m[0][0] * c4 + m[0][1] * c2 - m[0][3] * c0) * inv;
			r.m[2][2] = ( m[3][0] * s4 - m[3][1] * s2 + m[3][3] * s0) * inv;
			r.m[2][3] = (-m[2][0] * s4 + m[2][1] * s2 - m[2][3] * s0) * inv;
			r.m[3][0] = (-m[1][0] * c3 + m[1][1] * c1 - m[1][2] * c0) * inv;
			r.m[3][1] = ( m[0][0] * c3 - m[0][1] * c1 + m[0][2] * c0) * inv;
			r.m[3][2] = (-m[3][0] * s3 + m[3][1] * s1 - m[3][2] * s0) * inv;
			r.m[3][3] = ( m[2][0] * s3 - m[2][1] * s1 + m[2][2] * s0) * inv;
			return r;
		}

		/// @brief 指定された移動ベクトルに基づく平行移動行列を生成します。
		/// @param t 平行移動量を表す3次元ベクトル。
		/// @return 指定された平行移動を表す4×4行列。
		static constexpr Matrix4x4 Translate(const Vector3& t)
		{
			Matrix4x4 r;
			r.m[3][0] = t.x;
			r.m[3][1] = t.y;
			r.m[3][2] = t.z;
			return r;
		}

		static constexpr Matrix4x4 Scale(const Vector3& s)
		{
			Matrix4x4 r;
			r.m[0][0] = s.x;
			r.m[1][1] = s.y;
			r.m[2][2] = s.z;
			return r;
		}

		static constexpr Matrix4x4 Rotate(const Quaternion& q)
		{
			const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
			const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
			const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

			Matrix4x4 r;
			r.m[0][0] = 1.0f - 2.0f * (yy + zz);
			r.m[0][1] = 2.0f * (xy + wz);
			r.m[0][2] = 2.0f * (xz - wy);
			r.m[1][0] = 2.0f * (xy - wz);
			r.m[1][1] = 1.0f - 2.0f * (xx + zz);
			r.m[1][2] = 2.0f * (yz + wx);
			r.m[2][0] = 2.0f * (xz + wy);
			r.m[2][1] = 2.0f * (yz - wx);
			r.m[2][2] = 1.0f - 2.0f * (xx + yy);
			return r;
		}

		/// @brief ワールド行列
		static constexpr Matrix4x4 TRS(
			const Vector3& position,
			const Quaternion& rotation,
			const Vector3& scale)
		{
			return Scale(scale) * Rotate(rotation) * Translate(position);
		}

		/// @brief 法線用の行列(ワールド行列の逆行列の転置)
		constexpr Matrix4x4 NormalMatrix()const
		{
			return Inverse().Transposed();
		}

		/// @brief カメラの位置と回転からビュー行列を作成する
		static Matrix4x4 View(const Vector3& position,const Quaternion& rotation)
		{
			return Translate(-position) * Rotate(Quaternion::Inverse(rotation));
		}

		/// @brief 左手座標系のビュー行列を作成します。
		/// @param eye カメラの位置。
		/// @param target カメラが見る対象の位置。
		/// @param up カメラの上方向を示すベクトル。
		/// @return 指定されたカメラの位置、対象位置、上方向に基づく4×4のビュー行列。
		static Matrix4x4 LookAtLH(const Vector3& eye, const Vector3& target, const Vector3& up)
		{
			const Vector3 zAxis = (target - eye).Normalized();
			const Vector3 xAxis = Vector3::Cross(up, zAxis).Normalized();
			const Vector3 yAxis = Vector3::Cross(zAxis, xAxis);

			Matrix4x4 r;
			r.m[0][0] = xAxis.x;
			r.m[0][1] = yAxis.x;
			r.m[0][2] = zAxis.x;
			r.m[1][0] = xAxis.y;
			r.m[1][1] = yAxis.y;
			r.m[1][2] = zAxis.y;
			r.m[2][0] = xAxis.z;
			r.m[2][1] = yAxis.z;
			r.m[2][2] = zAxis.z;
			r.m[3][0] = -Vector3::Dot(xAxis, eye);
			r.m[3][1] = -Vector3::Dot(yAxis, eye);
			r.m[3][2] = -Vector3::Dot(zAxis, eye);

			return r;
		}

		/// @brief 透視投影の作成 
		static Matrix4x4 PerspectiveFovLH(float fovY, float aspect, float nearZ, float farZ)
		{
			const float yScale = 1.0f / Mathf::Tan(fovY * Mathf::Deg2Rad * 0.5f);
			const float xScale = yScale / aspect;
			const float range = farZ / (farZ - nearZ);

			Matrix4x4 r;
			r.m[0][0] = xScale;
			r.m[1][1] = yScale;
			r.m[2][2] = range;
			r.m[2][3] = 1.0f;
			r.m[3][2] = -nearZ * range;
			r.m[3][3] = 0.0f;

			return r;
		}

		/// @brief 平行投影で
		static constexpr Matrix4x4 OrthographicLH(float width, float height, float nearZ, float farZ)
		{
			const float range = 1.0f / (farZ - nearZ);

			Matrix4x4 r;
			r.m[0][0] = 2.0f / width;
			r.m[1][1] = 2.0f / height;
			r.m[2][2] = range;
			r.m[3][2] = -nearZ * range;
			return r;
		}

		/// @brief 平行投影で、うつす範囲を左右上下で指定できる
		static constexpr Matrix4x4 OrthographicOffCenterLH(float left, float right, float bottom,
			float top, float nearZ, float farZ)
		{
			const float range = 1.0f / (farZ - nearZ);

			Matrix4x4 r;
			r.m[0][0] = 2.0f / (right - left);
			r.m[1][1] = 2.0f / (top - bottom);
			r.m[2][2] = range;
			r.m[3][0] = (left + right) / (left - right);
			r.m[3][1] = (top + bottom) / (bottom - top);
			r.m[3][2] = -nearZ * range;
			return r;
		}
	};

	inline constexpr Matrix4x4 Matrix4x4::identity{};
	inline constexpr Matrix4x4 Matrix4x4::zero{ { { 0.0f, 0.0f, 0.0f, 0.0f },{ 0.0f, 0.0f, 0.0f, 0.0f },
												  { 0.0f, 0.0f, 0.0f, 0.0f },{ 0.0f, 0.0f, 0.0f, 0.0f } } };
}
