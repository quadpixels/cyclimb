#version 330 core
layout (location = 0) in vec4 vertex;
out vec2 TexCoords;

uniform vec2 screensize;
uniform mat4 transform;
uniform mat4 projection;

void main()
{
	const float fovy = 30.0f * 3.14159f / 180.0f, Z = 10.0f;
	const float half_yext = Z * tan(fovy);
	float half_xext = half_yext / screensize.y * screensize.x;

	vec2 xy = vertex.xy / screensize;
	xy.y = 1.0 - xy.y;
	xy = xy * 2.0f - vec2(1.0f, 1.0f);
	xy = xy * vec2(half_xext, half_yext);
    gl_Position = projection * transform * vec4(xy, -Z, 1.0);
    TexCoords = vertex.zw;
}  
