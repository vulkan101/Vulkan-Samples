/* Copyright (c) 2019, Arm Limited and Contributors
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

#pragma once

#include "rendering/render_pipeline.h"
#include "scene_graph/components/camera.h"
#include "vulkan_sample.h"

class volume_render : public vkb::VulkanSample
{
  public:
	volume_render();

	virtual bool prepare(vkb::Platform &platform) override;
	
	virtual ~volume_render() = default;
	virtual void prepare_render_context() override;
  private:
	vkb::sg::Camera* _camera;
	std::unique_ptr<vkb::RenderPipeline> create_renderpass();
	std::unique_ptr<vkb::RenderTarget>   create_render_target(vkb::core::Image &&swapchain_image);
	
	VkFormat          albedo_format{VK_FORMAT_R8G8B8A8_UNORM};
	VkFormat          position_format{VK_FORMAT_A2B10G10R10_UNORM_PACK32};
	VkFormat          direction_format{VK_FORMAT_A2B10G10R10_UNORM_PACK32};
	VkImageUsageFlags rt_usage_flags{VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT};
};

std::unique_ptr<vkb::VulkanSample> create_volume_render();
