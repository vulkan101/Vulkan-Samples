/* Copyright (c) 2019-2020, Arm Limited and Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 the "License";
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "volume_render.h"

#include "common/vk_common.h"
#include "gltf_loader.h"
#include "gui.h"
#include "platform/filesystem.h"
#include "platform/platform.h"
#include "rendering/subpasses/forward_subpass.h"
#include "rendering/subpasses/lighting_subpass.h"
#include "rendering/subpasses/volume_raydir_subpass.h"
#include "stats/stats.h"

volume_render::volume_render()
{
}

bool volume_render::prepare(vkb::Platform &platform)
{
	if (!VulkanSample::prepare(platform))
	{
		return false;
	}

	// Load a scene from the assets folder
	load_scene("scenes/cube.gltf");

	// Attach a move script to the camera component in the scene
	auto &camera_node = vkb::add_free_camera(*scene, "main_camera", get_render_context().get_surface_extent());
	_camera       = &camera_node.get_component<vkb::sg::Camera>();
	camera_node.get_transform().set_translation({0.0f, 0.0f, 10.0f});
	

		
	// Example Scene Render Pipeline
	vkb::ShaderSource vert_shader("base.vert");
	vkb::ShaderSource frag_shader("basemh.frag");
	auto              scene_subpass   = std::make_unique<vkb::ForwardSubpass>(get_render_context(), std::move(vert_shader), std::move(frag_shader), *scene, *_camera);
	auto              render_pipeline = vkb::RenderPipeline();
	render_pipeline.add_subpass(std::move(scene_subpass));
	set_render_pipeline(std::move(render_pipeline));

	// Add a GUI with the stats you want to monitor
	stats->request_stats({/*stats you require*/});
	gui = std::make_unique<vkb::Gui>(*this, platform.get_window(), stats.get());

	return true;
}

std::unique_ptr<vkb::VulkanSample> create_volume_render()
{
	return std::make_unique<volume_render>();
}

std::unique_ptr<vkb::RenderPipeline> volume_render::create_renderpass()
{
	
	// draw back faces into texture
	auto back_vs   = vkb::ShaderSource{"volume/geometry.vert"};
	auto back_fs   = vkb::ShaderSource{"volume/raydir_back.frag"};
	auto back_subpass = std::make_unique<vkb::RayDirSubpass>(get_render_context(), std::move(back_vs), std::move(back_fs), *scene, *_camera, vkb::FaceDirection::Back);
	
	
	back_subpass->set_output_attachments({1, 2, 3});
	// draw front faces 
	auto front_vs      = vkb::ShaderSource{"volume/geometry.vert"};
	auto front_fs      = vkb::ShaderSource{"volume/raydir_front.frag"};
	auto front_subpass = std::make_unique<vkb::RayDirSubpass>(get_render_context(), std::move(back_vs), std::move(back_fs), *scene, *_camera, vkb::FaceDirection::Front);
	front_subpass->set_ray_direction(vkb::RayDirection::Backward);
	front_subpass->set_input_attachments({1, 2, 3});
	front_subpass->set_output_attachments({1, 2, 3});
	
	// Outputs are depth, albedo, and normal
	

	// Lighting subpass
	auto lighting_vs      = vkb::ShaderSource{"deferred/lighting.vert"};
	auto lighting_fs      = vkb::ShaderSource{"deferred/lighting.frag"};
	auto lighting_subpass = std::make_unique<vkb::LightingSubpass>(get_render_context(), std::move(lighting_vs), std::move(lighting_fs), *_camera, *scene);

	// Inputs are depth, albedo, and normal from the geometry subpass
	lighting_subpass->set_input_attachments({1, 2, 3});

	// Create subpasses pipeline
	std::vector<std::unique_ptr<vkb::Subpass>> subpasses{};
	subpasses.push_back(std::move(back_subpass));
	subpasses.push_back(std::move(front_subpass));

	auto render_pipeline = std::make_unique<vkb::RenderPipeline>(std::move(subpasses));

	render_pipeline->set_load_store(vkb::gbuffer::get_clear_all_store_swapchain());

	render_pipeline->set_clear_value(vkb::gbuffer::get_clear_value());

	return render_pipeline;
	
}