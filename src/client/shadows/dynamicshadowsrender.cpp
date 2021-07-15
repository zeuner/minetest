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

#include <cstring>
#include "client/shadows/dynamicshadowsrender.h"
#include "client/shadows/shadowsScreenQuad.h"
#include "client/shadows/shadowsshadercallbacks.h"
#include "settings.h"
#include "filesys.h"
#include "util/string.h"
#include "client/shader.h"
#include "client/client.h"
#include "client/clientmap.h"

ShadowRenderer::ShadowRenderer(IrrlichtDevice *device, Client *client) :
		m_device(device), m_smgr(device->getSceneManager()),
		m_driver(device->getVideoDriver()), m_client(client)
{
	m_shadows_enabled = true;

	m_shadow_strength = g_settings->getFloat("shadow_strength");

	m_shadow_map_max_distance = g_settings->getFloat("shadow_map_max_distance");

	m_shadow_map_texture_size = g_settings->getFloat("shadow_map_texture_size");

	m_shadow_map_texture_32bit = g_settings->getBool("shadow_map_texture_32bit");
	m_shadow_map_colored = g_settings->getBool("shadow_map_color");
	m_shadow_samples = g_settings->getS32("shadow_filters");
	m_update_delta = g_settings->getFloat("shadow_update_time");
}

ShadowRenderer::~ShadowRenderer()
{
	if (m_shadow_depth_cb)
		delete m_shadow_depth_cb;
	if (m_shadow_mix_cb)
		delete m_shadow_mix_cb;
	m_shadow_node_array.clear();
	m_light_list.clear();

	if (shadowMapTextureDynamicObjects)
		m_driver->removeTexture(shadowMapTextureDynamicObjects);

	if (shadowMapTextureFinal)
		m_driver->removeTexture(shadowMapTextureFinal);

	if (shadowMapTextureColors)
		m_driver->removeTexture(shadowMapTextureColors);

	if (shadowMapClientMap)
		m_driver->removeTexture(shadowMapClientMap);
}

void ShadowRenderer::initialize()
{
	auto *gpu = m_driver->getGPUProgrammingServices();

	// we need glsl
	if (m_shadows_enabled && gpu && m_driver->queryFeature(video::EVDF_ARB_GLSL)) {
		createShaders();
	} else {
		m_shadows_enabled = false;

		warningstream << "Shadows: GLSL Shader not supported on this system."
			<< std::endl;
		return;
	}

	m_texture_format = m_shadow_map_texture_32bit
					   ? video::ECOLOR_FORMAT::ECF_R32F
					   : video::ECOLOR_FORMAT::ECF_R16F;

	m_texture_format_color = m_shadow_map_texture_32bit
						 ? video::ECOLOR_FORMAT::ECF_G32R32F
						 : video::ECOLOR_FORMAT::ECF_G16R16F;
}


float ShadowRenderer::getUpdateDelta() const
{
	return m_update_delta;
}

size_t ShadowRenderer::addDirectionalLight()
{
	m_light_list.emplace_back(m_shadow_map_texture_size,
			v3f(0.f, 0.f, 0.f),
			video::SColor(255, 255, 255, 255), m_shadow_map_max_distance);
	return m_light_list.size() - 1;
}

DirectionalLight &ShadowRenderer::getDirectionalLight(u32 index)
{
	return m_light_list[index];
}

size_t ShadowRenderer::getDirectionalLightCount() const
{
	return m_light_list.size();
}

f32 ShadowRenderer::getMaxShadowFar() const
{
	if (!m_light_list.empty()) {
		float wanted_range = m_client->getEnv().getClientMap().getWantedRange();

		float zMax = m_light_list[0].getMaxFarValue() > wanted_range
					     ? wanted_range
					     : m_light_list[0].getMaxFarValue();
		return zMax * MAP_BLOCKSIZE;
	}
	return 0.0f;
}

void ShadowRenderer::addNodeToShadowList(
		scene::ISceneNode *node, E_SHADOW_MODE shadowMode)
{
	m_shadow_node_array.emplace_back(NodeToApply(node, shadowMode));
}

