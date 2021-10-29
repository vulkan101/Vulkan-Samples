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
#include <iostream>
#include "scene_graph/components/texture.h"
#include "scene_graph/components/image.h"

using namespace vkb;

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
	create_texture3D();
	upload_images();
	// Attach a move script to the camera component in the scene
	auto &camera_node = vkb::add_free_camera(*scene, "main_camera", get_render_context().get_surface_extent());
	_camera       = &camera_node.get_component<vkb::sg::Camera>();
	camera_node.get_transform().set_translation({0.0f, 0.0f, 20.0f});
	
				
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
	auto front_subpass = std::make_unique<vkb::RayDirSubpass>(get_render_context(), std::move(front_vs), std::move(front_fs), *scene, *_camera, vkb::FaceDirection::Front);	
	front_subpass->set_input_attachments({3});
	front_subpass->set_output_attachments({0, 1, 2, 3, 4});
	
		
	// Lighting subpass
	//auto lighting_vs      = vkb::ShaderSource{"volume/lighting.vert"};
	//auto lighting_fs      = vkb::ShaderSource{"volume/lighting.frag"};
	//auto lighting_subpass = std::make_unique<vkb::LightingSubpass>(get_render_context(), std::move(lighting_vs), std::move(lighting_fs), *_camera, *scene);

	//// Inputs are depth, albedo, and normal from the geometry subpass
	//lighting_subpass->set_input_attachments({0, 1, 2, 3, 4});

	// Create subpasses pipeline
	std::vector<std::unique_ptr<vkb::Subpass>> subpasses{};
	subpasses.push_back(std::move(back_subpass));
	subpasses.push_back(std::move(front_subpass));
	//subpasses.push_back(std::move(lighting_subpass));

	auto pipeline = std::make_unique<vkb::RenderPipeline>(std::move(subpasses));

	pipeline->set_load_store(vkb::gbuffer::get_clear_all_store_swapchain());

	pipeline->set_clear_value(vkb::gbuffer::get_clear_value());

	return pipeline;
	
}

void volume_render::draw_pipeline(vkb::CommandBuffer &command_buffer, vkb::RenderTarget &render_target, vkb::RenderPipeline &render_pipeline, vkb::Gui *gui)
{
	auto &extent = render_target.get_extent();

	VkViewport viewport{};
	viewport.width    = static_cast<float>(extent.width);
	viewport.height   = static_cast<float>(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	command_buffer.set_viewport(0, {viewport});

	VkRect2D scissor{};
	scissor.extent = extent;
	command_buffer.set_scissor(0, {scissor});

	render_pipeline.draw(command_buffer, render_target);

	/*if (gui)
	{
		gui->draw(command_buffer);
	}*/

	command_buffer.end_render_pass();
}

void volume_render::draw_renderpass(vkb::CommandBuffer &command_buffer, vkb::RenderTarget &render_target)
{
	draw_pipeline(command_buffer, render_target, *render_pipeline, gui.get());
}

std::unique_ptr<sg::Sampler> volume_render::create_sampler3D(const std::string& name)
{
	auto &   device     = get_render_context().get_device();

	VkFilter min_filter = VK_FILTER_LINEAR;	
	VkFilter mag_filter = VK_FILTER_LINEAR;

	VkSamplerMipmapMode mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

	VkSamplerAddressMode address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;	
	VkSamplerAddressMode address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;	
	VkSamplerAddressMode address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;	

	VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};

	sampler_info.magFilter    = mag_filter;
	sampler_info.minFilter    = min_filter;
	sampler_info.mipmapMode   = mipmap_mode;
	sampler_info.addressModeU = address_mode_u;
	sampler_info.addressModeV = address_mode_v;
	sampler_info.addressModeW = address_mode_w;
	sampler_info.borderColor  = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	sampler_info.maxLod       = std::numeric_limits<float>::max();

	core::Sampler vk_sampler{device, sampler_info};

	return std::make_unique<sg::Sampler>(name, std::move(vk_sampler));
}

