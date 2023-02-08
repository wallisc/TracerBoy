#include "pch.h"

pbrt::math::vec3f CalculatePointOnQuadraticBezier(
	const pbrt::math::vec3f& v0,
	const pbrt::math::vec3f& v1,
	const pbrt::math::vec3f& v2,
	float t)
{
	VERIFY(t >= 0.0f && t <= 1.0f);
	return (v0 * (1.0f - t) + v1 * t) * (1.0f - t) + (v1 * (1.0f - t) + v2 * t) * t;
}

pbrt::math::vec3f CalculateTangentOnCubicBezier(
	const pbrt::math::vec3f& v0,
	const pbrt::math::vec3f& v1,
	const pbrt::math::vec3f& v2,
	const pbrt::math::vec3f& v3,
	float t)
{
	VERIFY(t >= 0.0f && t <= 1.0f);
	return (v1 - v0) * 3.0 * (1.0 - t) * (1.0 - 1) + (v2 - v1) * 6.0 * t * (1.0 - t) + (v3 - v2) * 3.0 * t * t;
}


pbrt::math::vec3f CalculatePointOnCubicBezier(
	const pbrt::math::vec3f& v0,
	const pbrt::math::vec3f& v1,
	const pbrt::math::vec3f& v2,
	const pbrt::math::vec3f& v3,
	float t)
{
	VERIFY(t >= 0.0f && t <= 1.0f);
	return CalculatePointOnQuadraticBezier(v0, v1, v2, t) * (1.0f - t) + CalculatePointOnQuadraticBezier(v1, v2, v3, t) * t;;
}

void CalculateObjectSpaceAxisOnCubicBezier(
	const pbrt::math::vec3f& v0,
	const pbrt::math::vec3f& v1,
	const pbrt::math::vec3f& v2,
	const pbrt::math::vec3f& v3,
	float t,
	pbrt::math::vec3f& forward,
	pbrt::math::vec3f& normal0,
	pbrt::math::vec3f& normal1)
{
	pbrt::math::vec3f tangent = CalculateTangentOnCubicBezier(v0, v1, v2, v3, t);
	forward = pbrt::math::normalize(tangent);
	pbrt::math::vec3f up = forward.y < 0.99f ? pbrt::math::vec3f(0, 1, 0) : pbrt::math::vec3f(0, 0, 1);
	normal0 = pbrt::math::normalize(pbrt::math::cross(forward, up));
	normal1 = pbrt::math::normalize(pbrt::math::cross(normal0, forward));
}