void ShadowRenderer::removeNodeFromShadowList(scene::ISceneNode *node)
{
	for (auto it = m_shadow_node_array.begin(); it != m_shadow_node_array.end();) {
		if (it->node == node) {
			it = m_shadow_node_array.erase(it);
			break;
		} else {
			++it;
		}
	}
}

void ShadowRenderer::setClearColor(video::SColor ClearColor)
{
	m_clear_color = ClearColor;
}

void ShadowRenderer::update(video::ITexture *outputTarget)
{
	if (!m_shadows_enabled || m_smgr->getActiveCamera() == nullptr) {
		m_smgr->drawAll();
		return;
	}

	if (!shadowMapTextureDynamicObjects) {

		shadowMapTextureDynamicObjects = getSMTexture(
			std::string("shadow_dynamic_") + itos(m_shadow_map_texture_size),
			m_texture_format, true);
	}

	if (!shadowMapClientMap) {

		shadowMapClientMap = getSMTexture(
			std::string("shadow_clientmap_") + itos(m_shadow_map_texture_size),
			m_shadow_map_colored ? m_texture_format_color : m_texture_format,
			true);
	}

	if (m_shadow_map_colored && !shadowMapTextureColors) {
		shadowMapTextureColors = getSMTexture(
			std::string("shadow_colored_") + itos(m_shadow_map_texture_size),
			m_shadow_map_colored ? m_texture_format_color : m_texture_format,
			true);
	}

	// The merge all shadowmaps texture
	if (!shadowMapTextureFinal) {
		video::ECOLOR_FORMAT frt;
		if (m_shadow_map_texture_32bit) {
			if (m_shadow_map_colored)
				frt = video::ECOLOR_FORMAT::ECF_A32B32G32R32F;
			else
				frt = video::ECOLOR_FORMAT::ECF_R32F;
		} else {
			if (m_shadow_map_colored)
				frt = video::ECOLOR_FORMAT::ECF_A16B16G16R16F;
			else
				frt = video::ECOLOR_FORMAT::ECF_R16F;
		}
		shadowMapTextureFinal = getSMTexture(
			std::string("shadowmap_final_") + itos(m_shadow_map_texture_size),
			frt, true);
	}

	if (!m_shadow_node_array.empty() && !m_light_list.empty()) {
		// for every directional light:
		for (DirectionalLight &light : m_light_list) {
			// Static shader values.
			m_shadow_depth_cb->MapRes = (f32)m_shadow_map_texture_size;
			m_shadow_depth_cb->MaxFar = (f32)m_shadow_map_max_distance * BS;

			// set the Render Target
			// right now we can only render in usual RTT, not
			// Depth texture is available in irrlicth maybe we
			// should put some gl* fn here

			if (light.should_update_map_shadow) {
				light.should_update_map_shadow = false;

				m_driver->setRenderTarget(shadowMapClientMap, true, true,
						video::SColor(255, 255, 255, 255));
				renderShadowMap(shadowMapClientMap, light);

				if (m_shadow_map_colored) {
					m_driver->setRenderTarget(shadowMapTextureColors,
							true, false, video::SColor(255, 255, 255, 255));
				}
				renderShadowMap(shadowMapTextureColors, light,
						scene::ESNRP_TRANSPARENT);
				m_driver->setRenderTarget(0, false, false);
			}

			// render shadows for the n0n-map objects.
			m_driver->setRenderTarget(shadowMapTextureDynamicObjects, true,
					true, video::SColor(255, 255, 255, 255));
			renderShadowObjects(shadowMapTextureDynamicObjects, light);
			// clear the Render Target
			m_driver->setRenderTarget(0, false, false);

			// in order to avoid too many map shadow renders,
			// we should make a second pass to mix clientmap shadows and
			// entities shadows :(
			m_screen_quad->getMaterial().setTexture(0, shadowMapClientMap);
			// dynamic objs shadow texture.
			if (m_shadow_map_colored)
				m_screen_quad->getMaterial().setTexture(1, shadowMapTextureColors);
			m_screen_quad->getMaterial().setTexture(2, shadowMapTextureDynamicObjects);

			m_driver->setRenderTarget(shadowMapTextureFinal, false, false,
					video::SColor(255, 255, 255, 255));
			m_screen_quad->render(m_driver);
			m_driver->setRenderTarget(0, false, false);

		} // end for lights

		// now render the actual MT render pass
		m_driver->setRenderTarget(outputTarget, true, true, m_clear_color);
		m_smgr->drawAll();

		/* this code just shows shadows textures in screen and in ONLY for debugging*/
		#if 0
		// this is debug, ignore for now.
		m_driver->draw2DImage(shadowMapTextureFinal,
				core::rect<s32>(0, 50, 128, 128 + 50),
				core::rect<s32>({0, 0}, shadowMapTextureFinal->getSize()));

		m_driver->draw2DImage(shadowMapClientMap,
				core::rect<s32>(0, 50 + 128, 128, 128 + 50 + 128),
				core::rect<s32>({0, 0}, shadowMapTextureFinal->getSize()));
		m_driver->draw2DImage(shadowMapTextureDynamicObjects,
				core::rect<s32>(0, 128 + 50 + 128, 128,
						128 + 50 + 128 + 128),
				core::rect<s32>({0, 0}, shadowMapTextureDynamicObjects->getSize()));

		if (m_shadow_map_colored) {

			m_driver->draw2DImage(shadowMapTextureColors,
					core::rect<s32>(128,128 + 50 + 128 + 128,
							128 + 128, 128 + 50 + 128 + 128 + 128),
					core::rect<s32>({0, 0}, shadowMapTextureColors->getSize()));
		}
		#endif
		m_driver->setRenderTarget(0, false, false);
	}
}


