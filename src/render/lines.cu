#define CUB_DISABLE_BF16_SUPPORT

// === required by GLM ===
#define GLM_FORCE_CUDA
#define CUDA_VERSION 12000
namespace std{
	using size_t = ::size_t;
};
// =======================

#include <curand_kernel.h>
#include <cooperative_groups.h>

#include "./libs/glm/glm/glm.hpp"
#include "./libs/glm/glm/gtc/matrix_transform.hpp"
#include "./libs/glm/glm/gtc/matrix_access.hpp"
#include "./libs/glm/glm/gtx/transform.hpp"
#include "./libs/glm/glm/gtc/quaternion.hpp"

#include "../utils.cuh"

#include "../HostDeviceInterface.h"
#include "./../math.cuh"

using glm::ivec2;

// __device__ uint32_t numProcessedLines;

inline vec4 toScreenCoord(vec3 p, mat4& transform, int width, int height){
	vec4 pos = transform * vec4{p.x, p.y, p.z, 1.0f};

	pos.x = pos.x / pos.w;
	pos.y = pos.y / pos.w;

	vec4 imgPos = {
		(pos.x * 0.5f + 0.5f) * width, 
		(pos.y * 0.5f + 0.5f) * height,
		pos.z, 
		pos.w
	};

	return imgPos;
}

