#version 330 core

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inCol;
layout(location = 2) in vec2 inTex;
out vec4 chCol;
out vec2 chTex;

// Transformacija objekta: skaliranje -> rotacija -> pomeraj
uniform vec2 uOffset = vec2(0.0, 0.0);
uniform float uRotation = 0.0;
uniform vec2 uScale = vec2(1.0, 1.0);

void main()
{
	vec2 p = inPos * uScale;

	float c = cos(uRotation);
	float s = sin(uRotation);
	p = vec2(p.x * c - p.y * s,
	         p.x * s + p.y * c);

	gl_Position = vec4(p + uOffset, 0.0, 1.0);
	chCol = inCol;
	chTex = inTex;
}
