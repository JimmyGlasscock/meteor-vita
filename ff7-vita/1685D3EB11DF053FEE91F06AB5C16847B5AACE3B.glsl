#extension GL_OES_EGL_image_external:require
//
//  Shader.fsh
//  iFF7
//

varying mediump vec4 colorVarying;
varying mediump vec2 v_texCoord;

uniform sampler2D s_texture;
uniform samplerExternalOES s_texture_OES;
uniform bool enableOES;
uniform bool enableTex;
uniform bool enableAlphaTest;


//modified 20151008 for Nexus 6

void main()
{
	if(enableTex) {
		mediump vec4 val;
		if(enableOES) {
			val = texture2D(s_texture_OES, v_texCoord);
		} else {
			val = texture2D(s_texture, v_texCoord);
		}
		if(enableAlphaTest) {
			if(val.a <= 0.0) {
				discard;
				//return;//necessary or not?
			}
		}
		gl_FragColor = val * colorVarying;
	} else {
		gl_FragColor = colorVarying;
	}
}