extern "C" __global__
void kernel_drawLines(CommonLaunchArgs args, RenderTarget target, Line* lines, uint32_t* numLines, uint32_t* numProcessedLines){

	auto grid = cg::this_grid();
	auto block = cg::this_thread_block();

	auto index = cg::this_grid().thread_rank();


	Frustum frustum = Frustum::fromWorldViewProj(target.proj * target.view);

	__shared__ int sh_blockLineOffset;

	while(true){

		block.sync();
		if(block.thread_rank() == 0){
			sh_blockLineOffset = atomicAdd(numProcessedLines, 1);
		}
		block.sync();

		if(sh_blockLineOffset >= *numLines) return;

		Line line = lines[sh_blockLineOffset];

		vec3 start = line.start;
		vec3 end = line.end;

		{ // CLIP TO FRUSTUM

			vec3 origin = start;
			vec3 dir = normalize(end - start);

			vec3 I = frustum.intersectRay(origin, dir);

			bool containsStart = frustum.contains(start);
			bool containsEnd = frustum.contains(end);

			// if(!containsStart || !containsEnd)
			{


				vec3 I_start = frustum.intersectRay(start, dir);
				vec3 I_end = frustum.intersectRay(end, dir * -1.0f);

				float t_start_frustum = 0.0f;
				float t_end_frustum = dot(end - origin, dir);

				if(I_start.x != -Infinity){
					t_start_frustum = dot(I_start - origin, dir);
				}

				if(I_end.x != -Infinity){
					t_end_frustum = max(dot(I_end - origin, dir), 0.0f);
				}

				// if(line.start.z == -10.0f && block.thread_rank() == 0){
				// 	printf("%.1f, %.1f \n", t_start_frustum, t_end_frustum);
				// }

				if(t_start_frustum >= t_end_frustum) continue;

				start = origin + t_start_frustum * dir;
				end = origin + t_end_frustum * dir;

				// float t_istart = dot(I_start - origin, dir);
				// float t_end = dot(end - origin, dir);
				// float t_iend = dot(I_end - origin, dir);

				// // if(t_istart > t_end) continue;
				// // if(t_iend < 0) continue;

				// if(line.start.z == -10.0f && block.thread_rank() == 0){
				// 	// printf("%f \n", t_iend);
				// 	printf("%f, %f, %f \n", I_end.x, I_end.y, I_end.z);
				// 	printf("%f, %f, %f \n", I_start.x, I_start.y, I_start.z);
				// }


				// if(I_start.x != -Infinity){
				// 	start = I_start;
				// }

				// if(I_end.x != -Infinity){
				// 	end = I_end;
				// }
			}
		}


		vec4 start_ndc = target.proj * target.view * vec4(start, 1.0f);
		vec4 end_ndc = target.proj * target.view * vec4(end, 1.0f);

		if(start_ndc.w < 0.0f || end_ndc.w < 0.0f) continue;

		start_ndc.x = start_ndc.x / start_ndc.w;
		start_ndc.y = start_ndc.y / start_ndc.w;
		start_ndc.z = start_ndc.z / start_ndc.w;
		end_ndc.x = end_ndc.x / end_ndc.w;
		end_ndc.y = end_ndc.y / end_ndc.w;
		end_ndc.z = end_ndc.z / end_ndc.w;

		vec2 start_screen = {
			(start_ndc.x * 0.5f + 0.5f) * target.width,
			(start_ndc.y * 0.5f + 0.5f) * target.height,
		};
		vec2 end_screen = {
			(end_ndc.x * 0.5f + 0.5f) * target.width,
			(end_ndc.y * 0.5f + 0.5f) * target.height,
		};

		if(start_ndc.x < -1.0f && end_ndc.x < -1.0f) continue; // was "return", which stopped this block from drawing all remaining lines
		if(start_ndc.x >  1.0f && end_ndc.x >  1.0f) continue; // was "return", which stopped this block from drawing all remaining lines
		if(start_ndc.y < -1.0f && end_ndc.y < -1.0f) continue; // was "return", which stopped this block from drawing all remaining lines
		if(start_ndc.y >  1.0f && end_ndc.y >  1.0f) continue; // was "return", which stopped this block from drawing all remaining lines

		// if(sh_blockLineOffset == 0 && block.thread_rank() == 0){
		// 	printf("%.1f, %.1f to %.1f, %.1f  \n", start_screen.x, start_screen.y, end_screen.x, end_screen.y);
		// }


		vec2 delta = end_screen - start_screen;
		float distance = length(delta);
		vec2 dir = normalize(delta);

		distance = min(distance, 2000.0f); //safety

		int fragsPerThread = ceil(distance / float(block.num_threads()));

		vec2 pos = start_screen;
		int numFrags = ceil(distance);
		for(int i = 0; i < fragsPerThread; i++)
		// for(float i = 0.0f; i <= distance; i += 1.0f)

		for(
			int i = block.thread_rank();
			i <= numFrags;
			i += block.num_threads()
		){

			float u = float(i) / distance;

			vec2 pos = start_screen + float(i) * dir;

			// float u = float(i)
			// float u = i / distance;

			int x = clamp(pos.x, 0.0f, float(target.width - 1));
			int y = clamp(pos.y, 0.0f, float(target.height - 1));
			float zr = (1.0f - u) * (1.0f / start_ndc.w) + u * (1.0f / end_ndc.w);
			float z = 1.0f / zr;

			int pixelID = x + y * target.width;

			float depth = z;
			uint64_t udepth = __float_as_uint(depth);
			uint64_t pixel = (udepth << 32) | line.color;

			atomicMin(&target.framebuffer[pixelID], pixel);

			// pos = pos + dir;
		}



	}






	// // single threaded


	// Line line = lineData.lines[index];
	// vec3 start = line.start;
	// vec3 end = line.end;

	// { // CLIP TO FRUSTUM
	// 	Frustum frustum = Frustum::fromWorldViewProj(target.proj * target.view);

	// 	vec3 origin = start;
	// 	vec3 dir = normalize(end - start);

	// 	vec3 I = frustum.intersectRay(origin, dir);

	// 	bool containsStart = frustum.contains(start);
	// 	bool containsEnd = frustum.contains(end);

	// 	if(!containsStart || !containsEnd){

	// 		vec3 I_start = frustum.intersectRay(start, dir);
	// 		vec3 I_end = frustum.intersectRay(end, dir * -1.0f);


	// 		if(I_start.x != -Infinity){
	// 			start = I_start;
	// 		}

	// 		if(I_end.x != -Infinity){
	// 			end = I_end;
	// 		}
	// 	}
	// }

	// vec4 start_ndc = target.proj * target.view * vec4(start, 1.0f);
	// vec4 end_ndc = target.proj * target.view * vec4(end, 1.0f);

	// start_ndc.x = start_ndc.x / start_ndc.w;
	// start_ndc.y = start_ndc.y / start_ndc.w;
	// start_ndc.z = start_ndc.z / start_ndc.w;
	// end_ndc.x = end_ndc.x / end_ndc.w;
	// end_ndc.y = end_ndc.y / end_ndc.w;
	// end_ndc.z = end_ndc.z / end_ndc.w;

	// vec2 start_screen = {
	// 	(start_ndc.x * 0.5f + 0.5f) * target.width,
	// 	(start_ndc.y * 0.5f + 0.5f) * target.height,
	// };
	// vec2 end_screen = {
	// 	(end_ndc.x * 0.5f + 0.5f) * target.width,
	// 	(end_ndc.y * 0.5f + 0.5f) * target.height,
	// };


	// vec2 delta = end_screen - start_screen;
	// float distance = length(delta);
	// vec2 dir = normalize(delta);

	// distance = min(distance, 2000.0f); //safety

	// vec2 pos = start_screen;
	// for(float i = 0.0f; i <= distance; i += 1.0f){

	// 	float u = i / distance;

	// 	int x = clamp(pos.x, 0.0f, float(target.width - 1));
	// 	int y = clamp(pos.y, 0.0f, float(target.height - 1));
	// 	float zr = (1.0f - u) * (1.0f / start_ndc.w) + u * (1.0f / end_ndc.w);
	// 	float z = 1.0f / zr;

	// 	int pixelID = x + y * target.width;

	// 	float depth = z;
	// 	uint64_t udepth = __float_as_uint(depth);
	// 	uint64_t pixel = (udepth << 32) | line.color;

	// 	atomicMin(&target.framebuffer[pixelID], pixel);

	// 	pos = pos + dir;
	// }

}
