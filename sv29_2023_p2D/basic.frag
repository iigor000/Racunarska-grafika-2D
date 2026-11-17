#version 330 core

in vec4 chCol;
in vec2 chTex;
out vec4 outCol;

uniform sampler2D uTex0; // osnovna tekstura
uniform sampler2D uTex1; // prvi sloj (covek)
uniform sampler2D uTex2; // drugi sloj (pojas)

uniform bool uUseTexture;
uniform bool uUseTex1 = false;
uniform bool uUseTex2 = false;

// Standardno mesanje dva sloja
vec4 over(vec4 top, vec4 bottom)
{
	if (top.a == 0.0) return bottom;
	return top;
}

void main()
{
	if (uUseTexture){
		vec4 col = texture(uTex0, chTex);
		if (uUseTex1) col = over(texture(uTex1, chTex), col);
		if (uUseTex2) col = over(texture(uTex2, chTex), col);
		outCol = col;
	} else {
		outCol = chCol;
	}
}
