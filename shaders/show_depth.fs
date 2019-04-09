#version 330 core

layout(location = 0) out vec4 color;

uniform sampler2D tex;

in vec2 UV;

void main(){
	vec4 d = texture(tex, UV);
	color = vec4(d.x, d.x, d.x, 1.0f);
}