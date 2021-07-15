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

#include "client/shadows/shadowsshadercallbacks.h"

void ShadowDepthShaderCB::OnSetConstants(
		video::IMaterialRendererServices *services, s32 userData)
{
	video::IVideoDriver *driver = services->getVideoDriver();

	core::matrix4 lightMVP = driver->getTransform(video::ETS_PROJECTION);
	lightMVP *= driver->getTransform(video::ETS_VIEW);
	lightMVP *= driver->getTransform(video::ETS_WORLD);

	services->setVertexShaderConstant(
		services->getPixelShaderConstantID("LightMVP"),
		lightMVP.pointer(), 16);

	services->setVertexShaderConstant(
		services->getPixelShaderConstantID("MapResolution"), &MapRes, 1);
	services->setVertexShaderConstant(
		services->getPixelShaderConstantID("MaxFar"), &MaxFar, 1);

	s32 TextureId = 0;
	services->setPixelShaderConstant(
		services->getPixelShaderConstantID("ColorMapSampler"), &TextureId,
		1);
}
