$input v_normal

#include <bgfx_shader.sh>

uniform vec4 u_color;

void main()
{
    // Normalize interpolated normal
    vec3 N = normalize(v_normal);

    // Fixed directional light from upper-right-front (in world space, Z-up)
    vec3 lightDir = normalize(vec3(0.3, -0.5, 0.8));

    // Diffuse: half-lambert for softer shading (no harsh dark side)
    float NdotL = dot(N, lightDir);
    float diffuse = NdotL * 0.5 + 0.5;  // remap [-1,1] to [0,1]

    // Ambient + diffuse
    float ambient = 0.15;
    float lighting = ambient + (1.0 - ambient) * diffuse;

    gl_FragColor = vec4(u_color.rgb * lighting, u_color.a);
}
