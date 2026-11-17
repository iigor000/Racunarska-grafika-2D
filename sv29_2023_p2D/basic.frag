#version 330 core

in vec4 chCol;
in vec2 chTex;
out vec4 outCol;

uniform sampler2D uTex0; // osnovna tekstura (vagon, trava, oblaci, ...)
uniform sampler2D uTex1; // prvi sloj preko osnovne (putnik)
uniform sampler2D uTex2; // drugi sloj preko prvog (pojas)

uniform bool uUseTexture;
uniform bool uUseTex1 = false;
uniform bool uUseTex2 = false;

void main()
{
	// mozemo ovako definisati boju (bez mnozenja) jer znamo da su sve 
	// gornje teksture 100% ili providne ili neprovidne
	if (uUseTexture){
		vec4 col = texture(uTex0, chTex);
		if (uUseTex1) col = texture(uTex1, chTex);
		if (uUseTex2) col = texture(uTex2, chTex);
		outCol = col;
	} else {
		outCol = chCol;
	}
}
