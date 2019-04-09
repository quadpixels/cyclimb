#version 330 core

layout(location = 0) out vec4 color;

uniform sampler2D tex;

in vec2 UV;

void main(){
	color = texture(tex, UV);
	//color = vec4(UV, 0.0f, 1.0f);
}