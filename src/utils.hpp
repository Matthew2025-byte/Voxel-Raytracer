#pragma once
#include <cmath>
#include <concepts>

template <typename T>
	requires std::integral<T> || std::floating_point<T>
struct Vector3 {
	T x, y, z;

	T sqrMagnitude() const { return x * x + y * y + z * z; }
	T magnitude() const { return std::sqrtf(sqrMagnitude()); }
	Vector3 normalized() const { return *this / magnitude(); }
	Vector3 cross(const Vector3& other) const {
		T x = this->y * other.z - this->z * other.y;
		T y = this->z * other.x - this->x * other.z;
		T z = this->x * other.y - this->y * other.x;
		return { x, y, z };
	}
	T dot(const Vector3& other) const { return x * other.x + y * other.y + z * other.z; }

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
	Vector3& operator*=(const T& scalar) {
		x *= scalar;
		y *= scalar;
		z *= scalar;
		return *this;
	}
	Vector3& operator/=(const T& scalar) {
		x /= scalar;
		y /= scalar;
		z /= scalar;
		return *this;
	}
	Vector3 operator+(const Vector3& other) const { return { x + other.x, y + other.y, z + other.z }; }
	Vector3 operator-(const Vector3& other) const { return { x - other.x, y - other.y, z - other.z }; }

	Vector3 operator%(const T& scalar) const { return { x % scalar, y % scalar, z % scalar }; }
	bool operator==(const Vector3& other) const {
		return (this->x == other.x) && (this->y == other.y) && (this->z == other.z);
	}

	Vector3 operator*(const T& scalar) const {
		Vector3 result(*this);
		result *= scalar;
		return result;
	}
	Vector3 operator*(const Vector3& other) const { return { x * other.x, y * other.y, z * other.z }; }
	Vector3 operator/(const T& scalar) const {
		Vector3 result(*this);
		result /= scalar;
		return result;
	}

	Vector3() : x(0), y(0), z(0) {}
	Vector3(T x, T y, T z) : x(x), y(y), z(z) {}

	template <typename newType>
		requires std::integral<newType> || std::floating_point<newType>
	Vector3<newType> cast() const {
		return { static_cast<newType>(x), static_cast<newType>(y), static_cast<newType>(z) };
	}
};

template <typename T>
struct std::hash<Vector3<T>> {
	std::size_t operator()(const Vector3<T>& v) const noexcept {
		std::size_t h1 = std::hash<T>{}(v.x);
		std::size_t h2 = std::hash<T>{}(v.y);
		std::size_t h3 = std::hash<T>{}(v.z);
		return h1 ^ (h2 << 1) ^ (h3 << 2);
	}
};

using Vector3f = Vector3<float>;

struct Quaternion {
	float w, x, y, z;

	Quaternion(float w, float x, float y, float z) : w(w), x(x), y(y), z(z) {}

	Quaternion normalized() const {
		float mag = std::sqrtf(w * w + x * x + y * y + z * z);
		return { w / mag, x / mag, y / mag, z / mag };
	}

	Quaternion conjugate() const { return { w, -x, -y, -z }; }

	Quaternion operator*(const Quaternion& other) const {
		return { w * other.w - x * other.x - y * other.y - z * other.z,
				 w * other.x + x * other.w + y * other.z - z * other.y,
				 w * other.y - x * other.z + y * other.w + z * other.x,
				 w * other.z + x * other.y - y * other.x + z * other.w };
	}

	static Quaternion fromAxisAngle(Vector3f axis, float angle) {
		axis = axis.normalized();
		float half = angle * 0.5f;
		float s = std::sinf(half);

		return { std::cosf(half), axis.x * s, axis.y * s, axis.z * s };
	}

	Vector3f rotate(const Vector3f& v) const {
		Quaternion p = { 0, v.x, v.y, v.z };
		Quaternion result = (*this) * p * this->conjugate();
		return { result.x, result.y, result.z };
	}
};