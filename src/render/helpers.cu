
#define CUB_DISABLE_BF16_SUPPORT

#define GLM_FORCE_CUDA
#define CUDA_VERSION 12000

namespace std{
	using size_t = ::size_t;
};


#include <curand_kernel.h>

#include "./libs/glm/glm/glm.hpp"
#include "./libs/glm/glm/gtc/matrix_transform.hpp"
#include "./libs/glm/glm/gtc/matrix_access.hpp"
#include "./libs/glm/glm/gtx/transform.hpp"
#include "./libs/glm/glm/gtc/quaternion.hpp"

#include "../utils.cuh"

#include "../HostDeviceInterface.h"

constexpr float PI = 3.1415f;

extern "C" __global__
void kernel_drawLine(CommonLaunchArgs args, RenderTarget target, vec3 start, vec3 end, uint32_t color){

	auto grid = cg::this_grid();

	float u = float(grid.thread_rank()) / float(grid.size());

	vec3 pos = (1.0f - u) * start + u * end;

	mat4 transform = target.proj * target.view;

	vec4 ndc = transform * vec4(pos, 1.0f);
	// ndc.x = ndc.x / ndc.w;
	// ndc.y = ndc.y / ndc.w;
	// ndc.z = ndc.z / ndc.w;

	if(ndc.w < 0.0f) return;
	// if(ndc.x < -1.0f  || ndc.x > 1.0f) return;
	// if(ndc.y < -1.0f  || ndc.y > 1.0f) return;

	// if(grid.thread_rank() == 10000){
	// 	printf("%f, %f, %f \n", pos.x, pos.y, pos.z);
	// }

	ivec2 imgCoords = ivec2(
		(0.5f * (ndc.x / ndc.w) + 0.5f) * target.width,
		(0.5f * (ndc.y / ndc.w) + 0.5f) * target.height
	);

	if(imgCoords.x < 0 || imgCoords.x >= target.width) return;
	if(imgCoords.y < 0 || imgCoords.y >= target.height) return;

	int pixelID = int(imgCoords.x) + int(imgCoords.y) * target.width;

	if(pixelID >= target.width * target.height) return;
	if(pixelID < 0) return;

	float depth = ndc.w;
	uint64_t udepth = *((uint32_t*)&depth);
	uint64_t pixel = (udepth << 32ull) | color;

	atomicMin(&target.framebuffer[pixelID], pixel);

}

extern "C" __global__
void kernel_applyEyeDomeLighting(CommonLaunchArgs args, RenderTarget target){

	auto grid = cg::this_grid();

	auto uniforms = args.uniforms;

	float r = 1.5f;
	float edlStrength = 0.37f;
	float numSamples = 5;

	Pixel* framebuffer = ((Pixel*)target.framebuffer);

	int pixelID = grid.thread_rank();

	if(pixelID >= target.width * target.height) return;

	// process(target.width * target.height, [&](int pixelID){
		Pixel pixel = framebuffer[pixelID];

		// framebuffer[pixelID].color = p.color * 1;

		float sum = 0.0f;

		for(float u : {0.0f, PI / 2.0f, PI, 3.0f * PI / 2.0f}){
			int dx = r * sin(u);
			int dy = r * cos(u);

			int neighborIndex = pixelID + dx + target.width * dy;
			neighborIndex = max(neighborIndex, 0);
			neighborIndex = min(neighborIndex, target.width * target.height - 1);

			Pixel neighbor = framebuffer[neighborIndex];

			sum = sum + max(log2(pixel.depth) - log2(neighbor.depth), 0.0f);
		}

		float response = sum / 4.0f;
		float shade = __expf(-response * 300.0f * edlStrength);

		uint32_t R = shade * ((pixel.color >>  0) & 0xff);
		uint32_t G = shade * ((pixel.color >>  8) & 0xff);
		uint32_t B = shade * ((pixel.color >> 16) & 0xff);
		uint32_t color = R | (G << 8) | (B << 16) | (255u << 24);

		framebuffer[pixelID].color = color;
	// });


}

// Draws selection volumes as translucent overlays on top of the final image.
// Faces in front of the scene surface are blended with <fillAlpha>, hidden faces faintly.
extern "C" __global__
void kernel_drawVolumes(CommonLaunchArgs args, RenderTarget target, mat4 invView, VolumeGPU* volumes, int numVolumes){

	auto grid = cg::this_grid();

	int pixelID = grid.thread_rank();

	if(pixelID >= target.width * target.height) return;

	int px = pixelID % target.width;
	int py = pixelID / target.width;

	uint64_t pixel = target.framebuffer[pixelID];
	float surfaceDepth = __int_as_float(uint32_t(pixel >> 32));
	if(!(surfaceDepth > 0.0f && surfaceDepth < 1e30f)) surfaceDepth = 1e30f;

	// Same (unconventional) projection as in kernel_update_hovered: ndc.w is the linear depth.
	// With view-space direction z = -1, the ray parameter t equals the linear depth.
	vec3 dir_view = {
		(2.0f * (float(px) + 0.5f) / target.width - 1.0f) / target.proj[0][0],
		(2.0f * (float(py) + 0.5f) / target.height - 1.0f) / target.proj[1][1],
		-1.0f
	};
	vec3 origin = vec3(invView * vec4(0.0f, 0.0f, 0.0f, 1.0f));
	vec3 dir = vec3(invView * vec4(dir_view, 0.0f));

	uint32_t C = pixel & 0xffffffff;
	vec3 color = {
		float((C >>  0) & 0xff),
		float((C >>  8) & 0xff),
		float((C >> 16) & 0xff),
	};

	bool changed = false;

	for(int i = 0; i < numVolumes; i++){
		VolumeGPU volume = volumes[i];

		vec3 o = vec3(volume.worldToVolume * vec4(origin, 1.0f));
		vec3 d = vec3(volume.worldToVolume * vec4(dir, 0.0f));

		float t0, t1;
		if(!intersectUnitVolume(volume.shape, o, d, t0, t1)) continue;

		vec3 vcolor = {
			float((volume.color >>  0) & 0xff),
			float((volume.color >>  8) & 0xff),
			float((volume.color >> 16) & 0xff),
		};

		float hiddenAlpha = volume.fillAlpha * 0.3f;

		// back face first, then front face
		float faces[2] = {t1, t0};
		for(int j = 0; j < 2; j++){
			float t = faces[j];
			if(t <= 0.0f) continue;

			float a = (t < surfaceDepth) ? volume.fillAlpha : hiddenAlpha;
			color = (1.0f - a) * color + a * vcolor;
			changed = true;
		}
	}

	if(changed){
		uint32_t R = clamp(color.x, 0.0f, 255.0f);
		uint32_t G = clamp(color.y, 0.0f, 255.0f);
		uint32_t B = clamp(color.z, 0.0f, 255.0f);
		uint32_t newColor = R | (G << 8) | (B << 16) | (C & 0xff000000);

		target.framebuffer[pixelID] = (pixel & 0xffffffff'00000000) | uint64_t(newColor);
	}
}
