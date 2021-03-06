//__________________________ Banshee Project - A modern game development toolkit _________________________________//
//_____________________________________ www.banshee-project.com __________________________________________________//
//________________________ Copyright (c) 2014 Marko Pintera. All rights reserved. ________________________________//
#pragma once

#include "BsPrerequisitesUtil.h"
#include "BsVector3.h"

namespace BansheeEngine 
{
    /**
     * @brief	A plane represented by a normal and a distance.
     */
    class BS_UTILITY_EXPORT Plane
    {
	public:
		/**
		 * @brief	The "positive side" of the plane is the half space to which the
		 *			plane normal points. The "negative side" is the other half
		 *			space. The flag "no side" indicates the plane itself.
         */
        enum Side
        {
            NO_SIDE,
            POSITIVE_SIDE,
            NEGATIVE_SIDE,
            BOTH_SIDE
        };

    public:
        Plane();
        Plane(const Plane& copy);
        Plane(const Vector3& normal, float d);
		Plane(float a, float b, float c, float d);
        Plane(const Vector3& normal, const Vector3& point);
        Plane(const Vector3& point0, const Vector3& point1, const Vector3& point2);

        /**
         * @brief	Returns the side of the plane where the point is located on.
         * 			
		 * @note	NO_SIDE signifies the point is on the plane.
         */
        Side getSide(const Vector3& point) const;

        /**
		 * @brief	Returns the side where the alignedBox is. The flag BOTH_SIDE indicates an intersecting box.
		 *			One corner ON the plane is sufficient to consider the box and the plane intersecting.
         */
        Side getSide(const AABox& box) const;

        /**
		 * @brief	Returns the side where the sphere is. The flag BOTH_SIDE indicates an intersecting sphere.
         */
		Side getSide(const Sphere& sphere) const;

        /**
         * @brief	Returns a distance from point to plane.
         *
		 * @note	The sign of the return value is positive if the point is on the 
		 * 			positive side of the plane, negative if the point is on the negative 
		 * 			side, and zero if the point is on the plane.
         */
        float getDistance(const Vector3& point) const;

		/**
		 * @brief	Project a vector onto the plane.
		 */
		Vector3 projectVector(const Vector3& v) const;

        /**
		 * @brief	Normalizes the plane's normal and the length scale of d
		 *			is as well.
         */
        float normalize();

		/**
		 * @brief	Box/plane intersection.
		 */
		bool intersects(const AABox& box) const;

		/**
		 * @brief	Sphere/plane intersection.
		 */
		bool intersects(const Sphere& sphere) const;

		/**
		 * @brief	Ray/plane intersection, returns boolean result and distance to intersection point.
		 */
		std::pair<bool, float> intersects(const Ray& ray) const;

        bool operator==(const Plane& rhs) const
        {
            return (rhs.d == d && rhs.normal == normal);
        }
        bool operator!=(const Plane& rhs) const
        {
            return (rhs.d != d || rhs.normal != normal);
        }

	public:
		Vector3 normal;
		float d;
    };

    typedef Vector<Plane> PlaneList;
}