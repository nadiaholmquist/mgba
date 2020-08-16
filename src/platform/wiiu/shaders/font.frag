#version 100
precision mediump float;

varying vec2 texCoord;
uniform sampler2D tex;
uniform vec4 color;
uniform float cutoff;

void main() {
	vec4 texColor = texture2D(tex, texCoord);
	texColor.a = clamp((texColor.a - cutoff) / (1.0 - cutoff), 0.0, 1.0);
	texColor.rgb = color.rgb;
	texColor.a *= color.a;
	gl_FragColor = texColor;
}