video::ITexture *ShadowRenderer::getSMTexture(const std::string &shadow_map_name,
		video::ECOLOR_FORMAT texture_format, bool force_creation)
{
	if (force_creation) {
		return m_driver->addRenderTargetTexture(
				core::dimension2du(m_shadow_map_texture_size,
						m_shadow_map_texture_size),
				shadow_map_name.c_str(), texture_format);
	}

	return m_driver->getTexture(shadow_map_name.c_str());
}

void ShadowRenderer::renderShadowMap(video::ITexture *target,
		DirectionalLight &light, scene::E_SCENE_NODE_RENDER_PASS pass)
{
	m_driver->setTransform(video::ETS_VIEW, light.getViewMatrix());
	m_driver->setTransform(video::ETS_PROJECTION, light.getProjectionMatrix());

	// Operate on the client map
	for (const auto &shadow_node : m_shadow_node_array) {
		if (strcmp(shadow_node.node->getName(), "ClientMap") != 0)
			continue;

		ClientMap *map_node = static_cast<ClientMap *>(shadow_node.node);

		video::SMaterial material;
		if (map_node->getMaterialCount() > 0) {
			// we only want the first material, which is the one with the albedo info
			material = map_node->getMaterial(0);
		}

		material.BackfaceCulling = false;
		material.FrontfaceCulling = true;
		material.PolygonOffsetFactor = 4.0f;
		material.PolygonOffsetDirection = video::EPO_BACK;
		//material.PolygonOffsetDepthBias = 1.0f/4.0f;
		//material.PolygonOffsetSlopeScale = -1.f;

		if (m_shadow_map_colored && pass != scene::ESNRP_SOLID)
			material.MaterialType = (video::E_MATERIAL_TYPE) depth_shader_trans;
		else
			material.MaterialType = (video::E_MATERIAL_TYPE) depth_shader;

		// FIXME: I don't think this is needed here
		map_node->OnAnimate(m_device->getTimer()->getTime());

		m_driver->setTransform(video::ETS_WORLD,
				map_node->getAbsoluteTransformation());

		map_node->renderMapShadows(m_driver, material, pass);
		break;
	}
}

