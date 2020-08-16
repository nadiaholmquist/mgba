#version 100
precision mediump float;

attribute vec2 offset;
uniform vec3 origin;
uniform vec2 glyph;
uniform vec2 dims;
uniform mat2 transform;
varying vec2 texCoord;

void main() {
	texCoord = (glyph + offset * dims) / 512.0;
	vec2 scaledOffset = (transform * (offset * 2.0 - vec2(1.0)) + vec2(1.0)) / 2.0 * dims;
	gl_Position = vec4((origin.x + scaledOffset.x) / 640.0 - 1.0, -(origin.y + scaledOffset.y) / 360.0 + 1.0, origin.z, 1.0);
}
