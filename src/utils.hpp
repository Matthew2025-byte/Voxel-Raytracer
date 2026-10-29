#pragma once
#include <cmath>

struct Vector3 {
	float x, y, z;

	float sqrMagnitude() const { return x * x + y * y + z * z; }
	float magnitude() const { return std::sqrtf(sqrMagnitude()); }
	Vector3 normalized() const { return *this / magnitude(); }

	Vector3& operator+=(const Vector3& other) {
		x += other.x;
		y += other.y;
		z += other.z;
		return *this;
	}
	Vector3& operator-=(const Vector3& other) {
		x -= other.x;
		y -= other.y;
		z -= other.z;
		return *this;
	}
	Vector3& operator*=(const float& scalar) {
		x *= scalar;
		y *= scalar;
		z *= scalar;
		return *this;
	}
	Vector3& operator/=(const float& scalar) {
		x /= scalar;
		y /= scalar;
		z /= scalar;
		return *this;
	}

	Vector3 operator*(const float& scalar) const {
		Vector3 result(*this);
		result *= scalar;
		return result;
	}
	Vector3 operator/(const float& scalar) const {
		Vector3 result(*this);
		result /= scalar;
		return result;
	}

	Vector3() : x(0), y(0), z(0) {}
	Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
};