void ShadowRenderer::renderShadowObjects(
		video::ITexture *target, DirectionalLight &light)
{
	m_driver->setTransform(video::ETS_VIEW, light.getViewMatrix());
	m_driver->setTransform(video::ETS_PROJECTION, light.getProjectionMatrix());

	for (const auto &shadow_node : m_shadow_node_array) {
		// we only take care of the shadow casters
		if (shadow_node.shadowMode == ESM_RECEIVE ||
				strcmp(shadow_node.node->getName(), "ClientMap") == 0)
			continue;

		// render other objects
		u32 n_node_materials = shadow_node.node->getMaterialCount();
		std::vector<s32> BufferMaterialList;
		std::vector<std::pair<bool, bool>> BufferMaterialCullingList;
		BufferMaterialList.reserve(n_node_materials);
		BufferMaterialCullingList.reserve(n_node_materials);

		// backup materialtype for each material
		// (aka shader)
		// and replace it by our "depth" shader
		for (u32 m = 0; m < n_node_materials; m++) {
			auto &current_mat = shadow_node.node->getMaterial(m);

			BufferMaterialList.push_back(current_mat.MaterialType);
			current_mat.MaterialType =
					(video::E_MATERIAL_TYPE)depth_shader;

			current_mat.setTexture(3, shadowMapTextureFinal);

			BufferMaterialCullingList.emplace_back(
				(bool)current_mat.BackfaceCulling, (bool)current_mat.FrontfaceCulling);

			current_mat.BackfaceCulling = true;
			current_mat.FrontfaceCulling = false;
			current_mat.PolygonOffsetFactor = 1.0f/2048.0f;
			current_mat.PolygonOffsetDirection = video::EPO_BACK;
			//current_mat.PolygonOffsetDepthBias = 1.0 * 2.8e-6;
			//current_mat.PolygonOffsetSlopeScale = -1.f;
		}

		m_driver->setTransform(video::ETS_WORLD,
				shadow_node.node->getAbsoluteTransformation());
		shadow_node.node->render();

		// restore the material.

		for (u32 m = 0; m < n_node_materials; m++) {
			auto &current_mat = shadow_node.node->getMaterial(m);

			current_mat.MaterialType = (video::E_MATERIAL_TYPE) BufferMaterialList[m];

			current_mat.BackfaceCulling = BufferMaterialCullingList[m].first;
			current_mat.FrontfaceCulling = BufferMaterialCullingList[m].second;
		}

	} // end for caster shadow nodes
}

void ShadowRenderer::mixShadowsQuad()
{
}

/*
 * @Liso's disclaimer ;) This function loads the Shadow Mapping Shaders.
 * I used a custom loader because I couldn't figure out how to use the base
 * Shaders system with custom IShaderConstantSetCallBack without messing up the
 * code too much. If anyone knows how to integrate this with the standard MT
 * shaders, please feel free to change it.
 */

