/*
Minetest
Copyright (C) 2021 Liso <anlismon@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include "irrlichttypes_bloated.h"
#include <matrix4.h>
#include "util/basic_macros.h"

class Camera;
class Client;

struct shadowFrustum
{
	float zNear{0.0f};
	float zFar{0.0f};
	float length{0.0f};
	core::matrix4 ProjOrthMat;
	core::matrix4 ViewMat;
	v3f position;
};

class DirectionalLight
{
public:
	DirectionalLight(const u32 shadowMapResolution,
			const v3f &position,
			video::SColorf lightColor = video::SColor(0xffffffff),
			f32 farValue = 100.0f);
	~DirectionalLight() = default;

	//DISABLE_CLASS_COPY(DirectionalLight)

	void update_frustum(const Camera *cam, Client *client);

	// when set direction is updated to negative normalized(direction)
	void setDirection(v3f dir);
	v3f getDirection() const{
		return direction;
	};
	v3f getPosition() const;

	/// Gets the light's matrices.
	const core::matrix4 &getViewMatrix() const;
	const core::matrix4 &getProjectionMatrix() const;
	core::matrix4 getViewProjMatrix();

	/// Gets the light's far value.
	f32 getMaxFarValue() const
	{
		return farPlane;
	}


	/// Gets the light's color.
	const video::SColorf &getLightColor() const
	{
		return diffuseColor;
	}

	/// Sets the light's color.
	void setLightColor(const video::SColorf &lightColor)
	{
		diffuseColor = lightColor;
	}

	/// Gets the shadow map resolution for this light.
	u32 getMapResolution() const
	{
		return mapRes;
	}

	bool should_update_map_shadow{true};

private:
	void createSplitMatrices(const Camera *cam);

	video::SColorf diffuseColor;

	f32 farPlane;
	u32 mapRes;

	v3f pos;
	v3f direction{0};
	shadowFrustum shadow_frustum;
};
