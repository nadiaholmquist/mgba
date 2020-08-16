#version 110

varying vec2 texCoord;
uniform sampler2D tex;
uniform vec4 color;

void main() {
	vec4 texColor = vec4(texture2D(tex, texCoord).rgb, 1.0);
	texColor *= color;
	gl_FragColor = texColor;
}
