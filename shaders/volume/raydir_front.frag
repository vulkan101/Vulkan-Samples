#version 320 es
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

precision highp float;

#ifdef HAS_BASE_COLOR_TEXTURE
layout (set=0, binding=0) uniform sampler2D base_color_texture;
#endif

layout (location = 0) in vec4 in_pos;
layout (location = 1) in vec2 in_uv;
layout (location = 2) in vec3 in_normal;


layout (location = 0) out vec4 o_col; // swapchain

layout (location = 3) out vec4 o_front_pos;
layout (location = 4) out vec4 o_direction;

precision highp float;

layout(input_attachment_index = 0, binding = 0) uniform highp subpassInput i_backpos;

layout(set = 0, binding = 1) uniform GlobalUniform {
    mat4 model;
    mat4 view_proj;
    vec3 camera_position;
} global_uniform;


void main(void)
{
    vec4 back_pos = subpassLoad(i_backpos);
        
// output 
    o_front_pos = in_pos;  
    
    o_direction.xyz = normalize(back_pos.xyz - in_pos.xyz);
    o_direction.w = 1.0;
    o_col = 0.5 * o_direction + 0.5;
    
}
