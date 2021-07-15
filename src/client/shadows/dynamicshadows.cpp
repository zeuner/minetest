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

#include <cmath>

#include "client/shadows/dynamicshadows.h"
#include "client/client.h"
#include "client/clientenvironment.h"
#include "client/clientmap.h"
#include "client/camera.h"

using m4f = core::matrix4;

void DirectionalLight::createSplitMatrices(const Camera *cam)
{
	float radius;
	v3f newCenter;
	v3f look = cam->getDirection();

	// camera view tangents
	float tanFovY = tanf(cam->getFovY() * 0.5f);
	float tanFovX = tanf(cam->getFovX() * 0.5f);

	// adjusted frustum boundaries
	float sfNear = shadow_frustum.zNear;
	float sfFar = adjustDist(shadow_frustum.zFar, cam->getFovY());

	// adjusted camera positions
	v3f camPos2 = cam->getPosition();
	v3f camPos = v3f(camPos2.X - cam->getOffset().X * BS,
			camPos2.Y - cam->getOffset().Y * BS,
			camPos2.Z - cam->getOffset().Z * BS);
	camPos += look * sfNear;
	camPos2 += look * sfNear;

	// center point of light frustum
	float end = sfNear + sfFar;
	newCenter = camPos + look * (sfNear + 0.05f * end);
	v3f world_center = camPos2 + look * (sfNear + 0.05f * end);

	// Create a vector to the frustum far corner
	const v3f &viewUp = cam->getCameraNode()->getUpVector();
	v3f viewRight = look.crossProduct(viewUp);

	v3f farCorner = look + viewRight * tanFovX + viewUp * tanFovY;
	// Compute the frustumBoundingSphere radius
	v3f boundVec = (camPos + farCorner * sfFar) - newCenter;
	radius = boundVec.getLength() * 2.0f;
	// boundVec.getLength();
	float vvolume = radius * 2.0f;

	float texelsPerUnit = getMapResolution() / vvolume;
	m4f mTexelScaling;
	mTexelScaling.setScale(texelsPerUnit);

	m4f mLookAt, mLookAtInv;

	mLookAt.buildCameraLookAtMatrixLH(v3f(0.0f, 0.0f, 0.0f), -direction, v3f(0.0f, 1.0f, 0.0f));

	mLookAt *= mTexelScaling;
	mLookAtInv = mLookAt;
	mLookAtInv.makeInverse();

	v3f frustumCenter = newCenter;
	mLookAt.transformVect(frustumCenter);
	frustumCenter.X = floorf(frustumCenter.X); // clamp to texel increment
	frustumCenter.Y = floorf(frustumCenter.Y); // clamp to texel increment
	frustumCenter.Z = floorf(frustumCenter.Z);
	mLookAtInv.transformVect(frustumCenter);
	// probar radius multipliacdor en funcion del I, a menor I mas multiplicador
	v3f eye_displacement = direction * vvolume;

	// we must compute the viewmat with the position - the camera offset
	// but the shadow_frustum position must be the actual world position
	v3f eye = frustumCenter - eye_displacement;
	shadow_frustum.position = world_center - eye_displacement;
	shadow_frustum.length = vvolume;
	shadow_frustum.ViewMat.buildCameraLookAtMatrixLH(eye, frustumCenter, v3f(0.0f, 1.0f, 0.0f));
	shadow_frustum.ProjOrthMat.buildProjectionMatrixOrthoLH(shadow_frustum.length,
			shadow_frustum.length, -shadow_frustum.length,
			shadow_frustum.length,false);
}

DirectionalLight::DirectionalLight(const u32 shadowMapResolution,
		const v3f &position, video::SColorf lightColor,
		f32 farValue) :
		diffuseColor(lightColor),
		farPlane(farValue), mapRes(shadowMapResolution), pos(position)
{}

void DirectionalLight::update_frustum(const Camera *cam, Client *client)
{
	should_update_map_shadow = true;
	float zNear = cam->getCameraNode()->getNearValue();
	float zFar = getMaxFarValue();

	///////////////////////////////////
	// update splits near and fars
	shadow_frustum.zNear = zNear;
	shadow_frustum.zFar = zFar;

	// update shadow frustum
	createSplitMatrices(cam);
	// get the draw list for shadows
	client->getEnv().getClientMap().updateDrawListShadow(
			getPosition(), getDirection(), shadow_frustum.length);
	should_update_map_shadow = true;
}

void DirectionalLight::setDirection(v3f dir)
{
	direction = -dir;
	direction.normalize();
}

v3f DirectionalLight::getPosition() const
{
	return shadow_frustum.position;
}

const m4f &DirectionalLight::getViewMatrix() const
{
	return shadow_frustum.ViewMat;
}

const m4f &DirectionalLight::getProjectionMatrix() const
{
	return shadow_frustum.ProjOrthMat;
}

m4f DirectionalLight::getViewProjMatrix()
{
	return shadow_frustum.ProjOrthMat * shadow_frustum.ViewMat;
}
