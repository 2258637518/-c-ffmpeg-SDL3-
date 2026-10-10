#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D tex_y;
uniform sampler2D tex_uv;

void main()
{
	float y = texture(tex_y,TexCoord).r;
	vec2 uv = texture(tex_uv, TexCoord).rg - vec2(0.5, 0.5);

    float u = uv.r;
    float v = uv.g;

	vec3 rgb;
    rgb.r = y + 1.402 * v;
    rgb.g = y - 0.344136 * u - 0.714136 * v;
    rgb.b = y + 1.772 * u;

    FragColor = vec4(rgb, 1.0);
}