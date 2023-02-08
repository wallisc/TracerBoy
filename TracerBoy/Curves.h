#pragma once

pbrt::math::vec3f CalculatePointOnQuadraticBezier(
	const pbrt::math::vec3f& v0,
	const pbrt::math::vec3f& v1,
	const pbrt::math::vec3f& v2,
	float t);

pbrt::math::vec3f CalculateTangentOnCubicBezier(
	const pbrt::math::vec3f& v0,
	const pbrt::math::vec3f& v1,
	const pbrt::math::vec3f& v2,
	const pbrt::math::vec3f& v3,
	float t);

void CalculateObjectSpaceAxisOnCubicBezier(
	const pbrt::math::vec3f& v0,
	const pbrt::math::vec3f& v1,
	const pbrt::math::vec3f& v2,
	const pbrt::math::vec3f& v3,
	float t,
	pbrt::math::vec3f& forward,
	pbrt::math::vec3f& normal0,
	pbrt::math::vec3f& normal1);


pbrt::math::vec3f CalculatePointOnCubicBezier(
	const pbrt::math::vec3f& v0,
	const pbrt::math::vec3f& v1,
	const pbrt::math::vec3f& v2,
	const pbrt::math::vec3f& v3,
	float t);
