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
	
				
	render_pipeline = create_renderpass();

	// Add a GUI with the stats you want to monitor
	stats->request_stats({/*stats you require*/});
	gui = std::make_unique<vkb::Gui>(*this, platform.get_window(), stats.get());

	return true;
}

std::unique_ptr<vkb::VulkanSample> create_volume_render()
{
	return std::make_unique<volume_render>();
}

void volume_render::prepare_render_context()
{
	get_render_context().prepare(1, [this](vkb::core::Image &&swapchain_image) { return create_render_target(std::move(swapchain_image)); });
}

std::unique_ptr<vkb::RenderTarget> volume_render::create_render_target(vkb::core::Image &&swapchain_image)
{
	auto &device = swapchain_image.get_device();
	auto &extent = swapchain_image.get_extent();

	// G-Buffer should fit 128-bit budget for buffer color storage
	// in order to enable subpasses merging by the driver
	// Light (swapchain_image) RGBA8_UNORM   (32-bit)
	// Albedo                  RGBA8_UNORM   (32-bit)
	// Normal                  RGB10A2_UNORM (32-bit)

	vkb::core::Image depth_image{device,
	                             extent,
	                             vkb::get_suitable_depth_format(swapchain_image.get_device().get_gpu().get_handle()),
	                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | rt_usage_flags,
	                             VMA_MEMORY_USAGE_GPU_ONLY};

	vkb::core::Image albedo_image{device,
	                              extent,
	                              albedo_format,
	                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | rt_usage_flags,
	                              VMA_MEMORY_USAGE_GPU_ONLY};

	vkb::core::Image position_image{device,
	                              extent,
	                              position_format,
	                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | rt_usage_flags,
	                              VMA_MEMORY_USAGE_GPU_ONLY};

	vkb::core::Image direction_image{device,
	                                extent,
	                                direction_format,
	                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | rt_usage_flags,
	                                VMA_MEMORY_USAGE_GPU_ONLY};

	std::vector<vkb::core::Image> images;

	// Attachment 0
	images.push_back(std::move(swapchain_image));

	// Attachment 1
	images.push_back(std::move(depth_image));

	// Attachment 2
	images.push_back(std::move(albedo_image));

	// Attachment 3
	images.push_back(std::move(position_image));

	// Attachment 4
	images.push_back(std::move(direction_image));

	return std::make_unique<vkb::RenderTarget>(std::move(images));
}

std::unique_ptr<vkb::RenderPipeline> volume_render::create_renderpass()
{
	
	// draw back faces into texture
	auto back_vs   = vkb::ShaderSource{"volume/geometry.vert"};
	auto back_fs   = vkb::ShaderSource{"volume/raydir_back.frag"};
	auto back_subpass = std::make_unique<vkb::RayDirSubpass>(get_render_context(), std::move(back_vs), std::move(back_fs), *scene, *_camera, vkb::FaceDirection::Back);
	
	
	back_subpass->set_output_attachments({3});
	// draw front faces 
	auto front_vs      = vkb::ShaderSource{"volume/geometry.vert"};
	auto front_fs      = vkb::ShaderSource{"volume/raydir_front.frag"};
	auto front_subpass = std::make_unique<vkb::RayDirSubpass>(get_render_context(), std::move(back_vs), std::move(back_fs), *scene, *_camera, vkb::FaceDirection::Front);	
	front_subpass->set_input_attachments({3});
	front_subpass->set_output_attachments({1, 2, 3, 4});
	
	// Outputs are depth, albedo, and normal
	

	// Lighting subpass
	auto lighting_vs      = vkb::ShaderSource{"deferred/lighting.vert"};
	auto lighting_fs      = vkb::ShaderSource{"deferred/lighting.frag"};
	auto lighting_subpass = std::make_unique<vkb::LightingSubpass>(get_render_context(), std::move(lighting_vs), std::move(lighting_fs), *_camera, *scene);

	// Inputs are depth, albedo, and normal from the geometry subpass
	lighting_subpass->set_input_attachments({1, 2, 3, 4});

	// Create subpasses pipeline
	std::vector<std::unique_ptr<vkb::Subpass>> subpasses{};
	subpasses.push_back(std::move(back_subpass));
	subpasses.push_back(std::move(front_subpass));
	
	auto pipeline = std::make_unique<vkb::RenderPipeline>(std::move(subpasses));

	pipeline->set_load_store(vkb::gbuffer::get_clear_all_store_swapchain());

	pipeline->set_clear_value(vkb::gbuffer::get_clear_value());

	return pipeline;
	
}