void volume_render::create_texture3D()
{
	// create the data 
	uint32_t const r = 64;
	uint32_t const w = r;
	uint32_t const h = r;
	uint32_t const d = r;
	std::vector<uint8_t> data;
	data.reserve(w * h * d);
	
	float v = 0.0f;
	for (int z = 0; z < d; z++)
	{
		for (int y = 0; y < h; y++)
		{
			for (int x = 0; x < w; x++)
			{				
				if (z == d / 2 || x == d/2 || y == d/2)
					v = 1.0f;
				else
					 v = 0.01f;				
				data.push_back(static_cast<uint8_t>(floor(v * 255)));
			}
		}
	}
	auto &device = get_render_context().get_device();
	VkExtent3D extent = {w, h, d};

	// G-Buffer should fit 128-bit budget for buffer color storage
	// in order to enable subpasses merging by the driver
	// Light (swapchain_image) RGBA8_UNORM   (32-bit)
	// Albedo                  RGBA8_UNORM   (32-bit)
	// Normal                  RGB10A2_UNORM (32-bit)

	std::unique_ptr<sg::Texture> tex3d = std::make_unique<sg::Texture>(std::string("MyTex3D"));
	// sgimage is high level
	std::unique_ptr<sg::Image> image{nullptr};
	std::unique_ptr<sg::Sampler> sampler3d = this->create_sampler3D("MySampled3D");
	auto mipmap = sg::Mipmap{
		    /* .level = */ 0,
		    /* .offset = */ 0,
		    /* .extent = */ {/* .width = */ static_cast<uint32_t>(w),
		                     /* .height = */ static_cast<uint32_t>(d),
		                     /* .depth = */ static_cast<uint32_t>(h)}};
		std::vector<sg::Mipmap> mipmaps{mipmap};
	image = std::make_unique<sg::Image>("My3DImage", std::move(data), std::move(mipmaps));

	if (sg::is_astc(image->get_format()))
	{
		if (!device.is_image_format_supported(image->get_format(), VK_IMAGE_TYPE_3D))
		{
			LOGW("Image format not supported: {}", image->get_name());			
		}
	}
	// set up a sampler - see GLTFLoader::parse_sampler
	// create core::Image
	image->create_vk_image(device, VK_IMAGE_VIEW_TYPE_3D);
	tex3d->set_image(*image);
	tex3d->set_sampler(*sampler3d);

	scene->add_component(std::move(image));
	scene->add_component(std::move(sampler3d));
	scene->add_component(std::move(tex3d));					
}

inline void upload_image_to_gpu(CommandBuffer &command_buffer, core::Buffer &staging_buffer, sg::Image &image)
{
	// Clean up the image data, as they are copied in the staging buffer
	image.clear_data();

	{
		ImageMemoryBarrier memory_barrier{};
		memory_barrier.old_layout      = VK_IMAGE_LAYOUT_UNDEFINED;
		memory_barrier.new_layout      = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		memory_barrier.src_access_mask = 0;
		memory_barrier.dst_access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
		memory_barrier.src_stage_mask  = VK_PIPELINE_STAGE_HOST_BIT;
		memory_barrier.dst_stage_mask  = VK_PIPELINE_STAGE_TRANSFER_BIT;

		command_buffer.image_memory_barrier(image.get_vk_image_view(), memory_barrier);
	}

	// Create a buffer image copy for every mip level
	auto &mipmaps = image.get_mipmaps();

	std::vector<VkBufferImageCopy> buffer_copy_regions(mipmaps.size());

	for (size_t i = 0; i < mipmaps.size(); ++i)
	{
		auto &mipmap      = mipmaps[i];
		auto &copy_region = buffer_copy_regions[i];

		copy_region.bufferOffset     = mipmap.offset;
		copy_region.imageSubresource = image.get_vk_image_view().get_subresource_layers();
		// Update miplevel
		copy_region.imageSubresource.mipLevel = mipmap.level;
		copy_region.imageExtent               = mipmap.extent;
	}

	command_buffer.copy_buffer_to_image(staging_buffer, image.get_vk_image(), buffer_copy_regions);

	{
		ImageMemoryBarrier memory_barrier{};
		memory_barrier.old_layout      = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		memory_barrier.new_layout      = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		memory_barrier.src_access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
		memory_barrier.dst_access_mask = VK_ACCESS_SHADER_READ_BIT;
		memory_barrier.src_stage_mask  = VK_PIPELINE_STAGE_TRANSFER_BIT;
		memory_barrier.dst_stage_mask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

		command_buffer.image_memory_barrier(image.get_vk_image_view(), memory_barrier);
	}
}

void volume_render::upload_images()
{
	auto &device           = get_render_context().get_device();
	auto image_components = scene->get_components<sg::Image>();
	auto &command_buffer   = device.request_command_buffer();
	std::vector<core::Buffer> transient_buffers;

	command_buffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, 0);
	auto image_count = image_components.size();
	for (size_t image_index = 0; image_index < image_count; image_index++)
	{
		auto &image = image_components.at(image_index);

		core::Buffer stage_buffer{device,
		                          image->get_data().size(),
		                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		                          VMA_MEMORY_USAGE_CPU_ONLY};

		stage_buffer.update(image->get_data());

		upload_image_to_gpu(command_buffer, stage_buffer, *image);

		transient_buffers.push_back(std::move(stage_buffer));
	}

	command_buffer.end();

	auto &queue = device.get_queue_by_flags(VK_QUEUE_GRAPHICS_BIT, 0);

	queue.submit(command_buffer, device.request_fence());

	device.get_fence_pool().wait();
	device.get_fence_pool().reset();
	device.get_command_pool().reset_pool();
	device.wait_idle();

	transient_buffers.clear();
}


