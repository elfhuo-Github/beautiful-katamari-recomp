#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;

layout(location = 0) out vec4 v_color;

layout(set = 0, binding = 0) uniform Uniforms {
    mat4 view_proj;
} uniforms;

void main() {
    gl_Position = uniforms.view_proj * vec4(in_position, 1.0);
    v_color = in_color;
}
