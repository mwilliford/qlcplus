$input a_position, a_normal
$output v_normal

#include <bgfx_shader.sh>

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));

    // Transform normal to world space (using upper-left 3x3 of model matrix)
    v_normal = mul(u_model[0], vec4(a_normal, 0.0)).xyz;
}
