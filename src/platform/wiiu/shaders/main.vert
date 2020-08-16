#version 110

attribute vec2 offset;
uniform vec2 dims;
uniform vec2 insize;
varying vec2 texCoord;

void main() {
	vec2 ratio = insize;
	vec2 scaledOffset = offset * dims;
	gl_Position = vec4(scaledOffset.x * 2.0 - dims.x, scaledOffset.y * -2.0 + dims.y, 0.0, 1.0);
	texCoord = offset * ratio;
}
