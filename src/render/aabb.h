/*
 * Copyright (c) 2020-2021 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "glm/mat4x4.hpp"
#include "glm/vec3.hpp"

namespace reone {

namespace render {

class AABB {
public:
    AABB() = default;
    AABB(const glm::vec3 &min, const glm::vec3 &max);

    AABB operator*(const glm::mat4 &m) const;

    void reset();
    void expand(const glm::vec3 &p);
    void expand(const AABB &aabb);

    bool contains(const glm::vec2 &point) const;
    bool contains(const glm::vec3 &point) const;
    bool intersect(const AABB &other) const;

    /**
     * Casts a ray and tests if it intersects this AABB.
     *
     * @param origin ray origin
     * @param invDir ray direction inverse
     * @param[out] distance to the intersection point (negative if ray origin is inside this AABB)
     */
    bool raycast(const glm::vec3 &origin, const glm::vec3 &invDir, float &distance) const;

    glm::vec3 getSize() const;

    /**
     * @return distance from the closest point of this AABB to the specified point
     */
    float getDistanceFromClosestPoint(const glm::vec3 &point) const;

    /**
     * @return squared distance from the closest point of this AABB to the specified point
     */
    float getDistanceFromClosestPoint2(const glm::vec3 &point) const;

    const glm::vec3 &min() const { return _min; }
    const glm::vec3 &max() const { return _max; }
    const glm::vec3 &center() const { return _center; }
    const glm::mat4 &transform() const { return _transform; }

private:
    bool _empty { true };
    glm::vec3 _min { 0.0f };
    glm::vec3 _max { 0.0f };
    glm::vec3 _center { 0.0f };
    glm::mat4 _transform { 1.0f };

    void updateTransform();

    inline glm::vec3 getClosestPoint(const glm::vec3 &point) const;
};

} // namespace render

} // namespace reone