void ShadowRenderer::createShaders()
{
	video::IGPUProgrammingServices *gpu = m_driver->getGPUProgrammingServices();

	if (depth_shader == -1) {
		std::string depth_shader_vs = getShaderPath("shadow_shaders", "pass1_vertex.glsl");
		if (depth_shader_vs.empty()) {
			m_shadows_enabled = false;
			errorstream << "Error shadow mapping vs shader not found." << std::endl;
			return;
		}
		std::string depth_shader_fs = getShaderPath("shadow_shaders", "pass1_fragment.glsl");
		if (depth_shader_fs.empty()) {
			m_shadows_enabled = false;
			errorstream << "Error shadow mapping fs shader not found." << std::endl;
			return;
		}
		m_shadow_depth_cb = new ShadowDepthShaderCB();

		depth_shader = gpu->addHighLevelShaderMaterial(
				readShaderFile(depth_shader_vs).c_str(), "vertexMain",
				video::EVST_VS_1_1,
				readShaderFile(depth_shader_fs).c_str(), "pixelMain",
				video::EPST_PS_1_2, m_shadow_depth_cb);

		if (depth_shader == -1) {
			// upsi, something went wrong loading shader.
			delete m_shadow_depth_cb;
			m_shadows_enabled = false;
			errorstream << "Error compiling shadow mapping shader." << std::endl;
			return;
		}

		// HACK, TODO: investigate this better
		// Grab the material renderer once more so minetest doesn't crash
		// on exit
		m_driver->getMaterialRenderer(depth_shader)->grab();
	}

	if (mixcsm_shader == -1) {
		std::string depth_shader_vs = getShaderPath("shadow_shaders", "pass2_vertex.glsl");
		if (depth_shader_vs.empty()) {
			m_shadows_enabled = false;
			errorstream << "Error cascade shadow mapping fs shader not found." << std::endl;
			return;
		}

		std::string depth_shader_fs = getShaderPath("shadow_shaders", "pass2_fragment.glsl");
		if (depth_shader_fs.empty()) {
			m_shadows_enabled = false;
			errorstream << "Error cascade shadow mapping fs shader not found." << std::endl;
			return;
		}
		m_shadow_mix_cb = new shadowScreenQuadCB();
		m_screen_quad = new shadowScreenQuad();
		mixcsm_shader = gpu->addHighLevelShaderMaterial(
				readShaderFile(depth_shader_vs).c_str(), "vertexMain",
				video::EVST_VS_1_1,
				readShaderFile(depth_shader_fs).c_str(), "pixelMain",
				video::EPST_PS_1_2, m_shadow_mix_cb);

		m_screen_quad->getMaterial().MaterialType =
				(video::E_MATERIAL_TYPE)mixcsm_shader;

		if (mixcsm_shader == -1) {
			// upsi, something went wrong loading shader.
			delete m_shadow_mix_cb;
			delete m_screen_quad;
			m_shadows_enabled = false;
			errorstream << "Error compiling cascade shadow mapping shader." << std::endl;
			return;
		}

		// HACK, TODO: investigate this better
		// Grab the material renderer once more so minetest doesn't crash
		// on exit
		m_driver->getMaterialRenderer(mixcsm_shader)->grab();
	}

	if (m_shadow_map_colored && depth_shader_trans == -1) {
		std::string depth_shader_vs = getShaderPath("shadow_shaders", "pass1_trans_vertex.glsl");
		if (depth_shader_vs.empty()) {
			m_shadows_enabled = false;
			errorstream << "Error shadow mapping vs shader not found." << std::endl;
			return;
		}
		std::string depth_shader_fs = getShaderPath("shadow_shaders", "pass1_trans_fragment.glsl");
		if (depth_shader_fs.empty()) {
			m_shadows_enabled = false;
			errorstream << "Error shadow mapping fs shader not found." << std::endl;
			return;
		}
		m_shadow_depth_trans_cb = new ShadowDepthShaderCB();

		depth_shader_trans = gpu->addHighLevelShaderMaterial(
				readShaderFile(depth_shader_vs).c_str(), "vertexMain",
				video::EVST_VS_1_1,
				readShaderFile(depth_shader_fs).c_str(), "pixelMain",
				video::EPST_PS_1_2, m_shadow_depth_trans_cb);

		if (depth_shader_trans == -1) {
			// upsi, something went wrong loading shader.
			delete m_shadow_depth_trans_cb;
			m_shadow_map_colored = false;
			m_shadows_enabled = false;
			errorstream << "Error compiling colored shadow mapping shader." << std::endl;
			return;
		}

		// HACK, TODO: investigate this better
		// Grab the material renderer once more so minetest doesn't crash
		// on exit
		m_driver->getMaterialRenderer(depth_shader_trans)->grab();
	}
}

std::string ShadowRenderer::readShaderFile(const std::string &path)
{
	std::string prefix;
	if (m_shadow_map_colored)
		prefix.append("#define COLORED_SHADOWS 1\n");

	std::string content;
	fs::ReadFile(path, content);

	return prefix + content;
}
